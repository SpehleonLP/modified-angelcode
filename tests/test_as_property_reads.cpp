// The property-read report (GetPropertyReadCount / GetPropertyRead /
// GetPropertyReadsComplete): which script-class properties a function reads,
// transitively, and whether each read went through `this`. The fork only
// reports; the host decides what a read through another object means.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace {

class MemStream : public asIBinaryStream {
public:
	std::vector<asBYTE> data;
	size_t pos = 0;
	int Write(const void *ptr, asUINT size) override {
		data.insert(data.end(), (const asBYTE*)ptr, (const asBYTE*)ptr + size);
		return 0;
	}
	int Read(void *ptr, asUINT size) override {
		if (pos + size > data.size()) {
			return -1;
		}
		memcpy(ptr, &data[pos], size);
		pos += size;
		return 0;
	}
};

void MessageCallback(const asSMessageInfo* msg, void* param) {
	std::string* out = static_cast<std::string*>(param);
	*out += msg->message;
	*out += "\n";
}

// {class name, property name, viaThis}
using Read = std::tuple<std::string, std::string, bool>;

float Native() { return 0; }

const char* kClassHead = "class G { G@ other; float a; float b; private float p; float getA() const { return a; } ";

class AsPropertyReads : public ::testing::Test {
protected:
	asIScriptEngine* engine = nullptr;
	asIScriptModule* mod = nullptr;
	std::string messages;

	void SetUp() override {
		engine = asCreateScriptEngine();
		ASSERT_NE(engine, nullptr);
		engine->SetMessageCallback(asFUNCTION(MessageCallback), &messages, asCALL_CDECL);
	}
	void TearDown() override {
		if (engine) {
			engine->ShutDownAndRelease();
		}
	}

	bool Build(const std::string& code) {
		mod = engine->GetModule("reads", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", code.c_str());
		return mod->Build() >= 0;
	}

	// Builds G with the extra members and returns G's own (non-virtual) m
	asIScriptFunction* BuildMethod(const std::string& members) {
		if (!Build(std::string(kClassHead) + members + " }\n")) {
			ADD_FAILURE() << messages;
			return nullptr;
		}
		return Method(mod, "G", "m");
	}

	static asIScriptFunction* Method(asIScriptModule* m, const char* type, const char* name) {
		asITypeInfo* t = m->GetTypeInfoByName(type);
		EXPECT_NE(t, nullptr) << type;
		if (!t) {
			return nullptr;
		}
		asIScriptFunction* f = t->GetMethodByName(name, false);
		EXPECT_NE(f, nullptr) << name;
		return f;
	}

	static std::vector<Read> Report(asIScriptFunction* f) {
		std::vector<Read> out;
		if (!f) {
			return out;
		}
		for (asUINT n = 0; n < f->GetPropertyReadCount(); n++) {
			asITypeInfo* type = nullptr;
			asUINT index = 0;
			bool viaThis = false;
			EXPECT_EQ(f->GetPropertyRead(n, &type, &index, &viaThis), asSUCCESS);
			const char* propName = nullptr;
			EXPECT_NE(type, nullptr);
			if (type) {
				type->GetProperty(index, &propName);
				out.emplace_back(type->GetName(), propName ? propName : "?", viaThis);
			}
		}
		return out;
	}

	static bool Has(const std::vector<Read>& report, const Read& r) {
		return std::find(report.begin(), report.end(), r) != report.end();
	}
};

} // namespace

TEST_F(AsPropertyReads, DirectThisReadIsViaThis) {
	asIScriptFunction* m = BuildMethod("float m() const { return a; }");
	ASSERT_NE(m, nullptr);
	EXPECT_EQ(Report(m), (std::vector<Read>{ Read("G", "a", true) }));
	EXPECT_TRUE(m->GetPropertyReadsComplete());
}

TEST_F(AsPropertyReads, ExplicitThisReadIsViaThis) {
	asIScriptFunction* m = BuildMethod("float m() const { return this.a; }");
	EXPECT_EQ(Report(m), (std::vector<Read>{ Read("G", "a", true) }));
}

TEST_F(AsPropertyReads, OtherObjectReadIsNotViaThis) {
	asIScriptFunction* m = BuildMethod("float m() const { return other.a; }");
	std::vector<Read> report = Report(m);
	EXPECT_EQ(report.size(), 2u);
	EXPECT_TRUE(Has(report, Read("G", "other", true)));
	EXPECT_TRUE(Has(report, Read("G", "a", false)));
}

TEST_F(AsPropertyReads, AccessorOnThisStaysViaThis) {
	asIScriptFunction* m = BuildMethod("float m() const { return getA(); }");
	std::vector<Read> report = Report(m);
	EXPECT_EQ(report, (std::vector<Read>{ Read("G", "a", true) }));
	EXPECT_TRUE(m->GetPropertyReadsComplete());
}

TEST_F(AsPropertyReads, AccessorOnOtherBypassesNothing) {
	asIScriptFunction* m = BuildMethod("float m() const { return other.getA(); }");
	std::vector<Read> report = Report(m);
	EXPECT_TRUE(Has(report, Read("G", "a", false)));
	EXPECT_FALSE(Has(report, Read("G", "a", true)));
	EXPECT_TRUE(Has(report, Read("G", "other", true)));
}

// One callee reached through both receivers contributes both reads
TEST_F(AsPropertyReads, AccessorOnBothReceiversReportsBoth) {
	asIScriptFunction* m = BuildMethod("float m() const { return getA() + other.getA(); }");
	std::vector<Read> report = Report(m);
	EXPECT_TRUE(Has(report, Read("G", "a", true)));
	EXPECT_TRUE(Has(report, Read("G", "a", false)));
}

TEST_F(AsPropertyReads, PrivateDoesNotHideOtherReads) {
	asIScriptFunction* m = BuildMethod("float m() const { return other.p; }");
	ASSERT_NE(m, nullptr);
	EXPECT_TRUE(Has(Report(m), Read("G", "p", false)));
}

TEST_F(AsPropertyReads, ReportHasNoDuplicates) {
	asIScriptFunction* m = BuildMethod("float m() const { return a + a * getA() + this.a; }");
	EXPECT_EQ(Report(m), (std::vector<Read>{ Read("G", "a", true) }));
}

// A local or a parameter of class type is not `this`, even when it holds it
TEST_F(AsPropertyReads, HandleToThisIsNotViaThis) {
	asIScriptFunction* m = BuildMethod("float m() const { const G@ h = this; return h.a; }");
	EXPECT_EQ(Report(m), (std::vector<Read>{ Read("G", "a", false) }));
}

// A global function has no `this`; a method it calls reads through its receiver
TEST_F(AsPropertyReads, GlobalCallerSeesNoThisReads) {
	ASSERT_TRUE(Build(std::string(kClassHead) + "}\nfloat f(G@ g) { return g.getA() + g.b; }\n")) << messages;
	asIScriptFunction* f = mod->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);
	std::vector<Read> report = Report(f);
	EXPECT_EQ(report.size(), 2u);
	EXPECT_TRUE(Has(report, Read("G", "a", false)));
	EXPECT_TRUE(Has(report, Read("G", "b", false)));
}

// A virtual stub has no body: it reports the join over its dispatch targets,
// with `this` meaning the stub's own receiver
TEST_F(AsPropertyReads, VirtualStubJoinsItsTargets) {
	ASSERT_TRUE(Build(
		"class B { float x; float m() const { return x; } }\n"
		"class D : B { float y; float m() const override { return y; } }\n")) << messages;
	asIScriptFunction* stub = mod->GetTypeInfoByName("B")->GetMethodByName("m");
	ASSERT_NE(stub, nullptr);
	ASSERT_EQ(stub->GetFuncType(), asFUNC_VIRTUAL);
	std::vector<Read> report = Report(stub);
	EXPECT_TRUE(Has(report, Read("B", "x", true)));
	EXPECT_TRUE(Has(report, Read("D", "y", true)));
	EXPECT_TRUE(stub->GetPropertyReadsComplete());
}

// Fail closed: a call the fold cannot follow may read anything, so the report
// says it is incomplete, and so does every caller's
TEST_F(AsPropertyReads, UnresolvedCallMakesTheReportIncomplete) {
	ASSERT_TRUE(Build(std::string(kClassHead) +
		"float viaDelegate(CB@ cb) const { return cb() + a; } "
		"float outer() const { return viaDelegate(null); } "
		"float plain() const { return a; } }\n"
		"funcdef float CB();\n"
		"shared class S { float s; float m() const { return s; } }\n"
		"float viaShared(S@ s) { return s.m(); }\n")) << messages;
	asIScriptFunction* viaDelegate = Method(mod, "G", "viaDelegate");
	EXPECT_FALSE(viaDelegate->GetPropertyReadsComplete());
	EXPECT_TRUE(Has(Report(viaDelegate), Read("G", "a", true)));
	EXPECT_FALSE(Method(mod, "G", "outer")->GetPropertyReadsComplete());
	EXPECT_TRUE(Method(mod, "G", "plain")->GetPropertyReadsComplete());
	EXPECT_FALSE(mod->GetFunctionByName("viaShared")->GetPropertyReadsComplete());
}

TEST_F(AsPropertyReads, OutOfRangeIndexIsRefused) {
	asIScriptFunction* m = BuildMethod("float m() const { return a; }");
	ASSERT_NE(m, nullptr);
	asITypeInfo* type = nullptr;
	asUINT index = 0;
	bool viaThis = false;
	EXPECT_EQ(m->GetPropertyRead(m->GetPropertyReadCount(), &type, &index, &viaThis), asINVALID_ARG);
}

// Natives are trusted to read no script properties, as they are trusted to
// halt; the host's own declarations cover a native that reaches script
TEST_F(AsPropertyReads, NativeHasAnEmptyCompleteReport) {
	ASSERT_GE(engine->RegisterGlobalFunction("float native()", asFUNCTION(Native), asCALL_CDECL), 0);
	asIScriptFunction* native = engine->GetGlobalFunctionByDecl("float native()");
	ASSERT_NE(native, nullptr);
	EXPECT_EQ(native->GetPropertyReadCount(), 0u);
	EXPECT_TRUE(native->GetPropertyReadsComplete());

	ASSERT_TRUE(Build(std::string(kClassHead) + "float m() const { return native() + a; } }\n")) << messages;
	asIScriptFunction* m = Method(mod, "G", "m");
	EXPECT_EQ(Report(m), (std::vector<Read>{ Read("G", "a", true) }));
	EXPECT_TRUE(m->GetPropertyReadsComplete());
}

TEST_F(AsPropertyReads, CompileMethodProductHasTheReport) {
	ASSERT_TRUE(Build(std::string(kClassHead) + "}\n")) << messages;
	asITypeInfo* g = mod->GetTypeInfoByName("G");
	ASSERT_NE(g, nullptr);
	asIScriptFunction* x = nullptr;
	ASSERT_EQ(mod->CompileMethod(g, "gauge", "float x() const { return a * b; }", 0, &x), asSUCCESS) << messages;
	ASSERT_NE(x, nullptr);
	std::vector<Read> report = Report(x);
	EXPECT_EQ(report.size(), 2u);
	EXPECT_TRUE(Has(report, Read("G", "a", true)));
	EXPECT_TRUE(Has(report, Read("G", "b", true)));
	EXPECT_TRUE(x->GetPropertyReadsComplete());
	x->Release();
}

TEST_F(AsPropertyReads, CompileMethodProductFoldsCalleeReceivers) {
	ASSERT_TRUE(Build(std::string(kClassHead) + "}\n")) << messages;
	asITypeInfo* g = mod->GetTypeInfoByName("G");
	asIScriptFunction* x = nullptr;
	ASSERT_EQ(mod->CompileMethod(g, "gauge", "float x() const { return getA() + other.getA(); }", 0, &x), asSUCCESS) << messages;
	ASSERT_NE(x, nullptr);
	std::vector<Read> report = Report(x);
	EXPECT_TRUE(Has(report, Read("G", "a", true)));
	EXPECT_TRUE(Has(report, Read("G", "a", false)));
	x->Release();
}

TEST_F(AsPropertyReads, ReportSurvivesSaveAndLoad) {
	ASSERT_TRUE(Build(std::string(kClassHead) +
		"float onThis() const { return getA() + b; } "
		"float onOther() const { return other.getA(); } }\n")) << messages;
	std::vector<Read> onThis = Report(Method(mod, "G", "onThis"));
	std::vector<Read> onOther = Report(Method(mod, "G", "onOther"));
	ASSERT_EQ(onThis, (std::vector<Read>{ Read("G", "a", true), Read("G", "b", true) }));
	ASSERT_TRUE(Has(onOther, Read("G", "a", false)));

	MemStream stream;
	ASSERT_GE(mod->SaveByteCode(&stream), 0);
	asIScriptModule* restored = engine->GetModule("restored", asGM_ALWAYS_CREATE);
	ASSERT_GE(restored->LoadByteCode(&stream), 0) << messages;

	asIScriptFunction* restoredOnThis = Method(restored, "G", "onThis");
	asIScriptFunction* restoredOnOther = Method(restored, "G", "onOther");
	EXPECT_EQ(Report(restoredOnThis), onThis);
	EXPECT_EQ(Report(restoredOnOther), onOther);
	EXPECT_TRUE(restoredOnThis->GetPropertyReadsComplete());
	asITypeInfo* type = nullptr;
	ASSERT_EQ(restoredOnThis->GetPropertyRead(0, &type, nullptr, nullptr), asSUCCESS);
	EXPECT_EQ(type, restored->GetTypeInfoByName("G"));
}

// A property accessor is a method call: on the implicit `this` it keeps the
// accessor's `this` reads, as an explicit `this.` does
TEST_F(AsPropertyReads, ImplicitPropertyAccessorStaysViaThis) {
	asIScriptFunction* m = BuildMethod(
		"float get_x() const property { return a; } void set_x(float v) property { b = v; } "
		"float m() { x = 1; return x + this.x; } "
		"float n() const { return other.x; }");
	ASSERT_NE(m, nullptr);
	std::vector<Read> report = Report(m);
	EXPECT_TRUE(Has(report, Read("G", "a", true)));
	EXPECT_TRUE(Has(report, Read("G", "b", true)));
	EXPECT_FALSE(Has(report, Read("G", "a", false)));
	EXPECT_FALSE(Has(report, Read("G", "b", false)));
	EXPECT_TRUE(Has(Report(Method(mod, "G", "n")), Read("G", "a", false)));
}
