// asIScriptModule::GetTransitiveSystemCallees: the natives a function may call,
// directly or through script, along the edges the transitive analysis follows.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <algorithm>
#include <string>
#include <vector>

namespace {

void MessageCallback(const asSMessageInfo* msg, void* param) {
	std::string* out = static_cast<std::string*>(param);
	*out += msg->message;
	*out += "\n";
}

int Alpha() { return 1; }
int Beta() { return 2; }
int Gamma() { return 3; }

int g_box = 0;
void* BoxFactory(asITypeInfo*) { return &g_box; }
int BoxGet(void*) { return 4; }
void RefNoop(void*) {}
void* RefFactory() { return &g_box; }

class AsSystemCallees : public ::testing::Test {
protected:
	asIScriptEngine* engine = nullptr;
	asIScriptModule* mod = nullptr;
	std::string messages;

	void SetUp() override {
		engine = asCreateScriptEngine();
		ASSERT_NE(engine, nullptr);
		engine->SetMessageCallback(asFUNCTION(MessageCallback), &messages, asCALL_CDECL);
		ASSERT_GE(engine->RegisterGlobalFunction("int alpha()", asFUNCTION(Alpha), asCALL_CDECL), 0);
		ASSERT_GE(engine->RegisterGlobalFunction("int beta()", asFUNCTION(Beta), asCALL_CDECL), 0);
		ASSERT_GE(engine->RegisterGlobalFunction("int gamma()", asFUNCTION(Gamma), asCALL_CDECL), 0);
		// A template's factory is reached through a script stub the engine makes.
		ASSERT_GE(engine->RegisterObjectType("tbox<class T>", 0, asOBJ_REF | asOBJ_NOCOUNT | asOBJ_TEMPLATE), 0);
		ASSERT_GE(engine->RegisterObjectBehaviour("tbox<T>", asBEHAVE_FACTORY, "tbox<T>@ f(int&in)", asFUNCTION(BoxFactory), asCALL_CDECL), 0);
		ASSERT_GE(engine->RegisterObjectMethod("tbox<T>", "int get() const", asFUNCTION(BoxGet), asCALL_CDECL_OBJLAST), 0);
		// A counted type: copying its handle calls addref and release implicitly.
		ASSERT_GE(engine->RegisterObjectType("R", 0, asOBJ_REF), 0);
		ASSERT_GE(engine->RegisterObjectBehaviour("R", asBEHAVE_FACTORY, "R@ f()", asFUNCTION(RefFactory), asCALL_CDECL), 0);
		ASSERT_GE(engine->RegisterObjectBehaviour("R", asBEHAVE_ADDREF, "void f()", asFUNCTION(RefNoop), asCALL_CDECL_OBJLAST), 0);
		ASSERT_GE(engine->RegisterObjectBehaviour("R", asBEHAVE_RELEASE, "void f()", asFUNCTION(RefNoop), asCALL_CDECL_OBJLAST), 0);
	}
	void TearDown() override {
		if (engine) {
			engine->ShutDownAndRelease();
		}
	}

	bool Build(const std::string& code) {
		mod = engine->GetModule("callees", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", code.c_str());
		return mod->Build() >= 0;
	}

	asIScriptFunction* Fn(const char* name) {
		asIScriptFunction* f = mod->GetFunctionByName(name);
		EXPECT_NE(f, nullptr) << name;
		return f;
	}

	// The declarations of what GetTransitiveSystemCallees lists, sorted.
	std::vector<std::string> Callees(asIScriptFunction* fn, bool* complete) {
		asUINT const count = mod->GetTransitiveSystemCallees(fn, nullptr, 0, complete);
		std::vector<asIScriptFunction*> found(count);
		EXPECT_EQ(mod->GetTransitiveSystemCallees(fn, found.data(), count, nullptr), count);
		std::vector<std::string> names;
		for (asIScriptFunction* f : found) {
			names.push_back(f->GetDeclaration(true, false, false));
		}
		std::sort(names.begin(), names.end());
		return names;
	}
};

} // namespace

TEST_F(AsSystemCallees, DirectAndThroughAScriptHelper) {
	ASSERT_TRUE(Build(
		"int helper() { return beta(); }\n"
		"int f() { return alpha() + helper() + helper(); }\n")) << messages;
	bool complete = false;
	EXPECT_EQ(Callees(Fn("f"), &complete), (std::vector<std::string>{"int alpha()", "int beta()"}));
	EXPECT_TRUE(complete);
}

TEST_F(AsSystemCallees, EveryOverrideOfAVirtualCall) {
	ASSERT_TRUE(Build(
		"class B { int m() { return alpha(); } }\n"
		"class D : B { int m() override { return gamma(); } }\n"
		"int f(B@ b) { return b.m(); }\n")) << messages;
	bool complete = false;
	EXPECT_EQ(Callees(Fn("f"), &complete), (std::vector<std::string>{"int alpha()", "int gamma()"}));
	EXPECT_TRUE(complete);
}

TEST_F(AsSystemCallees, ADelegateCallIsIncomplete) {
	ASSERT_TRUE(Build(
		"funcdef int Fn();\n"
		"int f(Fn@ fn) { return alpha() + fn(); }\n")) << messages;
	bool complete = true;
	EXPECT_EQ(Callees(Fn("f"), &complete), (std::vector<std::string>{"int alpha()"}));
	EXPECT_FALSE(complete);
}

TEST_F(AsSystemCallees, ATemplateFactoryIsFollowedThroughItsStub) {
	ASSERT_TRUE(Build("int f() { tbox<int> b; return b.get(); }\n")) << messages;
	bool complete = false;
	std::vector<std::string> const names = Callees(Fn("f"), &complete);
	EXPECT_TRUE(complete);
	ASSERT_EQ(names.size(), 2u);
	EXPECT_NE(names[0].find("get"), std::string::npos) << names[0];
	EXPECT_NE(names[1].find("tbox"), std::string::npos) << names[1];
}

TEST_F(AsSystemCallees, ADetachedMethodProductIsWalkedAgainstItsModule) {
	ASSERT_TRUE(Build(
		"class P { int helper() const { return beta(); } }\n")) << messages;
	asIScriptFunction* product = nullptr;
	ASSERT_GE(mod->CompileMethod(mod->GetTypeInfoByName("P"), "m", "int g() const { return helper() + gamma(); }", 0, &product), 0) << messages;
	bool complete = false;
	EXPECT_EQ(Callees(product, &complete), (std::vector<std::string>{"int beta()", "int gamma()"}));
	EXPECT_TRUE(complete);
	product->Release();
}

TEST_F(AsSystemCallees, ImplicitBehaviourCallsAreNotListed) {
	ASSERT_TRUE(Build("void f(R@ r) { R@ s = r; }\n")) << messages;
	bool complete = false;
	EXPECT_TRUE(Callees(Fn("f"), &complete).empty());
	EXPECT_TRUE(complete);
}

TEST_F(AsSystemCallees, CapacityBoundsTheCopyNotTheCount) {
	ASSERT_TRUE(Build("int f() { return alpha() + beta() + gamma(); }\n")) << messages;
	asIScriptFunction* one[1] = {nullptr};
	EXPECT_EQ(mod->GetTransitiveSystemCallees(Fn("f"), one, 1), 3u);
	EXPECT_NE(one[0], nullptr);
	bool complete = true;
	EXPECT_EQ(mod->GetTransitiveSystemCallees(nullptr, nullptr, 0, &complete), 0u);
	EXPECT_FALSE(complete);
}
