// The derived loops fact (GetLocalLoops / GetTransitiveLoops / GetLoopsVia /
// SetLoops). Nothing declares it: the compiler derives the local fact from
// reachable back edges, and the module pass folds it along the call graph the
// way it folds halting. A native contributes only through SetLoops.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <cstring>
#include <string>
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

int Scan() { return 0; }
void Construct(void*) {}

const char* kLoopingHelper =
	"int f() { int s = 0; for (int i = 0; i < 4; ++i) { s += i; } return s; }\n";

class AsLoopsFact : public ::testing::Test {
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
		mod = engine->GetModule("loops", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", code.c_str());
		return mod->Build() >= 0;
	}

	asIScriptFunction* Fn(const char* name) {
		asIScriptFunction* f = mod->GetFunctionByName(name);
		EXPECT_NE(f, nullptr) << name;
		return f;
	}
};

} // namespace

TEST_F(AsLoopsFact, StraightLineCodeDoesNotLoop) {
	ASSERT_TRUE(Build("float f(float x) { return x > 0 ? x : -x; }")) << messages;
	asIScriptFunction* f = Fn("f");
	ASSERT_NE(f, nullptr);
	EXPECT_FALSE(f->GetLocalLoops());
	EXPECT_FALSE(f->GetTransitiveLoops());
	EXPECT_EQ(f->GetLoopsVia(), nullptr);
}

// The counted-loop prover makes this halt, and it is still a loop: halting
// and looping answer different questions.
TEST_F(AsLoopsFact, ACountedLoopLoops) {
	ASSERT_TRUE(Build(kLoopingHelper)) << messages;
	asIScriptFunction* f = Fn("f");
	ASSERT_NE(f, nullptr);
	EXPECT_TRUE(f->GetLocalLoops());
	EXPECT_TRUE(f->GetTransitiveLoops());
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
	EXPECT_EQ(f->GetLoopsVia(), nullptr);
}

// The pruning pass drops back edges that are not loops; a for(;;) that
// returns on its first pass leaves only such an edge.
TEST_F(AsLoopsFact, AnUnreachableBackEdgeIsNotALoop) {
	ASSERT_TRUE(Build("int f() { for (;;) { return 1; } }")) << messages;
	asIScriptFunction* f = Fn("f");
	ASSERT_NE(f, nullptr);
	EXPECT_FALSE(f->GetLocalLoops());
}

TEST_F(AsLoopsFact, CallingALoopingHelperLoopsTransitively) {
	ASSERT_TRUE(Build(std::string(kLoopingHelper) + "int g() { return f() + 1; }\n")) << messages;
	asIScriptFunction* f = Fn("f");
	asIScriptFunction* g = Fn("g");
	ASSERT_NE(g, nullptr);
	EXPECT_FALSE(g->GetLocalLoops());
	EXPECT_TRUE(g->GetTransitiveLoops());
	EXPECT_EQ(g->GetLoopsVia(), f);
}

// The via names the direct callee, not the function that loops locally.
TEST_F(AsLoopsFact, LoopsViaNamesTheDirectCallee) {
	ASSERT_TRUE(Build(std::string(kLoopingHelper) +
		"int g() { return f(); }\n"
		"int h() { return g(); }\n")) << messages;
	EXPECT_TRUE(Fn("h")->GetTransitiveLoops());
	EXPECT_EQ(Fn("h")->GetLoopsVia(), Fn("g"));
}

// Recursion is a loop through the call graph, as the halting fold also
// treats it.
TEST_F(AsLoopsFact, RecursionLoopsTransitively) {
	ASSERT_TRUE(Build("int r(int n) { return n > 0 ? r(n - 1) : 0; }\n")) << messages;
	asIScriptFunction* r = Fn("r");
	ASSERT_NE(r, nullptr);
	EXPECT_FALSE(r->GetLocalLoops());
	EXPECT_TRUE(r->GetTransitiveLoops());
	EXPECT_EQ(r->GetLoopsVia(), r);
}

TEST_F(AsLoopsFact, NativeMarkedLoopingLoopsTransitively) {
	ASSERT_GE(engine->RegisterGlobalFunction("int scan()", asFUNCTION(Scan), asCALL_CDECL), 0);
	ASSERT_GE(engine->RegisterGlobalFunction("int plain()", asFUNCTION(Scan), asCALL_CDECL), 0);
	asIScriptFunction* scan = engine->GetGlobalFunctionByDecl("int scan()");
	ASSERT_NE(scan, nullptr);
	EXPECT_FALSE(scan->GetLocalLoops());
	EXPECT_FALSE(scan->GetTransitiveLoops());
	ASSERT_EQ(scan->SetLoops(true), asSUCCESS);
	EXPECT_TRUE(scan->GetLocalLoops());
	EXPECT_TRUE(scan->GetTransitiveLoops());

	ASSERT_TRUE(Build(
		"int h() { return scan(); }\n"
		"int k() { return plain(); }\n")) << messages;
	EXPECT_TRUE(Fn("h")->GetTransitiveLoops());
	EXPECT_EQ(Fn("h")->GetLoopsVia(), scan);
	EXPECT_FALSE(Fn("k")->GetTransitiveLoops());
	EXPECT_EQ(Fn("k")->GetLoopsVia(), nullptr);
}

TEST_F(AsLoopsFact, NativeMarkedLoopingFoldsThroughAMethodCall) {
	ASSERT_GE(engine->RegisterObjectType("Cursor", 0, asOBJ_REF | asOBJ_NOCOUNT), 0);
	ASSERT_GE(engine->RegisterObjectMethod("Cursor", "int scan()", asFUNCTION(Scan), asCALL_CDECL_OBJLAST), 0);
	asITypeInfo* cursor = engine->GetTypeInfoByName("Cursor");
	ASSERT_NE(cursor, nullptr);
	asIScriptFunction* scan = cursor->GetMethodByName("scan");
	ASSERT_NE(scan, nullptr);
	ASSERT_EQ(scan->SetLoops(true), asSUCCESS);

	ASSERT_TRUE(Build("int h(Cursor@ c) { return c.scan(); }\n")) << messages;
	EXPECT_TRUE(Fn("h")->GetTransitiveLoops());
	EXPECT_EQ(Fn("h")->GetLoopsVia(), scan);
}

TEST_F(AsLoopsFact, TemplateInstanceMethodInheritsTheNativeLoopsFact) {
	ASSERT_GE(engine->RegisterObjectType("Box<class T>", 0, asOBJ_REF | asOBJ_NOCOUNT | asOBJ_TEMPLATE), 0);
	ASSERT_GE(engine->RegisterObjectMethod("Box<T>", "int scan()", asFUNCTION(Scan), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(engine->RegisterObjectMethod("Box<T>", "int plain()", asFUNCTION(Scan), asCALL_CDECL_OBJLAST), 0);
	asITypeInfo* tmpl = engine->GetTypeInfoByName("Box");
	ASSERT_NE(tmpl, nullptr);
	ASSERT_EQ(tmpl->GetMethodByName("scan")->SetLoops(true), asSUCCESS);

	ASSERT_TRUE(Build(
		"int h(Box<int>@ b) { return b.scan(); }\n"
		"int k(Box<int>@ b) { return b.plain(); }\n")) << messages;
	asITypeInfo* inst = engine->GetTypeInfoByDecl("Box<int>");
	ASSERT_NE(inst, nullptr);
	EXPECT_TRUE(inst->GetMethodByName("scan")->GetTransitiveLoops());
	EXPECT_FALSE(inst->GetMethodByName("plain")->GetTransitiveLoops());
	EXPECT_TRUE(Fn("h")->GetTransitiveLoops());
	EXPECT_FALSE(Fn("k")->GetTransitiveLoops());
}

TEST_F(AsLoopsFact, SetLoopsOnAScriptFunctionIsRefused) {
	ASSERT_TRUE(Build("int f() { return 1; }\n")) << messages;
	asIScriptFunction* f = Fn("f");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->SetLoops(true), asNOT_SUPPORTED);
	EXPECT_FALSE(f->GetLocalLoops());
	EXPECT_FALSE(f->GetTransitiveLoops());
}

// Same rule as the halting fold: a CALLINTF site has an edge to every
// override GetDispatchTargets finds, so one looping override is enough.
TEST_F(AsLoopsFact, MethodCalledThroughAnInterfaceOrVirtualFoldsEveryOverride) {
	ASSERT_TRUE(Build(
		"class B { int m() { return 1; } }\n"
		"class D : B { int m() { int s = 0; for (int i = 0; i < 4; ++i) { s += i; } return s; } }\n"
		"interface I { int n(); }\n"
		"class P : I { int n() { return 1; } }\n"
		"class Q : I { int n() { int s = 0; for (int i = 0; i < 4; ++i) { s += i; } return s; } }\n"
		"int viaVirtual(B@ b) { return b.m(); }\n"
		"int viaInterface(I@ i) { return i.n(); }\n")) << messages;

	asIScriptFunction* dm = mod->GetTypeInfoByName("D")->GetMethodByName("m", false);
	asIScriptFunction* qn = mod->GetTypeInfoByName("Q")->GetMethodByName("n", false);
	ASSERT_NE(dm, nullptr);
	ASSERT_NE(qn, nullptr);

	asIScriptFunction* viaVirtual = Fn("viaVirtual");
	EXPECT_FALSE(viaVirtual->GetLocalLoops());
	EXPECT_TRUE(viaVirtual->GetTransitiveLoops());
	EXPECT_EQ(viaVirtual->GetLoopsVia(), dm);

	asIScriptFunction* viaInterface = Fn("viaInterface");
	EXPECT_FALSE(viaInterface->GetLocalLoops());
	EXPECT_TRUE(viaInterface->GetTransitiveLoops());
	EXPECT_EQ(viaInterface->GetLoopsVia(), qn);

	// GetMethodByName defaults to the virtual stub, which has no body: it
	// answers with the join over everything a call through it can reach.
	asITypeInfo* b = mod->GetTypeInfoByName("B");
	EXPECT_FALSE(b->GetMethodByName("m", false)->GetTransitiveLoops());
	asIScriptFunction* bStub = b->GetMethodByName("m");
	ASSERT_NE(bStub, nullptr);
	EXPECT_EQ(bStub->GetFuncType(), asFUNC_VIRTUAL);
	EXPECT_TRUE(bStub->GetTransitiveLoops());
	EXPECT_EQ(bStub->GetLoopsVia(), dm);

	asIScriptFunction* iStub = mod->GetTypeInfoByName("I")->GetMethodByName("n");
	ASSERT_NE(iStub, nullptr);
	EXPECT_EQ(iStub->GetFuncType(), asFUNC_INTERFACE);
	EXPECT_TRUE(iStub->GetTransitiveLoops());

	// A stub whose every target is straight-line does not loop
	asIScriptFunction* pStub = mod->GetTypeInfoByName("P")->GetMethodByName("n");
	ASSERT_NE(pStub, nullptr);
	EXPECT_EQ(pStub->GetFuncType(), asFUNC_VIRTUAL);
	EXPECT_FALSE(pStub->GetTransitiveLoops());
}

// A shared type can gain overrides in modules built later, so a call through
// it cannot be closed now.
TEST_F(AsLoopsFact, OpenDispatchLoops) {
	ASSERT_TRUE(Build(
		"shared class S { int m() { return 1; } }\n"
		"int viaShared(S@ s) { return s.m(); }\n")) << messages;
	EXPECT_TRUE(Fn("viaShared")->GetTransitiveLoops());
	EXPECT_EQ(Fn("viaShared")->GetLoopsVia(), nullptr);
	EXPECT_TRUE(mod->GetTypeInfoByName("S")->GetMethodByName("m")->GetTransitiveLoops());
}

// The callee behind a funcdef handle is unknown, so the call may loop
TEST_F(AsLoopsFact, FuncdefCallLoops) {
	ASSERT_TRUE(Build(
		"funcdef int CB();\n"
		"int call(CB@ cb) { return cb(); }\n"
		"int outer(CB@ cb) { return call(cb); }\n")) << messages;
	asIScriptFunction* call = Fn("call");
	EXPECT_FALSE(call->GetLocalLoops());
	EXPECT_TRUE(call->GetTransitiveLoops());
	EXPECT_EQ(call->GetLoopsVia(), nullptr);
	EXPECT_TRUE(Fn("outer")->GetTransitiveLoops());
	EXPECT_EQ(Fn("outer")->GetLoopsVia(), call);
}

// A local value is built by CALLSYS on its native constructor; a non-POD
// value member is heap-allocated through asBC_ALLOC. Both must see it.
TEST_F(AsLoopsFact, NativeConstructorMarkedLoopingLoopsTheConstructingCaller) {
	ASSERT_GE(engine->RegisterObjectType("V", 4, asOBJ_VALUE | asOBJ_APP_CLASS_CD), 0);
	ASSERT_GE(engine->RegisterObjectBehaviour("V", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(Construct), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(engine->RegisterObjectBehaviour("V", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(Construct), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(engine->RegisterObjectType("W", 4, asOBJ_VALUE | asOBJ_APP_CLASS_CD), 0);
	ASSERT_GE(engine->RegisterObjectBehaviour("W", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(Construct), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(engine->RegisterObjectBehaviour("W", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(Construct), asCALL_CDECL_OBJLAST), 0);
	asITypeInfo* v = engine->GetTypeInfoByName("V");
	ASSERT_NE(v, nullptr);
	asIScriptFunction* construct = nullptr;
	for (asUINT n = 0; n < v->GetBehaviourCount(); n++) {
		asEBehaviours beh;
		asIScriptFunction* f = v->GetBehaviourByIndex(n, &beh);
		if (beh == asBEHAVE_CONSTRUCT) {
			construct = f;
		}
	}
	ASSERT_NE(construct, nullptr);
	ASSERT_EQ(construct->SetLoops(true), asSUCCESS);

	ASSERT_TRUE(Build(
		"int local() { V v; return 0; }\n"
		"class HoldsV { V v; }\n"
		"int member() { HoldsV h; return 0; }\n"
		"int localW() { W w; return 0; }\n"
		"class HoldsW { W w; }\n"
		"int memberW() { HoldsW h; return 0; }\n")) << messages;
	EXPECT_TRUE(Fn("local")->GetTransitiveLoops());
	EXPECT_TRUE(Fn("member")->GetTransitiveLoops());
	EXPECT_FALSE(Fn("localW")->GetTransitiveLoops());
	EXPECT_FALSE(Fn("memberW")->GetTransitiveLoops());
}

// Kinds the analysis never visits must not claim "does not loop"
TEST_F(AsLoopsFact, UnanalysedKindsDefaultToLooping) {
	ASSERT_TRUE(Build("funcdef int CB();\n")) << messages;
	asITypeInfo* cb = mod->GetTypeInfoByName("CB");
	ASSERT_NE(cb, nullptr);
	asIScriptFunction* sig = cb->GetFuncdefSignature();
	ASSERT_NE(sig, nullptr);
	EXPECT_EQ(sig->GetFuncType(), asFUNC_FUNCDEF);
	EXPECT_TRUE(sig->GetLocalLoops());
	EXPECT_TRUE(sig->GetTransitiveLoops());
}

TEST_F(AsLoopsFact, CompileMethodProductHasTransitiveFacts) {
	ASSERT_TRUE(Build(
		"int helperThatLoops() { int s = 0; for (int i = 0; i < 4; ++i) { s += i; } return s; }\n"
		"int straight() { return 2; }\n"
		"funcdef int CB();\n"
		"CB@ cb;\n"
		"class Foo { int v = 5; }\n")) << messages;
	asITypeInfo* foo = mod->GetTypeInfoByName("Foo");
	ASSERT_NE(foo, nullptr);

	asIScriptFunction* looping = nullptr;
	ASSERT_EQ(mod->CompileMethod(foo, "gauge", "int get() const { return helperThatLoops(); }", 0, &looping), asSUCCESS) << messages;
	EXPECT_FALSE(looping->GetLocalLoops());
	EXPECT_TRUE(looping->GetTransitiveLoops());
	EXPECT_EQ(looping->GetLoopsVia(), Fn("helperThatLoops"));
	EXPECT_EQ(looping->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_FALSE(looping->GetTransitiveCallsDelegate());
	looping->Release();

	asIScriptFunction* plain = nullptr;
	ASSERT_EQ(mod->CompileMethod(foo, "gauge", "int get() const { return v + straight(); }", 0, &plain), asSUCCESS) << messages;
	EXPECT_FALSE(plain->GetTransitiveLoops());
	EXPECT_EQ(plain->GetLoopsVia(), nullptr);
	EXPECT_EQ(plain->GetTransitiveHalts(), asHALTS_YES);
	plain->Release();

	asIScriptFunction* delegating = nullptr;
	ASSERT_EQ(mod->CompileMethod(foo, "gauge", "int get() const { return cb(); }", 0, &delegating), asSUCCESS) << messages;
	EXPECT_TRUE(delegating->GetTransitiveCallsDelegate());
	EXPECT_NE(delegating->GetTransitiveHalts(), asHALTS_YES);
	delegating->Release();
}

TEST_F(AsLoopsFact, CompileFunctionProductHasTransitiveFacts) {
	ASSERT_TRUE(Build(kLoopingHelper)) << messages;
	asIScriptFunction* g = nullptr;
	ASSERT_EQ(mod->CompileFunction("gauge", "int g() { return f(); }", 0, 0, &g), asSUCCESS) << messages;
	EXPECT_FALSE(g->GetLocalLoops());
	EXPECT_TRUE(g->GetTransitiveLoops());
	EXPECT_EQ(g->GetLoopsVia(), Fn("f"));
	EXPECT_EQ(g->GetTransitiveHalts(), asHALTS_YES);
	g->Release();
}

TEST_F(AsLoopsFact, LoopsFactsSurviveSaveAndLoad) {
	ASSERT_TRUE(Build(std::string(kLoopingHelper) +
		"int g() { return f(); }\n"
		"int s() { return 1; }\n")) << messages;
	MemStream stream;
	ASSERT_GE(mod->SaveByteCode(&stream), 0);

	asIScriptModule* restored = engine->GetModule("restored", asGM_ALWAYS_CREATE);
	ASSERT_GE(restored->LoadByteCode(&stream), 0) << messages;

	asIScriptFunction* f = restored->GetFunctionByName("f");
	asIScriptFunction* g = restored->GetFunctionByName("g");
	asIScriptFunction* s = restored->GetFunctionByName("s");
	ASSERT_NE(f, nullptr);
	ASSERT_NE(g, nullptr);
	ASSERT_NE(s, nullptr);
	EXPECT_TRUE(f->GetLocalLoops());
	EXPECT_TRUE(f->GetTransitiveLoops());
	EXPECT_FALSE(g->GetLocalLoops());
	EXPECT_TRUE(g->GetTransitiveLoops());
	EXPECT_EQ(g->GetLoopsVia(), f);
	EXPECT_FALSE(s->GetLocalLoops());
	EXPECT_FALSE(s->GetTransitiveLoops());
}
