// Tests for the AngelScript fork's halting analysis
// (GetLocalHalts / GetTransitiveHalts, asEHalts in angelscript.h).
// The analysis under test lives in the fork:
//   sdk/angelscript/source/as_compiler.cpp   FinalizeFunction  (local)
//   sdk/angelscript/source/as_module.cpp     ComputeTransitiveFunctionMetadata (transitive)
#include <gtest/gtest.h>
#include <angelscript.h>
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>

// Internal header: gives ComputeTransitiveFunctionMetadataIsIdempotent below a
// way to re-invoke the analysis pass directly (no public API triggers it a
// second time on an already-built module). asCModule derives publicly from
// asIScriptModule with no other bases, so the static_cast is safe.
#include "as_module.h"

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
		if (pos + size > data.size()) return -1;
		memcpy(ptr, &data[pos], size);
		pos += size;
		return 0;
	}
};

static std::string g_asMessages;
static void CollectMessages(const asSMessageInfo *msg, void*) {
	g_asMessages += msg->message;
	g_asMessages += '\n';
}

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

	// Build `code` as a named module and return the named function.
	asIScriptFunction* FnIn(const char* moduleName, const char* code, const char* name) {
		asIScriptModule* mod = engine->GetModule(moduleName, asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", code);
		if (mod->Build() < 0) { ADD_FAILURE() << "build failed:\n" << code; return nullptr; }
		asIScriptFunction* f = mod->GetFunctionByName(name);
		if (!f) ADD_FAILURE() << "function not found: " << name;
		return f;
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

// Separate fixture, deliberately WITHOUT asEP_ALLOW_UNSAFE_REFERENCES.
//
// asGetConstFuncdefGlobalTarget's resolve (as_compiler.cpp, the global-read
// site) is only sound when unsafe references are disallowed: with them on, a
// read-only global funcdef handle can still be aliased by an F@ &inout (or
// bare F@ &) parameter and reassigned through that alias, so
// prop->type.IsReadOnly() stops guaranteeing a fixed value. AsHalting's
// engine (above) turns unsafe references ON for an unrelated by-ref-loop
// test, so the const-global-funcdef resolve tests need their own engine
// built the safe way, following Fn/FnIn's structure.
class AsHaltingConstGlobalFuncdef : public ::testing::Test {
protected:
	asIScriptEngine* engine = nullptr;

	void SetUp() override {
		engine = asCreateScriptEngine();
		ASSERT_NE(engine, nullptr);
	}
	void TearDown() override {
		if (engine) engine->ShutDownAndRelease();
	}

	asIScriptFunction* Fn(const char* code, const char* name) {
		asIScriptModule* mod = engine->GetModule("halts_cgf", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", code);
		if (mod->Build() < 0) { ADD_FAILURE() << "build failed:\n" << code; return nullptr; }
		asIScriptFunction* f = mod->GetFunctionByName(name);
		if (!f) ADD_FAILURE() << "function not found: " << name;
		return f;
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

TEST_F(AsHalting, DoWhileTrueWithBreakStillRequiresReturn) {
	EXPECT_FALSE(Builds("bool g() { return false; }\n"
	                    "int f() { do { if (g()) break; return 1; } while(true); }"));
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
	// S is a non-final shared class, so `s.m()` is a virtual call: it compiles
	// to asBC_CALLINTF (funcType asFUNC_VIRTUAL), not asBC_CALL. That op is
	// untouched by Task 6's direct-call resolution — Task 6 only folds direct
	// (asBC_CALL/asBC_ALLOC) targets to shared *script* functions, because
	// only those are frozen; virtual dispatch on a shared type can still be
	// overridden by an implementation this module never saw, so it must stay
	// poisoned. This test now pins that virtual-dispatch case specifically.
	// See SharedFinalClassMethodCallStillPoisonedViaVirtualDispatch below:
	// even a `final` class's methods go through this same CALLINTF path (an
	// external module holding only a forward declaration has no method body
	// to call directly — it can only reach the method through the shared
	// type's vftable), so shared class-method dispatch is untouched by
	// Task 6 regardless of finality; only free functions and constructors
	// (asBC_CALL/asBC_ALLOC) are in scope here.
	asIScriptFunction* f = Fn(
		"shared class S { void m() {} }\n"
		"void f() { S s; s.m(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, DirectCallToSharedFreeFunctionIsTransitivelyYes) {
	// DOES NOT DISCRIMINATE Task 6's CALL-branch fold (found in review): an
	// `external shared` declaration resolves through asCBuilder::
	// RegisterScriptFunction's existing-shared branch, which calls
	// module->AddScriptFunction(f) -> asCModule::AddScriptFunction, which
	// PushLast()s sh() straight into "user"'s own m_scriptFunctions
	// (as_module.cpp AddScriptFunction). So sh()'s id IS in "user"'s
	// funcIdToIndex map — f()'s call to sh() is a map HIT, resolved via the
	// pre-existing outCallees path, and never reaches the map-miss branch
	// this task added. This test passed before Task 6 too; it pins the
	// (still correct) behavior that a directly-imported shared function's
	// call resolves to YES, but proves nothing about the miss/fold path.
	// See DirectCallResolvesNestedSharedCallMapMiss below for a test that
	// does force a genuine map miss.
	ASSERT_NE(FnIn("owner", "shared int sh() { return 1; }", "sh"), nullptr);
	asIScriptFunction* f = FnIn("user",
		"external shared int sh();\n"
		"int f() { return sh(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, DirectCallToLoopingSharedFunctionIsNotYes) {
	// DOES NOT DISCRIMINATE, for the same reason as the test above (map HIT,
	// not miss — see its comment). It is ALSO not a real NO->UNKNOWN fold
	// exercise: `while (n > 0) { }` is the suite's own "unk" shape (see
	// ByteCodeRoundTripPreservesHaltsMetadata), not "no" (`while (true) {}`),
	// so loopy()'s own verdict is asHALTS_UNKNOWN, not asHALTS_NO — there is
	// no NO to downgrade here. This test only pins "a resolved call to a
	// non-YES callee does not spuriously become YES"; it was already non-YES
	// before Task 6 (poisoned) and stays non-YES after (now via a genuine
	// UNKNOWN fold) for an unrelated reason. See
	// ExternalFoldOfGenuineNoCalleeDowngradesToUnknown below for a test that
	// exercises the actual NO->UNKNOWN downgrade line in isolation.
	ASSERT_NE(FnIn("owner", "shared void loopy(int n) { while (n > 0) { } }", "loopy"), nullptr);
	asIScriptFunction* f = FnIn("user",
		"external shared void loopy(int n);\n"
		"void f() { loopy(3); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, DirectCallResolvesNestedSharedCallMapMiss) {
	// Genuinely discriminating positive test for the CALL-branch fold. Shape:
	// "owner" defines TWO shared functions, b() and a() { b(); }. "user"
	// only imports a() (via `external shared`), never b() — b()'s id is
	// nowhere in "user"'s funcIdToIndex map. But a()'s own bytecode (already
	// compiled, containing a CALL to b()) gets re-walked by BuildCalleeList
	// as part of *user*'s own ComputeTransitiveFunctionMetadata pass (a() is
	// in user's m_scriptFunctions too, via AddScriptFunction) — so the CALL
	// to b() is a real map miss in user's context, and only resolves to YES
	// if the CALL-branch fold added by Task 6 fires. Before Task 6 this
	// miss poisons a() to UNKNOWN (unresolved), which propagates to f() via
	// the ordinary internal call edge f()->a() — so f() would be UNKNOWN,
	// not YES, without the fix.
	ASSERT_NE(FnIn("owner",
		"shared void b() { }\n"
		"shared void a() { b(); }", "a"), nullptr);
	asIScriptFunction* f = FnIn("user",
		"external shared void a();\n"
		"void f() { a(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, ExternalFoldOfGenuineNoCalleeDowngradesToUnknown) {
	// Exercises the NO->UNKNOWN downgrade on the external-callee fold
	// (as_module.cpp phase 2, ~line 2093) in isolation from phase 4's
	// separate downgrade on ordinary *internal* call edges (~line 2185,
	// pre-existing, not part of Task 6). Shape, same map-miss mechanism as
	// DirectCallResolvesNestedSharedCallMapMiss above: "owner" defines
	// spin() { while(true){} } (genuinely asHALTS_NO — a bare infinite loop,
	// not the "unk" `while(n>0)` shape) and a() { spin(); }. "user" imports
	// only a(); spin() is a real map miss when a()'s bytecode is re-walked
	// in user's context, so the fold applies directly to a() in *user*'s
	// own pass.
	//
	// The assertion is on a() itself — not on some further caller like f()
	// — deliberately: if a() were folded to a raw (undowngraded) NO here,
	// then any caller of a() one hop further out (e.g. f()) would still
	// read back UNKNOWN anyway, because phase 4's *own* downgrade rule for
	// ordinary internal call edges (callee->transitiveHalts == NO ?
	// UNKNOWN : ...) fires again on the f()->a() edge — masking a removal
	// of the phase-2 downgrade under test. Asserting on a() directly is
	// the only way this test can go red if the phase-2 downgrade line is
	// deleted; confirmed by hand (see task-6-report.md).
	ASSERT_NE(FnIn("owner",
		"shared void spin() { while (true) { } }\n"
		"shared void a() { spin(); }", "a"), nullptr);
	asIScriptFunction* userF = FnIn("user",
		"external shared void a();\n"
		"void f() { a(); }", "f");
	ASSERT_NE(userF, nullptr);
	asIScriptModule* userMod = engine->GetModule("user", asGM_ONLY_IF_EXISTS);
	ASSERT_NE(userMod, nullptr);
	asIScriptFunction* userA = userMod->GetFunctionByName("a");
	ASSERT_NE(userA, nullptr);
	EXPECT_EQ(userA->GetTransitiveHalts(), asHALTS_UNKNOWN);
}

TEST_F(AsHalting, SharedFinalClassMethodCallStillPoisonedViaVirtualDispatch) {
	// DEVIATION FROM BRIEF (documented in task-6-report.md): the brief's
	// Step 1 proposed this test under the name
	// "DirectCallToSharedFinalClassMethodIsTransitivelyYes", expecting `final`
	// on the class to make c.m() compile to a direct asBC_CALL. Empirically
	// (confirmed by running this test against the Task 6 implementation) that
	// is not how this fork's builder works: EVERY class method — final class
	// or not — is unconditionally wrapped into the class's virtual function
	// table (as_builder.cpp, class-declaration pass, unguarded by any
	// final/NOINHERIT check), and PerformFunctionCall emits asBC_CALLINTF for
	// any funcType==asFUNC_VIRTUAL target. This is not an oversight: it is
	// how cross-module dispatch to a shared class's methods works at all —
	// "user" here only has an `external shared class C;` forward declaration
	// with no method body, so the vftable slot is the *only* way it can reach
	// C::m() at all. So `final` provides no direct-call path to fold; per the
	// brief's own scope line ("Modify: ...BuildCalleeList CALL and ALLOC
	// map-miss branches") and its virtual-dispatch-stays-poisoned rule, this
	// case is out of Task 6's scope. No later task in the plan (7-11)
	// addresses CALLINTF/shared-class-method resolution either, so this is
	// left poisoned; this test pins that as current, correct behavior rather
	// than asserting the brief's unachievable expectation.
	asIScriptModule* owner = engine->GetModule("owner", asGM_ALWAYS_CREATE);
	owner->AddScriptSection("s", "shared final class C { int m() { return 1; } }");
	ASSERT_GE(owner->Build(), 0);
	asIScriptFunction* f = FnIn("user",
		"external shared class C;\n"
		"int f() { C c; return c.m(); }", "f");
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

TEST_F(AsHalting, ByteCodeRoundTripPreservesHaltsMetadata) {
	asIScriptModule* mod = engine->GetModule("halts", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("s",
		"int yes() { return 1; }\n"
		"void unk(int n) { while (n > 0) { } }\n"
		"void no() { while (true) { } }\n");
	ASSERT_GE(mod->Build(), 0);

	MemStream stream;
	ASSERT_GE(mod->SaveByteCode(&stream), 0);

	asIScriptModule* mod2 = engine->GetModule("restored", asGM_ALWAYS_CREATE);
	ASSERT_GE(mod2->LoadByteCode(&stream), 0);

	const char* names[3] = { "yes", "unk", "no" };
	for (int i = 0; i < 3; i++) {
		asIScriptFunction* a = mod->GetFunctionByName(names[i]);
		asIScriptFunction* b = mod2->GetFunctionByName(names[i]);
		ASSERT_NE(a, nullptr) << names[i];
		ASSERT_NE(b, nullptr) << names[i];
		EXPECT_EQ(a->GetLocalHalts(),               b->GetLocalHalts())               << names[i];
		EXPECT_EQ(a->GetTransitiveHalts(),          b->GetTransitiveHalts())          << names[i];
		EXPECT_EQ(a->GetLocalCallsDelegate(),       b->GetLocalCallsDelegate())       << names[i];
		EXPECT_EQ(a->GetTransitiveCallsDelegate(),  b->GetTransitiveCallsDelegate())  << names[i];
	}
}

TEST_F(AsHalting, ComputeTransitiveFunctionMetadataIsIdempotent) {
	// Exercises every BuildCalleeList poison path in one module (unresolved
	// funcdef call, shared-class dispatch, plain call, looping callee) so
	// unresolved[] and transitiveCallsDelegate are non-trivial, then re-runs
	// the pass and asserts every function's four fields are bitwise
	// unchanged. This is the substrate guarantee Task 5 exists to establish:
	// the pass must be a pure, re-runnable function of current state.
	//
	// The module also carries a RESOLVED const-global funcdef call site
	// (viaConst -> cg -> target). That is the only shape whose verdict is
	// derived from per-function state the pass itself could consume
	// destructively (asCScriptFunction::funcdefCallTargets), so without it
	// this test is green for the wrong reason: a module with no resolved
	// site is trivially insensitive to what the pass does with that table.
	// The resolve only fires with unsafe references OFF (see
	// AsHaltingConstGlobalFuncdef's fixture comment), so turn the fixture's
	// engine-global flag off for this test before compiling anything.
	ASSERT_GE(engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, false), 0);

	asIScriptModule* mod = engine->GetModule("halts", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("s",
		"shared class S { void m() {} }\n"
		"funcdef void CB();\n"
		"void loop() { while (true) {} }\n"
		"void g() {}\n"
		"void f(CB@ cb, bool b) { S s; s.m(); g(); if (b) loop(); cb(); }\n"
		"funcdef int F();\n"
		"int target() { return 7; }\n"
		"F@ const cg = @target;\n"
		"int viaConst() { return cg(); }\n");
	ASSERT_GE(mod->Build(), 0);

	asCModule* cmod = static_cast<asCModule*>(mod);

	// Precondition: the resolved site really did resolve on the Build()-time
	// run, so the re-run below has something non-trivial to preserve.
	asIScriptFunction* viaConst = mod->GetFunctionByName("viaConst");
	ASSERT_NE(viaConst, nullptr);
	ASSERT_EQ(viaConst->GetTransitiveHalts(), asHALTS_YES);
	ASSERT_FALSE(viaConst->GetTransitiveCallsDelegate());

	asUINT count = mod->GetFunctionCount();
	ASSERT_GT(count, 0u);
	std::vector<asEHalts> localHalts(count), transitiveHalts(count);
	std::vector<bool> localDelegate(count), transitiveDelegate(count);
	for (asUINT i = 0; i < count; i++) {
		asIScriptFunction* fn = mod->GetFunctionByIndex(i);
		localHalts[i]         = fn->GetLocalHalts();
		transitiveHalts[i]    = fn->GetTransitiveHalts();
		localDelegate[i]      = fn->GetLocalCallsDelegate();
		transitiveDelegate[i] = fn->GetTransitiveCallsDelegate();
	}

	cmod->ComputeTransitiveFunctionMetadata();

	for (asUINT i = 0; i < count; i++) {
		asIScriptFunction* fn = mod->GetFunctionByIndex(i);
		EXPECT_EQ(fn->GetLocalHalts(),         localHalts[i])         << i;
		EXPECT_EQ(fn->GetTransitiveHalts(),    transitiveHalts[i])    << i;
		EXPECT_EQ(fn->GetLocalCallsDelegate(), localDelegate[i])      << i;
		EXPECT_EQ(fn->GetTransitiveCallsDelegate(), transitiveDelegate[i]) << i;
	}

	// Spelled out separately from the loop above so a regression names the
	// shape that broke rather than an opaque function index.
	EXPECT_EQ(viaConst->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_FALSE(viaConst->GetTransitiveCallsDelegate());
}

TEST_F(AsHalting, LocalCallsDelegateReflectsOwnBytecodeNotUnresolvedPoison) {
	// Task 5 contract shift: GetLocalCallsDelegate() must mean strictly "this
	// function's own bytecode contains a compile-time funcdef/delegate call
	// site" and no longer "something about this function was unresolvable".
	// h's only call site is a shared-class virtual dispatch, which
	// BuildCalleeList cannot resolve (poisons via outUnresolved) but which is
	// not a funcdef/delegate call at all. Pre-refactor code conflated the two
	// by setting localCallsDelegate = true for this exact poison; the
	// refactor must keep it false while still poisoning the transitive flag.
	asIScriptFunction* h = Fn(
		"shared class S { void m() {} }\n"
		"void h() { S s; s.m(); }", "h");
	ASSERT_NE(h, nullptr);
	EXPECT_FALSE(h->GetLocalCallsDelegate());
	EXPECT_TRUE(h->GetTransitiveCallsDelegate());
}

TEST_F(AsHalting, HeaderlessByteCodeRefusesToLoad) {
	engine->SetMessageCallback(asFUNCTION(CollectMessages), 0, asCALL_CDECL);
	g_asMessages.clear();

	// Pre-header streams start with the encoded debug-info flag (byte 0 or 1),
	// never 'A'. One zero byte is the minimal old-format prefix.
	MemStream stream;
	asBYTE oldPrefix[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	stream.Write(oldPrefix, 8);

	asIScriptModule* mod = engine->GetModule("stale", asGM_ALWAYS_CREATE);
	EXPECT_LT(mod->LoadByteCode(&stream), 0);
	EXPECT_NE(g_asMessages.find("format"), std::string::npos) << g_asMessages;
}

TEST_F(AsHalting, StepTwoLoopWithConstantBoundIsYes) {
	asIScriptFunction* f = Fn("void f() { for (int i = 0; i < 1000; i += 2) { } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, DecrementLoopToZeroIsYes) {
	asIScriptFunction* f = Fn("void f(int n) { for (int i = n; i > 0; --i) { } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, StepThreeDecrementToConstantBoundIsYes) {
	asIScriptFunction* f = Fn("void f() { for (int i = 900; i > 10; i -= 3) { } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, VariableStepLoopIsNotYes) {
	asIScriptFunction* f = Fn("void f(int k) { for (int i = 0; i < 100; i += k) { } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, WrongPolarityDecrementIsNotYes) {
	// i moves AWAY from the bound: never terminates for i < n at entry.
	asIScriptFunction* f = Fn("void f(int n) { for (int i = 0; i < n; i -= 1) { } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, TwoMutatorsIsNotYes) {
	asIScriptFunction* f = Fn("void f(bool b) { for (int i = 0; i < 100; ) { if (b) ++i; i += 2; } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, StepTwoAgainstVariableBoundIsNotYes) {
	// Wrap-around: with a variable bound, i += 2 can jump over the exit
	// window and cycle forever (n = INT_MAX, i even). Must refuse.
	asIScriptFunction* f = Fn("void f(int n) { for (int i = 0; i < n; i += 2) { } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, StepTwoAgainstIntMaxBoundIsNotYes) {
	// Exit window [0x7fffffff, INT_MAX] has width 1 < 2: even values skip
	// it and wrap. This loop genuinely never terminates.
	asIScriptFunction* f = Fn("void f() { for (int i = 0; i < 0x7fffffff; i += 2) { } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

// --- Top-test coverage -------------------------------------------------
// Every prior counted-loop test is a `for` loop, which the compiler always
// rotates into the bottom-test shape (ADDIi/DecVi;CMPIi;JS|JP-back). The
// entire top-test half of asProveCountedLoop (CMP;JNS|JNP-exit;...;step;
// JMP-back) — including the JNP acceptance branch, both condOp polarity
// guards, and the top-test window call reading asBC_INTARG(&bc[target]) —
// is unreached by that suite. `while` loops keep the top-test shape and
// exercise it. Each test's Dump() (see the fix report) confirmed the
// anticipated CMP*;JNS|JNP;...;step;JMP shape before these were trusted.

TEST_F(AsHalting, WhileLoopStepTwoConstantBoundIsYes) {
	// Top-test, JNS exit (stay while i < 1000), ADDIi step +2, ample headroom.
	asIScriptFunction* f = Fn("void f() { int i = 0; while (i < 1000) { i += 2; } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, WhileLoopStepTwoIntMaxBoundIsNotYes) {
	// Top-test twin of StepTwoAgainstIntMaxBoundIsNotYes: exit window at
	// INT_MAX is width 1 < 2, no headroom -> must NOT be YES.
	asIScriptFunction* f = Fn("void f() { int i = 0; while (i < 0x7fffffff) { i += 2; } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, WhileLoopStepTwoVariableBoundIsNotYes) {
	// Top-test twin of StepTwoAgainstVariableBoundIsNotYes: variable bound,
	// window position unknown at compile time -> must NOT be YES.
	asIScriptFunction* f = Fn("void f(int n) { int i = 0; while (i < n) { i += 2; } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, WhileLoopDecrementJNPPathIsYes) {
	// Top-test, JNP exit (stay while i > 0), SUBIi step -3: the only test in
	// the suite that reaches the condOp == asBC_JNP acceptance branch.
	asIScriptFunction* f = Fn("void f() { int i = 1000; while (i > 0) { i -= 3; } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

// --- Decrement-side window guard (adversarial) --------------------------
// The only pre-existing decrement-with-constant-bound test (StepThree
// DecrementToConstantBoundIsYes) uses imm = 10, nowhere near INT_MIN, so it
// cannot discriminate a broken decrement headroom check. These pin both the
// boundIsVar guard and the headroom arithmetic on the decrement side.

TEST_F(AsHalting, DecrementLoopVariableBoundIsNotYes) {
	// Bottom-test, JP back-edge, SUBIi step -2 against a VARIABLE bound:
	// window position unknown at compile time -> must NOT be YES.
	asIScriptFunction* f = Fn("void f(int n) { for (int i = 100; i > n; i -= 2) { } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, DecrementStepAgainstIntMinBoundIsNotYes) {
	// Top-test, JNP exit, SUBIi step -3 against a constant bound one step
	// short of INT_MIN: exit window [INT_MIN, -2147483647] has width 1 < 3,
	// narrower than the step, so the rule refuses — must NOT be YES. This
	// particular loop actually DOES terminate (from i = 0, gcd(3, 2^32) == 1
	// means the -3 orbit covers every residue, so it lands on INT_MIN after
	// exactly 2^31 iterations); the refusal is conservative here, not tight
	// — the prover reasons only about window width and has no way to know
	// the step is coprime with the wrap modulus. A genuinely divergent
	// decrement counterexample needs the bound AT INT_MIN itself, which a
	// plain int32 literal cannot express (see the JNP polarity-guard test
	// below, which reaches that bound via `-2147483647 - 1`). The bound here
	// is written as -2147483647 (INT_MIN+1), not -2147483648, so the literal
	// stays within int32 range and the compiler emits CMPIi (32-bit signed),
	// not CMPi64 — confirmed via Dump(), see the fix report.
	asIScriptFunction* f = Fn("void f() { int i = 0; while (i > -2147483647) { i -= 3; } }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

// --- Accepting side of the window boundary (adversarial) -----------------
// StepTwoAgainstIntMaxBoundIsNotYes pins the refusing side of the boundary
// (bound == 0x7fffffff, no headroom). Nothing pinned the accepting side, so
// a too-strict regression (pure precision loss, never a soundness bug)
// would be invisible. This is the tight positive twin.

TEST_F(AsHalting, StepTwoLoopAtMaxHeadroomBoundIsYes) {
	// Exit window [2147483646, INT_MAX] is exactly width 2 == mag: just
	// enough headroom -> must be YES.
	asIScriptFunction* f = Fn("void f() { for (int i = 0; i < 2147483646; i += 2) { } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() != asHALTS_YES) Dump(f);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);
}

// --- Top-test polarity guards (adversarial) ------------------------------
// The window check (asCountedLoopWindowOk) does not by itself refuse an
// INT_MIN-adjacent bound paired with a POSITIVE step: a positive step takes
// the `imm <= INT_MAX-(mag-1)` branch, which INT_MIN trivially satisfies
// (INT_MIN is nowhere near INT_MAX). Only the explicit polarity guards
// `if (condOp == asBC_JNS && step < 0) return false;` and
// `if (condOp == asBC_JNP && step > 0) return false;` catch the mismatched
// pairing before the window check ever runs. These two tests each force one
// guard's condOp and a step of the WRONG sign for it, landing on a
// wrap-around bound the window check alone would wave through — pinning
// that each guard line, not just the window arithmetic, is load-bearing.
// Confirmed via Dump() (see the fix report): both reach the top-test branch
// with the expected polarity — CMPIi;JNP;...;ADDIi;JMP for the first,
// CMPi;JNS;...;SUBIi;JMP for the second.

TEST_F(AsHalting, WrongPolarityIncrementAgainstIntMinBoundIsNotYes) {
	// Top-test, JNP exit (stay while i > w), step +2: the "wrong" pairing for
	// JNP (which wants a decrement). w = -2147483647-1 == INT_MIN. The
	// window check does NOT catch this case on its own: a positive step
	// takes the `imm <= INT_MAX-(mag-1)` branch, and INT_MIN trivially
	// satisfies it, so only the explicit JNP-guard refuses. From an odd
	// start (i = 1) the orbit under +2 is the odd residues, which never
	// lands in the width-1 window at INT_MIN, so this genuinely never
	// terminates — the guard is the only thing standing between this input
	// and a false YES.
	asIScriptFunction* f = Fn(
		"void f() { int i = 1; while (i > -2147483647 - 1) { i += 2; } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() == asHALTS_YES) Dump(f);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, WrongPolarityDecrementAgainstVariableBoundIsNotYes) {
	// Top-test, JNS exit (stay while i < n), step -1: the "wrong" pairing for
	// JNS (which wants an increment). Bound is a variable, so this also pins
	// the guard independent of any constant-bound window arithmetic — for
	// any n > i's start, i moving away from n never reaches or passes it.
	asIScriptFunction* f = Fn(
		"void f(int n) { int i = 0; while (i < n) { i -= 1; } }", "f");
	ASSERT_NE(f, nullptr);
	if (f->GetLocalHalts() == asHALTS_YES) Dump(f);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHalting, IifeDoesNotCompile) {
	// AngelScript function literals are untyped until converted to a target
	// funcdef; an IIFE has no conversion target. Pins the language rule the
	// spec's item-5 scoping rests on (spec: IIFE sub-item expected vacuous).
	EXPECT_FALSE(Builds("int f() { return (function() { return 1; })(); }"));
}

// --- Const-global funcdef resolve: positive + guard tests -----------------
// All under AsHaltingConstGlobalFuncdef (unsafe references OFF — see the
// fixture's comment). Running these under AsHalting (unsafe refs ON) would
// make the positive test wrongly fail closed, since the compiler now refuses
// to trust prop->type.IsReadOnly() under that engine property.

TEST_F(AsHaltingConstGlobalFuncdef, ConstGlobalFuncdefLiteralTargetIsTransitivelyYes) {
	// NOTE (deviation from brief text): AngelScript's const-position grammar
	// is C-pointer-like — `const F@ g` makes the *pointee* const (handle
	// itself stays reassignable; verified empirically: `@g = @other;`
	// compiles against `const F@ g`). Only trailing `F@ const g` makes the
	// HANDLE itself immutable, which is what asCDataType::IsReadOnly()
	// reports for object-handle types (isConstHandle) and what the
	// soundness of asGetConstFuncdefGlobalTarget depends on. Tests use the
	// trailing-const spelling throughout.
	asIScriptFunction* f = Fn(
		"funcdef int F();\n"
		"int target() { return 7; }\n"
		"F@ const g = @target;\n"
		"int f() { return g(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_YES);          // known site no longer caps local
	EXPECT_FALSE(f->GetLocalCallsDelegate());
}

TEST_F(AsHaltingConstGlobalFuncdef, NonConstGlobalFuncdefIsNotYes) {
	asIScriptFunction* f = Fn(
		"funcdef int F();\n"
		"int target() { return 7; }\n"
		"F@ g = @target;\n"
		"int f() { return g(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHaltingConstGlobalFuncdef, ConstGlobalFuncdefRuntimeInitIsNotYes) {
	asIScriptFunction* f = Fn(
		"funcdef int F();\n"
		"int target() { return 7; }\n"
		"F@ pick() { return @target; }\n"
		"F@ const g = pick();\n"
		"int f() { return g(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHaltingConstGlobalFuncdef, ConstGlobalFuncdefLoopingTargetIsNotYes) {
	asIScriptFunction* f = Fn(
		"funcdef void F(int);\n"
		"void loopy(int n) { while (n > 0) { } }\n"
		"F@ const g = @loopy;\n"
		"void f() { g(3); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHaltingConstGlobalFuncdef, PointeeConstGlobalFuncdefIsNotYes) {
	// `const F@ g` (leading const) only const-qualifies the pointee, not the
	// handle — asCDataType::IsReadOnly() reports false for it, so the
	// resolve must never fire here. Pins the exact footgun the deviation
	// note above documents: swap to trailing `F@ const g` and this becomes
	// the positive test.
	asIScriptFunction* f = Fn(
		"funcdef int F();\n"
		"int target() { return 7; }\n"
		"const F@ g = @target;\n"
		"int f() { return g(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHaltingConstGlobalFuncdef, TernaryInitializerConstGlobalFuncdefIsNotYes) {
	// Two asBC_FuncPtr pushes in the init bytecode (one per ternary arm):
	// asGetConstFuncdefGlobalTarget's "exactly one FuncPtr" guard must
	// refuse this, even though only one of the two ever actually executes.
	// Deliberately puts the looping arm FIRST (true branch) and the
	// non-halting arm LAST (false branch): bytecode for a ternary emits the
	// true-branch FuncPtr before the false-branch one, so a "last FuncPtr
	// wins" mistake here would resolve to the non-looping arm and produce a
	// demonstrable false YES — this ordering is what makes that mistake
	// observable instead of accidentally still-correct.
	asIScriptFunction* f = Fn(
		"funcdef int F();\n"
		"int a() { return 1; }\n"
		"int loopy() { while (true) {} return 0; }\n"
		"bool c = true;\n"
		"F@ const g = c ? @loopy : @a;\n"
		"int f() { return g(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHaltingConstGlobalFuncdef, DelegateInitializerConstGlobalFuncdefIsNotYes) {
	// The highest-value guard gap named in review: a delegate initializer's
	// bytecode is FuncPtr(bestMethod) followed by CALLSYS (the delegate
	// constructor call). A resolve here would bind straight to Base::m and
	// discard which object o.m was bound to. o's static type is Base
	// (non-looping m); its runtime type is Derived, whose override loops —
	// so a wrong resolve here would be a demonstrable lie: g() dispatches
	// virtually to Derived::m() and hangs, while a wrongly-resolved analysis
	// would have called that YES.
	//
	// EVIDENCE NOTE: mutating out the CALLSYS entry in
	// asGetConstFuncdefGlobalTarget's block-list does NOT turn this test
	// red. Traced why: bestMethod's captured asCScriptFunction has
	// funcType==asFUNC_VIRTUAL (every class method is vtable-wrapped, see
	// SharedFinalClassMethodCallStillPoisonedViaVirtualDispatch above), and
	// BuildCalleeList's own CallPtr handling only trusts a resolved target
	// that is either an exact id match in this module's funcIdToIndex (which
	// virtual methods are not entered into with a callable identity BuildCalleeList
	// treats as a plain call target) or asFUNC_SYSTEM/shared-asFUNC_SCRIPT;
	// asFUNC_VIRTUAL matches neither, so it falls into outUnresolved = true
	// independent of the CALLSYS check. So for this specific shape, a second
	// layer (BuildCalleeList's funcType classification) is redundant defense
	// that happens to also refuse it. This test still pins real, correct,
	// security-relevant behavior — it just isn't sufficient to independently
	// discriminate the CALLSYS line by itself; see
	// CallInInitializerRefusesResolveEvenWithASingleFuncPtrArg below for a
	// shape that does.
	asIScriptFunction* f = Fn(
		"funcdef void F();\n"
		"class Base { void m() {} }\n"
		"class Derived : Base { void m() { while (true) {} } }\n"
		"Base@ o = Derived();\n"
		"F@ const g = F(o.m);\n"
		"void f() { g(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHaltingConstGlobalFuncdef, CallInInitializerRefusesResolveEvenWithASingleFuncPtrArg) {
	// Isolates the CALLSYS half of asGetConstFuncdefGlobalTarget's "no calls
	// in the init bytecode" guard from the delegate test above (which is
	// independently caught by a different mechanism — see that test's
	// EVIDENCE NOTE). A registered native taking and returning F@ produces
	// exactly the shape the guard must refuse on its own: one asBC_FuncPtr
	// (the argument, @a) followed by asBC_CALLSYS. The guard cannot know
	// what the native does with its argument — it need not return it
	// unchanged — so trusting "exactly one FuncPtr, ignore any call" would
	// be unsound in general, independent of whether asFUNC_SYSTEM happens to
	// also be excluded elsewhere. g()/f() are never executed (this test only
	// inspects static metadata), so the native's body is an unreachable stub.
	struct L {
		static asIScriptFunction *Passthrough(asIScriptFunction *h) { return h; }
	};
	ASSERT_GE(engine->RegisterFuncdef("void F()"), 0);
	ASSERT_GE(engine->RegisterGlobalFunction(
		"F@ passthroughIgnoringArg(F@ h)", asFUNCTION(L::Passthrough), asCALL_CDECL), 0);

	asIScriptFunction* f = Fn(
		"void a() {}\n"
		"void loopy() { while (true) {} }\n"
		"F@ const g = passthroughIgnoringArg(@a);\n"
		"void f() { g(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHaltingConstGlobalFuncdef, ChainedCallPtrSecondSiteStaysUnknown) {
	// Regression pin for the `ctx->knownFuncdefTarget = 0;` reset in
	// PerformFunctionCall (as_compiler.cpp, the asFUNC_FUNCDEF branch): two
	// CallPtr sites on one statement, `g()()`. Only the FIRST is knowable
	// (g resolves to mk); the second calls whatever mk() returns at
	// runtime, which is never provably fixed. Without the reset, the first
	// site's resolved target would leak into the second site's knownFuncdefTarget
	// (CompileExpressionPostOp's ttOpenParenthesis case never overwrites ctx
	// from a fresh funcExpr the way CompileFunctionCall does), folding f to
	// a false YES while it actually calls the looping loopy().
	asIScriptFunction* f = Fn(
		"funcdef int F();\n"
		"funcdef F@ MK();\n"
		"int loopy() { while (true) {} return 0; }\n"
		"F@ mk() { return @loopy; }\n"
		"MK@ const g = @mk;\n"
		"int f() { return g()(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

// --- Critical: unsafe-reference aliasing must not forge a YES -------------
// Under AsHalting (unsafe references ON, matching this harness's default and
// asEP_ALLOW_UNSAFE_REFERENCES generally): a read-only global funcdef handle
// can be aliased by an F@ &inout parameter and reassigned through the alias.
// Before the engine->ep.allowUnsafeReferences guard at the global-read site,
// this reproduced a Critical false YES (f() folded to YES, then actually
// called b() — or in the real attack, a non-halting function — after poke()
// reassigned g through the alias). The guard must keep this at UNKNOWN.
TEST_F(AsHalting, UnsafeReferenceAliasCannotForgeConstGlobalFuncdefYes) {
	asIScriptFunction* f = Fn(
		"funcdef int F();\n"
		"int a() { return 1; }\n"
		"int b() { return 2; }\n"
		"F@ const g = @a;\n"
		"void sink(F@ &inout x) { @x = @b; }\n"
		"void poke() { sink(g); }\n"
		"int f() { return g(); }", "f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_NE(f->GetLocalHalts(), asHALTS_YES);
}

TEST_F(AsHaltingConstGlobalFuncdef, ConsumptionTimeUnsafeReferencesGuardPoisonsResolvedSite) {
	// asEP_ALLOW_UNSAFE_REFERENCES is engine-global and settable at any time,
	// so the stamp-site guard (compile time) cannot be the only one: a module
	// built with unsafe references off carries stamped per-site funcdef
	// targets that are no longer trustworthy once the property is flipped on.
	// BuildCalleeList therefore re-checks the property when it CONSUMES the
	// stamped table, and fails the site closed.
	//
	// Scope note: with the pass running once per Build(), flipping the
	// property does not by itself change any already-published verdict (see
	// the README soundness contract). This test drives the pass directly to
	// pin the consumption-time behaviour, which is what a later re-run of the
	// pass (module bind/unbind) will depend on.
	asIScriptModule* mod = engine->GetModule("halts_cgf", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("s",
		"funcdef int F();\n"
		"int target() { return 7; }\n"
		"F@ const g = @target;\n"
		"int f() { return g(); }\n");
	ASSERT_GE(mod->Build(), 0);

	asIScriptFunction* f = mod->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);
	ASSERT_EQ(f->GetTransitiveHalts(), asHALTS_YES);
	ASSERT_FALSE(f->GetTransitiveCallsDelegate());

	ASSERT_GE(engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, true), 0);
	static_cast<asCModule*>(mod)->ComputeTransitiveFunctionMetadata();

	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_TRUE(f->GetTransitiveCallsDelegate());
}

// --- Imported (CALLBND) refinement on bind/unbind -------------------------

// Prints a verdict so a passing run still shows the observed integer; the
// hazard analysis for the import-cycle tests below is stated in terms of
// concrete asEHalts values, not prose.
static void ShowHalts(const char* label, asIScriptFunction* fn) {
	fprintf(stderr, "  [halts] %s = %d\n", label,
	        fn ? (int)fn->GetTransitiveHalts() : -1);
}

TEST_F(AsHalting, ImportBindRefinesAndRebindDowngrades) {
	asIScriptModule* provider = engine->GetModule("provider", asGM_ALWAYS_CREATE);
	provider->AddScriptSection("s",
		"int good() { return 1; }\n"
		"int bad() { while (true) { } return 0; }\n");
	ASSERT_GE(provider->Build(), 0);

	asIScriptModule* importer = engine->GetModule("importer", asGM_ALWAYS_CREATE);
	importer->AddScriptSection("s",
		"import int ext() from \"provider\";\n"
		"int f() { return ext(); }\n");
	ASSERT_GE(importer->Build(), 0);

	asIScriptFunction* f = importer->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);

	// Unbound: the import site is unresolvable.
	ShowHalts("unbound f", f);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);

	// Bound to a halting target: refines to YES.
	ASSERT_GE(importer->BindImportedFunction(0, provider->GetFunctionByName("good")), 0);
	ShowHalts("bound-to-good f", f);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);

	// Unbound again: reverts.
	ASSERT_GE(importer->UnbindImportedFunction(0), 0);
	ShowHalts("re-unbound f", f);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);

	// Rebound to a looper: must NOT stay YES.
	ASSERT_GE(importer->BindImportedFunction(0, provider->GetFunctionByName("bad")), 0);
	ShowHalts("bound-to-bad f", f);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

TEST_F(AsHalting, BindAllImportedFunctionsRefines) {
	asIScriptModule* provider = engine->GetModule("ba_provider", asGM_ALWAYS_CREATE);
	provider->AddScriptSection("s", "int good() { return 1; }\n");
	ASSERT_GE(provider->Build(), 0);

	asIScriptModule* importer = engine->GetModule("ba_importer", asGM_ALWAYS_CREATE);
	importer->AddScriptSection("s",
		"import int good() from \"ba_provider\";\n"
		"int f() { return good(); }\n");
	ASSERT_GE(importer->Build(), 0);

	asIScriptFunction* f = importer->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);

	ASSERT_GE(importer->BindAllImportedFunctions(), 0);
	ShowHalts("BindAll f", f);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);

	ASSERT_GE(importer->UnbindAllImportedFunctions(), 0);
	ShowHalts("UnbindAll f", f);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

// Hazard (2), two-module ring: A::f -> B::g -> A::f is mutual infinite
// recursion. Neither may ever report YES, in either bind order.
TEST_F(AsHalting, TwoModuleImportCycleNeverYieldsYes) {
	asIScriptModule* A = engine->GetModule("cycA", asGM_ALWAYS_CREATE);
	A->AddScriptSection("s",
		"import int g() from \"cycB\";\n"
		"int f() { return g(); }\n");
	ASSERT_GE(A->Build(), 0);

	asIScriptModule* B = engine->GetModule("cycB", asGM_ALWAYS_CREATE);
	B->AddScriptSection("s",
		"import int f() from \"cycA\";\n"
		"int g() { return f(); }\n");
	ASSERT_GE(B->Build(), 0);

	asIScriptFunction* f = A->GetFunctionByName("f");
	asIScriptFunction* g = B->GetFunctionByName("g");
	ASSERT_NE(f, nullptr);
	ASSERT_NE(g, nullptr);

	ASSERT_GE(A->BindImportedFunction(0, g), 0);
	ASSERT_GE(B->BindImportedFunction(0, f), 0);
	ShowHalts("A->B->A order1 f", f);
	ShowHalts("A->B->A order1 g", g);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_NE(g->GetTransitiveHalts(), asHALTS_YES);

	// Re-drive the binds (both orders, repeatedly): a fold that reads the
	// peer's already-published verdict could otherwise settle on a mutual YES.
	for (int i = 0; i < 3; i++) {
		ASSERT_GE(B->BindImportedFunction(0, f), 0);
		ASSERT_GE(A->BindImportedFunction(0, g), 0);
	}
	ShowHalts("A->B->A settled f", f);
	ShowHalts("A->B->A settled g", g);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_NE(g->GetTransitiveHalts(), asHALTS_YES);
}

// Hazard (2), three-module ring A -> B -> C -> A.
TEST_F(AsHalting, ThreeModuleImportCycleNeverYieldsYes) {
	asIScriptModule* A = engine->GetModule("ringA", asGM_ALWAYS_CREATE);
	A->AddScriptSection("s",
		"import int b() from \"ringB\";\n"
		"int a() { return b(); }\n");
	ASSERT_GE(A->Build(), 0);
	asIScriptModule* B = engine->GetModule("ringB", asGM_ALWAYS_CREATE);
	B->AddScriptSection("s",
		"import int c() from \"ringC\";\n"
		"int b() { return c(); }\n");
	ASSERT_GE(B->Build(), 0);
	asIScriptModule* C = engine->GetModule("ringC", asGM_ALWAYS_CREATE);
	C->AddScriptSection("s",
		"import int a() from \"ringA\";\n"
		"int c() { return a(); }\n");
	ASSERT_GE(C->Build(), 0);

	asIScriptFunction* a = A->GetFunctionByName("a");
	asIScriptFunction* b = B->GetFunctionByName("b");
	asIScriptFunction* c = C->GetFunctionByName("c");
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	ASSERT_NE(c, nullptr);

	for (int i = 0; i < 3; i++) {
		ASSERT_GE(A->BindImportedFunction(0, b), 0);
		ASSERT_GE(B->BindImportedFunction(0, c), 0);
		ASSERT_GE(C->BindImportedFunction(0, a), 0);
	}
	ShowHalts("ring a", a);
	ShowHalts("ring b", b);
	ShowHalts("ring c", c);
	EXPECT_NE(a->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_NE(b->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_NE(c->GetTransitiveHalts(), asHALTS_YES);
}

// Hazard (2), the attack the plan's rule (guarded only by
// `target->module != this`) actually loses to.
//
//   prov: int good() { return 1; }
//   mid : import int h() from "prov";  int g() { return h(); }
//   top : import int g() from "mid";   int f() { return g(); }
//
// 1. bind mid.h -> prov.good        => mid::g is genuinely YES
// 2. bind top.g -> mid.g            => plan's rule folds YES into top::f
// 3. rebind mid.h -> top.f          => f -> g -> f is now mutual infinite
//    recursion, but step 2's YES was published into top::f and nothing
//    recomputes top, so mid's recompute folds a stale YES and BOTH settle
//    at YES. That is a false YES.
//
// The shipped rule refuses to fold a bound target whose own module has any
// bound import, which stops the chain at step 2 (top::f caps at UNKNOWN).
TEST_F(AsHalting, RebindingAProviderIntoACycleNeverYieldsYes) {
	asIScriptModule* prov = engine->GetModule("atkProv", asGM_ALWAYS_CREATE);
	prov->AddScriptSection("s", "int good() { return 1; }\n");
	ASSERT_GE(prov->Build(), 0);

	asIScriptModule* mid = engine->GetModule("atkMid", asGM_ALWAYS_CREATE);
	mid->AddScriptSection("s",
		"import int h() from \"atkProv\";\n"
		"int g() { return h(); }\n");
	ASSERT_GE(mid->Build(), 0);

	asIScriptModule* top = engine->GetModule("atkTop", asGM_ALWAYS_CREATE);
	top->AddScriptSection("s",
		"import int g() from \"atkMid\";\n"
		"int f() { return g(); }\n");
	ASSERT_GE(top->Build(), 0);

	asIScriptFunction* good = prov->GetFunctionByName("good");
	asIScriptFunction* g = mid->GetFunctionByName("g");
	asIScriptFunction* f = top->GetFunctionByName("f");
	ASSERT_NE(good, nullptr);
	ASSERT_NE(g, nullptr);
	ASSERT_NE(f, nullptr);

	ASSERT_GE(mid->BindImportedFunction(0, good), 0);
	ShowHalts("attack step1 g", g);
	EXPECT_EQ(g->GetTransitiveHalts(), asHALTS_YES);

	ASSERT_GE(top->BindImportedFunction(0, g), 0);
	ShowHalts("attack step2 f", f);

	// Close the cycle by repointing mid's import at top::f.
	ASSERT_GE(mid->BindImportedFunction(0, f), 0);
	ShowHalts("attack step3 f", f);
	ShowHalts("attack step3 g", g);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_NE(g->GetTransitiveHalts(), asHALTS_YES);
}

// Hazard (2) precision pin: an importer whose provider module itself has a
// bound import does NOT refine. This is the precision the sound rule costs;
// it is pinned so a future change that reintroduces the fold has to face the
// cycle test above.
TEST_F(AsHalting, ProviderWithABoundImportIsNotFolded) {
	asIScriptModule* prov = engine->GetModule("chainProv", asGM_ALWAYS_CREATE);
	prov->AddScriptSection("s", "int good() { return 1; }\n");
	ASSERT_GE(prov->Build(), 0);

	asIScriptModule* mid = engine->GetModule("chainMid", asGM_ALWAYS_CREATE);
	mid->AddScriptSection("s",
		"import int h() from \"chainProv\";\n"
		"int g() { return h(); }\n");
	ASSERT_GE(mid->Build(), 0);

	asIScriptModule* top = engine->GetModule("chainTop", asGM_ALWAYS_CREATE);
	top->AddScriptSection("s",
		"import int g() from \"chainMid\";\n"
		"int f() { return g(); }\n");
	ASSERT_GE(top->Build(), 0);

	ASSERT_GE(mid->BindImportedFunction(0, prov->GetFunctionByName("good")), 0);
	ASSERT_GE(top->BindImportedFunction(0, mid->GetFunctionByName("g")), 0);

	asIScriptFunction* f = top->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);
	ShowHalts("chain top f", f);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_UNKNOWN);
}

// Teardown smoke test, NOT a pin on the InternalReset routing.
//
// InternalReset deliberately unbinds through the non-recomputing
// UnbindAllImportedFunctionsInternal, but the reason is that a recompute
// during teardown is wasted work, not that it would be unsafe:
// asCGlobalProperty::DestroyInternal releases and NULLs initFunc rather than
// leaving it dangling, and the pass skips a null initFunc, so routing the
// teardown through the recomputing wrapper instead is measurably still
// crash-free. Nor could a test observe the difference: nothing survives the
// discard whose verdict a teardown recompute could change. So this test pins
// only what its body actually exercises - that discarding an importer with a
// bound import, live globals and a stamped const-global funcdef is clean.
TEST_F(AsHalting, DiscardingAModuleWithBoundImportsIsClean) {
	asIScriptModule* provider = engine->GetModule("tdProvider", asGM_ALWAYS_CREATE);
	provider->AddScriptSection("s", "int good() { return 1; }\n");
	ASSERT_GE(provider->Build(), 0);

	asIScriptModule* importer = engine->GetModule("tdImporter", asGM_ALWAYS_CREATE);
	importer->AddScriptSection("s",
		"funcdef int F();\n"
		"int local() { return 2; }\n"
		"F@ const gp = @local;\n"
		"int gv = local();\n"
		"import int good() from \"tdProvider\";\n"
		"int f() { return good() + gv; }\n");
	ASSERT_GE(importer->Build(), 0);
	ASSERT_GE(importer->BindAllImportedFunctions(), 0);

	// Must not crash / read destroyed globals.
	EXPECT_GE(engine->DiscardModule("tdImporter"), 0);
	EXPECT_GE(engine->DiscardModule("tdProvider"), 0);
}

// Hazard (4): funcdefCallTargets holds raw, un-refcounted pointers and is no
// longer cleared by the pass, so a bind/unbind re-run reads it long after
// Build(). Every stampable target is itself held by this module (its own
// m_scriptFunctions entry, which for a shared function survives the
// originating module's discard via FindNewOwnerForSharedFunc), so the
// pointers cannot dangle while the module that owns the table is alive.
// This pins that: discard the module that first compiled the shared target,
// then force a re-run through bind/unbind.
TEST_F(AsHaltingConstGlobalFuncdef, StampedTargetSurvivesOtherModuleDiscardAcrossRebind) {
	asIScriptModule* prov = engine->GetModule("lifeProv", asGM_ALWAYS_CREATE);
	prov->AddScriptSection("s", "int good() { return 1; }\n");
	ASSERT_GE(prov->Build(), 0);

	const char* sharedSrc = "shared int sfn() { return 3; }\n";

	asIScriptModule* owner = engine->GetModule("lifeOwner", asGM_ALWAYS_CREATE);
	owner->AddScriptSection("s", sharedSrc);
	ASSERT_GE(owner->Build(), 0);

	asIScriptModule* user = engine->GetModule("lifeUser", asGM_ALWAYS_CREATE);
	user->AddScriptSection("s",
		"shared int sfn() { return 3; }\n"
		"funcdef int F();\n"
		"F@ const gp = @sfn;\n"
		"import int good() from \"lifeProv\";\n"
		"int viaPtr() { return gp(); }\n"
		"int f() { return good() + viaPtr(); }\n");
	ASSERT_GE(user->Build(), 0);

	asIScriptFunction* viaPtr = user->GetFunctionByName("viaPtr");
	asIScriptFunction* f = user->GetFunctionByName("f");
	ASSERT_NE(viaPtr, nullptr);
	ASSERT_NE(f, nullptr);
	ASSERT_EQ(viaPtr->GetTransitiveHalts(), asHALTS_YES);

	// The module that first compiled `sfn` goes away; ownership of the shared
	// function transfers to lifeUser, which still holds the stamped pointer.
	ASSERT_GE(engine->DiscardModule("lifeOwner"), 0);

	// Re-run the pass through the public bind path.
	ASSERT_GE(user->BindAllImportedFunctions(), 0);
	EXPECT_EQ(viaPtr->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_YES);

	ASSERT_GE(user->UnbindAllImportedFunctions(), 0);
	EXPECT_EQ(viaPtr->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

// Hazard (4) companion: a bound target whose own module has been discarded.
// The bind keeps the asCScriptFunction alive, but InternalReset has cleared
// its `module` back-pointer, so the fold must fail closed rather than
// dereference a module that no longer owns it.
TEST_F(AsHalting, BoundTargetWhoseModuleWasDiscardedIsNotFolded) {
	asIScriptModule* provider = engine->GetModule("dcProvider", asGM_ALWAYS_CREATE);
	provider->AddScriptSection("s", "int good() { return 1; }\n");
	ASSERT_GE(provider->Build(), 0);

	asIScriptModule* other = engine->GetModule("dcOther", asGM_ALWAYS_CREATE);
	other->AddScriptSection("s", "int spare() { return 5; }\n");
	ASSERT_GE(other->Build(), 0);

	asIScriptModule* importer = engine->GetModule("dcImporter", asGM_ALWAYS_CREATE);
	importer->AddScriptSection("s",
		"import int good() from \"dcProvider\";\n"
		"import int spare() from \"dcOther\";\n"
		"int f() { return good(); }\n"
		"int h() { return spare(); }\n");
	ASSERT_GE(importer->Build(), 0);
	ASSERT_GE(importer->BindAllImportedFunctions(), 0);

	asIScriptFunction* f = importer->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);
	ASSERT_EQ(f->GetTransitiveHalts(), asHALTS_YES);

	int goodIdx = importer->GetImportedFunctionIndexByDecl("int good()");
	int spareIdx = importer->GetImportedFunctionIndexByDecl("int spare()");
	ASSERT_GE(goodIdx, 0);
	ASSERT_GE(spareIdx, 0);

	ASSERT_GE(engine->DiscardModule("dcProvider"), 0);

	// Toggle the OTHER import to force a re-run while `good` stays bound to a
	// function whose module pointer InternalReset has since cleared.
	ASSERT_GE(importer->UnbindImportedFunction((asUINT)spareIdx), 0);
	ShowHalts("discarded-provider f", f);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

// LoadByteCode restores localHalts/transitiveHalts/transitiveCallsDelegate
// verbatim from the blob. A module saved while its import was bound carries a
// verdict that the binding justified, and loads with that import UNBOUND, so
// the restored verdict describes a call graph the loaded module does not have.
// Nothing else is needed for the lie: any runtime gate querying the loaded
// function directly gets it. LoadByteCode therefore recomputes.
TEST_F(AsHalting, LoadedByteCodeWithUnboundImportDoesNotReportYes) {
	asIScriptModule* prov = engine->GetModule("lbProv", asGM_ALWAYS_CREATE);
	prov->AddScriptSection("s", "int good() { return 1; }\n");
	ASSERT_GE(prov->Build(), 0);

	asIScriptModule* n = engine->GetModule("lbN", asGM_ALWAYS_CREATE);
	n->AddScriptSection("s",
		"import int h() from \"lbProv\";\n"
		"int t() { return h(); }\n");
	ASSERT_GE(n->Build(), 0);
	ASSERT_GE(n->BindImportedFunction(0, prov->GetFunctionByName("good")), 0);
	ASSERT_EQ(n->GetFunctionByName("t")->GetTransitiveHalts(), asHALTS_YES);

	MemStream stream;
	ASSERT_GE(n->SaveByteCode(&stream), 0);

	// Reload into a separate module. Its import is unbound.
	asIScriptModule* n2 = engine->GetModule("lbN2", asGM_ALWAYS_CREATE);
	ASSERT_GE(n2->LoadByteCode(&stream), 0);
	ASSERT_EQ(n2->GetImportedFunctionCount(), 1u);

	asIScriptFunction* t2 = n2->GetFunctionByName("t");
	ASSERT_NE(t2, nullptr);
	ShowHalts("loaded-unbound t", t2);
	EXPECT_NE(t2->GetTransitiveHalts(), asHALTS_YES);
}

// The same stale verdict, laundered through the CALLBND fold: an importer
// binds to the loaded module's function while that module still has an
// unbound import, then the loaded module's own import is pointed at an
// infinite loop. The importer never recomputes, so a fold that trusted the
// restored verdict would leave a false YES behind. The fold's guard is
// "the target's module has NO imported functions at all", which refuses this
// target whatever its bind state or the provenance of its verdicts.
TEST_F(AsHalting, LoadedByteCodeVerdictIsNotLaunderedThroughTheFold) {
	asIScriptModule* prov = engine->GetModule("ldProv", asGM_ALWAYS_CREATE);
	prov->AddScriptSection("s",
		"int good() { return 1; }\n"
		"int bad() { while (true) { } return 0; }\n");
	ASSERT_GE(prov->Build(), 0);

	asIScriptModule* n = engine->GetModule("ldN", asGM_ALWAYS_CREATE);
	n->AddScriptSection("s",
		"import int h() from \"ldProv\";\n"
		"int t() { return h(); }\n");
	ASSERT_GE(n->Build(), 0);
	ASSERT_GE(n->BindImportedFunction(0, prov->GetFunctionByName("good")), 0);

	MemStream stream;
	ASSERT_GE(n->SaveByteCode(&stream), 0);

	asIScriptModule* n2 = engine->GetModule("ldN2", asGM_ALWAYS_CREATE);
	ASSERT_GE(n2->LoadByteCode(&stream), 0);
	asIScriptFunction* t2 = n2->GetFunctionByName("t");
	ASSERT_NE(t2, nullptr);

	asIScriptModule* m = engine->GetModule("ldM", asGM_ALWAYS_CREATE);
	m->AddScriptSection("s",
		"import int t() from \"ldN2\";\n"
		"int f() { return t(); }\n");
	ASSERT_GE(m->Build(), 0);
	ASSERT_GE(m->BindImportedFunction(0, t2), 0);
	asIScriptFunction* f = m->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);
	ShowHalts("laundered f (at fold)", f);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);

	// Close the trap: ldN2's own import now runs forever. ldN2 recomputes,
	// ldM does not.
	ASSERT_GE(n2->BindImportedFunction(0, prov->GetFunctionByName("bad")), 0);
	ShowHalts("laundered t2 (after rebind)", t2);
	ShowHalts("laundered f (after rebind)", f);
	EXPECT_NE(t2->GetTransitiveHalts(), asHALTS_YES);
	EXPECT_NE(f->GetTransitiveHalts(), asHALTS_YES);
}

// Fix 1-B's own pin. The fold guard tests "the target's module declares no
// imports", not "the target's module has none bound"; the two differ exactly
// here, on a module that declares an import but whose folded function never
// calls it. The weaker guard folds `pure`'s honest YES; the shipped guard
// refuses it. That precision loss is the price of a guard that is a property
// of the target module alone rather than of how its verdicts were produced,
// and it is pinned so removing the strengthening turns this test red.
TEST_F(AsHalting, TargetInAModuleThatMerelyDeclaresAnImportIsNotFolded) {
	asIScriptModule* prov = engine->GetModule("declProv", asGM_ALWAYS_CREATE);
	prov->AddScriptSection("s", "int good() { return 1; }\n");
	ASSERT_GE(prov->Build(), 0);

	// Declares an import, never binds it, and `pure` does not call it.
	asIScriptModule* mid = engine->GetModule("declMid", asGM_ALWAYS_CREATE);
	mid->AddScriptSection("s",
		"import int h() from \"declProv\";\n"
		"int pure() { return 7; }\n");
	ASSERT_GE(mid->Build(), 0);
	asIScriptFunction* pure = mid->GetFunctionByName("pure");
	ASSERT_NE(pure, nullptr);
	ASSERT_EQ(pure->GetTransitiveHalts(), asHALTS_YES);
	ASSERT_EQ(mid->GetImportedFunctionCount(), 1u);

	asIScriptModule* top = engine->GetModule("declTop", asGM_ALWAYS_CREATE);
	top->AddScriptSection("s",
		"import int pure() from \"declMid\";\n"
		"int f() { return pure(); }\n");
	ASSERT_GE(top->Build(), 0);
	ASSERT_GE(top->BindImportedFunction(0, pure), 0);

	asIScriptFunction* f = top->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);
	ShowHalts("declared-import-only f", f);
	EXPECT_EQ(f->GetTransitiveHalts(), asHALTS_UNKNOWN);
}

// A literal loop guard (`while (true)`) compiles to SetV1 v,1 ; CpyVtoR4 v ;
// JLowZ end -- a conditional branch whose outcome is fixed at compile time.
// asCByteCode::Optimize() folds that triple to unconditional flow, so no
// asBC_JLowZ/asBC_JLowNZ survives in the emitted bytecode. The halting verdict
// is unaffected: the analysis elides constant guards on its own side.
TEST_F(AsHalting, LiteralGuardIsFoldedOutOfBytecode) {
	asIScriptFunction* f = Fn("void f() { while (true) { } }", "f");
	ASSERT_NE(f, nullptr);
	asUINT len = 0;
	asDWORD* bc = f->GetByteCode(&len);
	ASSERT_NE(bc, nullptr);
	bool sawLowJump = false;
	for (asUINT n = 0; n < len; ) {
		asBYTE op = *(asBYTE*)&bc[n];
		if (op == asBC_JLowZ || op == asBC_JLowNZ) sawLowJump = true;
		int sz = asBCTypeSize[asBCInfo[op].type];
		if (sz == 0) break;
		n += (asUINT)sz;
	}
	EXPECT_FALSE(sawLowJump);
	EXPECT_EQ(f->GetLocalHalts(), asHALTS_NO);   // verdict unchanged by folding
}

} // namespace
