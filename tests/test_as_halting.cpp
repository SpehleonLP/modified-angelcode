// Tests for the AngelScript fork's halting analysis
// (GetLocalHalts / GetTransitiveHalts, asEHalts in angelscript.h).
// The analysis under test lives in the fork:
//   sdk/angelscript/source/as_compiler.cpp   FinalizeFunction  (local)
//   sdk/angelscript/source/as_module.cpp     ComputeTransitiveFunctionMetadata (transitive)
#include <gtest/gtest.h>
#include <angelscript.h>
#include <cstdio>

namespace {

class AsHalting : public ::testing::Test {
protected:
	asIScriptEngine* engine = nullptr;

	void SetUp() override {
		engine = asCreateScriptEngine();
		ASSERT_NE(engine, nullptr);
		// LoopPassingInductionVarByRefIsNotYes needs `int &inout` on a script
		// function, which the engine otherwise rejects for non-handle types
		// (see test_harness_access_mask.cpp for the same prerequisite).
		engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, true);
	}
	void TearDown() override {
		if (engine) engine->ShutDownAndRelease();
	}

	// Build `code` as a fresh module and return the named function.
	// The module owns the function; no Release needed on it.
	asIScriptFunction* Fn(const char* code, const char* name) {
		asIScriptModule* mod = engine->GetModule("halts", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", code);
		if (mod->Build() < 0) { ADD_FAILURE() << "build failed:\n" << code; return nullptr; }
		asIScriptFunction* f = mod->GetFunctionByName(name);
		if (!f) ADD_FAILURE() << "function not found: " << name;
		return f;
	}

	// True if the code builds as a fresh module.
	bool Builds(const char* code) {
		asIScriptModule* mod = engine->GetModule("halts", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", code);
		return mod->Build() >= 0;
	}

	// Dump finalized bytecode as op names — use when a classification test
	// fails to see what the optimizer actually emitted.
	static void Dump(asIScriptFunction* f) {
		asUINT len = 0;
		asDWORD* bc = f->GetByteCode(&len);
		for (asUINT n = 0; n < len; ) {
			asBYTE op = *(asBYTE*)&bc[n];
			int sz = asBCTypeSize[asBCInfo[op].type];
			fprintf(stderr, "  %4u: %s\n", n, asBCInfo[op].name);
			if (sz == 0) break;
			n += (asUINT)sz;
		}
	}
};

TEST_F(AsHalting, StraightLineIsYes) {
	asIScriptFunction* f = Fn("int f() { return 42; }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, BareInfiniteLoopIsNo) {
	asIScriptFunction* f = Fn("void f() { while(true) {} }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_NO);
}

TEST_F(AsHalting, SystemFunctionDefaultsToYes) {
	// Natives are the application's responsibility; the default vouches YES.
	struct L { static void f() {} };
	ASSERT_GE(engine->RegisterGlobalFunction("void sysf()", asFUNCTION(L::f), asCALL_CDECL), 0);
	asIScriptFunction* f = engine->GetGlobalFunctionByIndex(0);
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, CompileFunctionTransitiveDefaultsToUnknown) {
	// CompileFunction runs only the LOCAL analysis; the dormant transitive
	// side must rest at UNKNOWN, never a vacuous YES.
	asIScriptModule* mod = engine->GetModule("halts", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("s", "int dummy;");
	ASSERT_GE(mod->Build(), 0);
	asIScriptFunction* fn = nullptr;
	ASSERT_GE(mod->CompileFunction("halts", "void f_loop() { while(true) {} }", 0, 0, &fn), 0);
	EXPECT_EQ(fn->GetLocalHalts(), asHALTS_NO);          // local analysis is live
	EXPECT_EQ(fn->GetTransitiveHalts(), asHALTS_UNKNOWN); // dormant => conservative
	fn->Release();
}

TEST_F(AsHalting, OptimizedConditionalBackedgeIsNotYes) {
	// do-while over a bool: the optimizer can rewrite the backedge into
	// JLowNZ, which the old scan did not recognize as a jump at all —
	// yielding a false "provably halts". Never-YES is the invariant.
	asIScriptFunction* f = Fn("void f(bool b) { do {} while(b); }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() == asHALTS_YES) Dump(f);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, SwitchReturnInInfiniteLoopIsNotNo) {
	// JMPP dispatch can reach a RET, so this can halt: claiming NO is wrong.
	asIScriptFunction* f = Fn(
		"int f(int x) { while(true) { switch(x) {"
		" case 0: return 1; case 1: return 2; case 2: return 3; } x = 0; } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() == asHALTS_NO) Dump(f);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_NO);
}

TEST_F(AsHalting, ReturnInsideWhileTrueCompiles) {
	EXPECT_TRUE(Builds("int f() { while(true) { return 1; } }"));
}

TEST_F(AsHalting, WhileTrueWithBreakStillRequiresReturn) {
	EXPECT_FALSE(Builds("bool g() { return false; }\n"
	                    "int f() { while(true) { if (g()) break; return 1; } }"));
}

TEST_F(AsHalting, ReturnInsideDoWhileTrueCompiles) {
	EXPECT_TRUE(Builds("int f() { do { return 1; } while(true); }"));
}

TEST_F(AsHalting, DoWhileBodyAlwaysReturnsCompiles) {
	EXPECT_TRUE(Builds("int f() { do { return 1; } while(false); }"));
}

TEST_F(AsHalting, ReturnInsideForEverCompiles) {
	EXPECT_TRUE(Builds("int f() { for(;;) { return 1; } }"));
}

TEST_F(AsHalting, ForEverWithBreakStillRequiresReturn) {
	EXPECT_FALSE(Builds("bool g() { return false; }\n"
	                    "int f() { for(;;) { if (g()) break; return 1; } }"));
}

TEST_F(AsHalting, NonVoidPureInfiniteLoopCompilesAndIsNo) {
	asIScriptFunction* f = Fn("int f() { while(true) { } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_NO);
}

TEST_F(AsHalting, BranchyInfiniteLoopIsNo) {
	// Narrowing: conditionals INSIDE a loop with no reachable RET do not
	// forfeit NO (the old scan degraded this to UNKNOWN).
	asIScriptFunction* f = Fn(
		"void f(int x) { while(true) { if (x > 0) x--; else x++; } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_NO);
}

TEST_F(AsHalting, GuardBeforeInfiniteLoopIsNotNo) {
	// The early return IS reachable, so NO would be wrong; UNKNOWN expected.
	asIScriptFunction* f = Fn(
		"void f(bool b) { if (b) return; while(true) {} }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_NO);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, DelegateCallCapsLocalAtUnknown) {
	// The callee behind a funcdef handle is unknown at compile time; the
	// caller cannot promise YES from its own bytecode.
	asIScriptFunction* f = Fn(
		"funcdef void CB();\n"
		"void f(CB@ cb) { cb(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, TryCatchAroundLoopIsNotNo) {
	// Catch handlers are CFG edges this analysis does not model; a function
	// with try/catch must never be claimed NO.
	asIScriptFunction* f = Fn(
		"void g() {}\n"
		"void f() { while(true) { try { g(); } catch { return; } } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_NO);
}

TEST_F(AsHalting, RuntimeGuardLoopIsNotNo) {
	// Guard on a runtime variable: the exit edge is live, so the
	// constant-guard elision must NOT fire. NO and YES are both wrong.
	asIScriptFunction* f = Fn("void f(bool b) { while(b) {} }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() == asHALTS_NO) Dump(f);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_NO);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, ConditionalCallOfNonHaltingCalleeIsUnknownNotNo) {
	const char* src =
		"void loop() { while(true) {} }\n"
		"void caller(bool b) { if (b) loop(); }\n";
	asIScriptFunction* caller = Fn(src, "caller");
	ASSERT_NE(caller, nullptr);
	// caller halts when b is false — NO would be over-claimed.
	EXPECT_EQ(caller->GetTransitiveHalts(), asHALTS_UNKNOWN);

	// The callee's own verdict is untouched: it is locally NO.
	asIScriptFunction* loop = engine->GetModule("halts")->GetFunctionByName("loop");
	ASSERT_NE(loop, nullptr);
	EXPECT_EQ(loop->GetTransitiveHalts(), asHALTS_NO);
}

TEST_F(AsHalting, VirtualCallSeesLoopingOverride) {
	const char* src =
		"class Base { int v() { return 1; } }\n"
		"class Derived : Base { int v() { while(true) {} return 0; } }\n"
		"int call(Base@ b) { return b.v(); }\n";
	asIScriptFunction* f = Fn(src, "call");
	ASSERT_NE(f, nullptr);
	// Dispatch may land on Derived::v, which never halts: YES is unsound.
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, VirtualCallWithAllHaltingOverridesIsYes) {
	const char* src =
		"class Base { int v() { return 1; } }\n"
		"class Derived : Base { int v() { return 2; } }\n"
		"int call(Base@ b) { return b.v(); }\n";
	asIScriptFunction* f = Fn(src, "call");
	ASSERT_NE(f, nullptr);
	// Every possible target provably halts; precision must not regress.
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, InterfaceCallStillSeesImplementations) {
	// Guard the existing interface path against regression.
	const char* src =
		"interface I { int v(); }\n"
		"class A : I { int v() { while(true) {} return 0; } }\n"
		"int call(I@ i) { return i.v(); }\n";
	asIScriptFunction* f = Fn(src, "call");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

// --- Counted-loop prover ---------------------------------------------------

TEST_F(AsHalting, CountedForLoopIsYes) {
	asIScriptFunction* f = Fn(
		"int f(int n) { int s = 0; for (int i = 0; i < n; ++i) s += i; return s; }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, CountedForLoopLiteralBoundIsYes) {
	asIScriptFunction* f = Fn(
		"int f() { int s = 0; for (int i = 0; i < 10; i++) s += i; return s; }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, CountedUintLoopIsYes) {
	asIScriptFunction* f = Fn(
		"uint f(uint n) { uint s = 0; for (uint i = 0; i < n; ++i) s += i; return s; }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, NestedCountedLoopsAreYes) {
	asIScriptFunction* f = Fn(
		"int f(int n, int m) { int s = 0;"
		" for (int i = 0; i < n; ++i) for (int j = 0; j < m; ++j) s++;"
		" return s; }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

// Adversarial: each of these breaks one premise of the proof and must NOT be YES.

TEST_F(AsHalting, LoopWritingInductionVarInBodyIsNotYes) {
	asIScriptFunction* f = Fn(
		"void f(int n, bool b) { for (int i = 0; i < n; ++i) { if (b) i--; } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, LoopWritingBoundInBodyIsNotYes) {
	asIScriptFunction* f = Fn(
		"void f(int n) { for (int i = 0; i < n; ++i) { n++; } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, WhileWithContinueSkippingIncrementIsNotYes) {
	// The continue edge re-enters the condition WITHOUT passing the
	// increment: two back edges to one target, proof must refuse.
	asIScriptFunction* f = Fn(
		"void f(int n, bool b) { int i = 0;"
		" while (i < n) { if (b) continue; i++; } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, LoopPassingInductionVarByRefIsNotYes) {
	// &inout aliasing: the callee may write i. Address-taken => refuse.
	asIScriptFunction* f = Fn(
		"void touch(int &inout x) { x = 0; }\n"
		"void f(int n) { for (int i = 0; i < n; ++i) touch(i); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, LoopWithByValueCallStaysYes) {
	// Reading i (by value) must not spoil the proof.
	asIScriptFunction* f = Fn(
		"int g(int x) { return x + 1; }\n"
		"int f(int n) { int s = 0; for (int i = 0; i < n; ++i) s = g(i); return s; }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, CountedWhileLoopIsYes) {
	// Exercises the top-test JMP prover branch: the increment dominates
	// the back edge and the bound is invariant.
	asIScriptFunction* f = Fn(
		"void f(int n) { int i = 0; while (i < n) { i++; } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, WhileForwardBranchSkippingIncrementIsNotYes) {
	// Single back edge, sanctioned IncVi last in the body, top-test shape —
	// but a forward branch can skip the increment (b false forever -> the
	// loop never advances). Bounded must NOT be claimed: the increment does
	// not dominate the back edge.
	asIScriptFunction* f = Fn(
		"void f(int n, bool b) { int i = 0;"
		" while (i < n) { if (b) { i++; } } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() == asHALTS_YES) Dump(f);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, CountedDoWhileLoopIsYes) {
	// Plain bottom-test shape: increment on every in-loop path.
	asIScriptFunction* f = Fn(
		"void f(int n) { int i = 0; do { i++; } while (i < n); }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, DoWhileContinueSkippingIncrementIsNotYes) {
	// continue in a do-while lands FORWARD on the condition, past the
	// increment: single back edge, matching shape, passing span scan —
	// bounded must NOT be claimed (b true forever => i never advances).
	asIScriptFunction* f = Fn(
		"void f(int n, bool b) { int i = 0;"
		" do { if (b) continue; i++; } while (i < n); }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() == asHALTS_YES) Dump(f);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, SharedClassMethodCallIsNotTransitivelyYes) {
	// A shared class's overrides may live in other modules; BuildCalleeList
	// poisons the call site instead of resolving it. The poison must cap
	// the caller's transitive YES.
	asIScriptFunction* f = Fn(
		"shared class S { void m() {} }\n"
		"void f() { S s; s.m(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, VirtualCallSeesGrandchildOverride) {
	// DerivesFrom must be transitive: a looping override two levels down
	// still spoils the caller's YES.
	asIScriptFunction* f = Fn(
		"class B { void m() {} }\n"
		"class D : B {}\n"
		"class D2 : D { void m() { while (true) {} } }\n"
		"void f(B@ b) { b.m(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

} // namespace
