// Obviousness audit for the halting analysis.
//
// This file is a MEASUREMENT, not a behavioural spec. test_as_halting.cpp
// pins what the analysis does; this pins how well it does it against the
// acceptance bar:
//
//   1. SOUNDNESS  - a YES or NO verdict must be true. A case whose obvious
//                   answer is YES must never report NO, and vice versa.
//                   Never allowlisted. A failure here is a real bug.
//   2. OBVIOUSNESS - UNKNOWN on a case a human answers instantly is a
//                   defect, not acceptable conservatism. The goal is not a
//                   maximally clever prover; it is that the easy cases are
//                   never wrong and never unknown.
//
// Each corpus entry records the answer a human gets from reading the source,
// with no knowledge of the implementation. Entries the analysis currently
// misses carry a `knownGap` string explaining WHY it misses them; those are
// printed as a scoreboard rather than failing, so the suite stays green while
// the gap list stays honest and visible. A NEW disagreement (knownGap == 0)
// fails. Closing a gap means deleting its knownGap string, not editing the
// expected answer.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <cstdio>
#include <string>

namespace {

const char* HaltsName(asEHalts h) {
	switch (h) {
	case asHALTS_YES:     return "YES";
	case asHALTS_NO:      return "NO";
	case asHALTS_UNKNOWN: return "UNKNOWN";
	}
	return "?";
}

struct ObviousCase {
	const char* name;
	const char* code;
	const char* fn;
	asEHalts    expect;      // the answer a human gets on sight
	bool        transitive;  // query GetTransitiveHalts instead of GetLocalHalts
	const char* knownGap;    // 0 == must match; else why it currently doesn't
};

// ---------------------------------------------------------------------------
// Obviously YES: a reader can see every path reaching the end.
// ---------------------------------------------------------------------------
const ObviousCase kCases[] = {

{ "EmptyBody",
  "void f() { }", "f", asHALTS_YES, false, 0 },

{ "StraightLineArithmetic",
  "int f(int a, int b) { int c = a + b; c *= 2; return c - 1; }",
  "f", asHALTS_YES, false, 0 },

{ "IfElseBothReturn",
  "int f(int a) { if( a > 0 ) return 1; else return 2; }",
  "f", asHALTS_YES, false, 0 },

{ "LiteralBoundedFor",
  "void f() { for( int i = 0; i < 10; i++ ) { } }",
  "f", asHALTS_YES, false, 0 },

{ "LiteralBoundedForWithBody",
  "int f() { int s = 0; for( int i = 0; i < 8; i++ ) s += i; return s; }",
  "f", asHALTS_YES, false, 0 },

{ "NestedLiteralBoundedFor",
  "int f() { int s = 0; for( int i = 0; i < 4; i++ ) for( int j = 0; j < 4; j++ ) s++; return s; }",
  "f", asHALTS_YES, false, 0 },

{ "ConstBoundedFor",
  "const int N = 6; void f() { for( int i = 0; i < N; i++ ) { } }",
  "f", asHALTS_YES, false, 0 },

{ "WhileFalse",
  "void f() { while( false ) { } }",
  "f", asHALTS_YES, false, 0 },

{ "DoWhileFalse",
  "void f() { int x = 0; do { x++; } while( false ); }",
  "f", asHALTS_YES, false, 0 },

{ "WhileTrueWithUnconditionalReturn",
  "void f() { while( true ) { return; } }",
  "f", asHALTS_YES, false, 0 },

{ "WhileTrueWithUnconditionalBreak",
  "void f() { while( true ) { break; } }",
  "f", asHALTS_YES, false, 0 },

{ "ForeverWithUnconditionalReturn",
  "void f() { for( ;; ) { return; } }",
  "f", asHALTS_YES, false, 0 },

{ "BoundedForWithConditionalBreak",
  "int f(int a) { for( int i = 0; i < 10; i++ ) { if( i == a ) break; } return 0; }",
  "f", asHALTS_YES, false, 0 },

{ "BoundedForWithEarlyReturn",
  "int f(int a) { for( int i = 0; i < 10; i++ ) { if( i == a ) return i; } return -1; }",
  "f", asHALTS_YES, false, 0 },

{ "SwitchOnLiteral",
  "int f() { int r = 0; switch( 2 ) { case 1: r = 10; break; case 2: r = 20; break; default: r = 30; } return r; }",
  "f", asHALTS_YES, false, 0 },

{ "CallsAnObviouslyHaltingLocalFunction",
  "int g(int a) { return a * 2; }\n"
  "int f() { return g(3) + g(4); }",
  "f", asHALTS_YES, true, 0 },

// Shared functions are what the engine's GUI lambdas lean on, so YES has to
// survive a call into one.
{ "CallsASharedFunction",
  "shared void g() { }\n"
  "void f() { g(); }",
  "f", asHALTS_YES, true, 0 },

{ "CallsASharedFunctionWithABoundedLoop",
  "shared int g() { int s = 0; for( int i = 0; i < 5; i++ ) s += i; return s; }\n"
  "int f() { return g(); }",
  "f", asHALTS_YES, true, 0 },

{ "CallsALiteralBoundedLoopFunction",
  "int g() { int s = 0; for( int i = 0; i < 5; i++ ) s += i; return s; }\n"
  "int f() { return g(); }",
  "f", asHALTS_YES, true, 0 },

// A non-shared class is resolved to its single implementation, so this is
// already YES. It is in the corpus to hold that ground against regressions.
{ "FinalClassMethodCall",
  "final class C { void m() { } }\n"
  "void f() { C c; c.m(); }",
  "f", asHALTS_YES, true, 0 },

// Same source with `shared` added. `final` forbids derivation, so `c.m()` has
// exactly one possible target in every module that could ever exist -- the
// compiler knows this and the analysis still refuses.
{ "SharedFinalClassMethodCall",
  "shared final class C { void m() { } }\n"
  "void f() { C c; c.m(); }",
  "f", asHALTS_YES, true,
  "DEFERRED INDEFINITELY by the owner (2026-07-23): no current use case for "
  "shared classes. A shared class's virtual targets are treated as open to "
  "override from a module not yet loaded, and `final` is not consulted to "
  "close that question, so the single legal target is discarded. Shared "
  "*functions* are the case that matters to the engine's GUI lambdas, and "
  "those resolve -- see CallsASharedFunction and the frozen-callee test." },

// ---------------------------------------------------------------------------
// Obviously NO: a reader can see a path that never reaches the end.
// ---------------------------------------------------------------------------

{ "BareInfiniteWhile",
  "void f() { while( true ) { } }",
  "f", asHALTS_NO, false, 0 },

{ "BareInfiniteFor",
  "void f() { for( ;; ) { } }",
  "f", asHALTS_NO, false, 0 },

{ "InfiniteLoopWithBody",
  "void f() { int x = 0; while( true ) { x++; } }",
  "f", asHALTS_NO, false, 0 },

{ "InfiniteLoopAfterStraightLine",
  "void f() { int x = 1 + 2; while( true ) { x = x * 2; } }",
  "f", asHALTS_NO, false, 0 },

// f calls spin unconditionally on its only path, so f never returns.
{ "UnconditionalCallToANonHaltingFunction",
  "void spin() { while( true ) { } }\n"
  "void f() { spin(); }",
  "f", asHALTS_NO, true, 0 },

{ "TwoHopCallToANonHaltingFunction",
  "void spin() { while( true ) { } }\n"
  "void mid() { spin(); }\n"
  "void f() { mid(); }",
  "f", asHALTS_NO, true, 0 },

{ "UnconditionalSelfRecursion",
  "void f() { f(); }",
  "f", asHALTS_NO, true, 0 },

{ "MutualRecursionWithNoBaseCase",
  "void f() { g(); }\n"
  "void g() { f(); }",
  "f", asHALTS_NO, true, 0 },

// The call to spin is on one branch only, so f returns whenever a is false.
// Must NOT be NO -- this is the case the must-reach cut exists to exclude.
{ "ConditionalCallToANonHaltingFunction",
  "void spin() { while( true ) { } }\n"
  "void f(bool a) { if( a ) spin(); }",
  "f", asHALTS_UNKNOWN, true, 0 },

// Recursion with a reachable base case returns for some inputs, so NO is
// wrong; proving YES would need the argument to be bounded.
{ "SelfRecursionWithABaseCase",
  "void f(int n) { if( n <= 0 ) return; f(n-1); }",
  "f", asHALTS_UNKNOWN, true, 0 },

// ---------------------------------------------------------------------------
// Genuinely UNKNOWN: a careful reader cannot answer either, so UNKNOWN is the
// correct verdict and NOT a bar violation. These guard against the analysis
// getting "clever" and returning an unsound YES or NO.
// ---------------------------------------------------------------------------

{ "LoopOnAParameterCondition",
  "void f(bool c) { while( c ) { } }",
  "f", asHALTS_UNKNOWN, false, 0 },

// Halts for every int n, including INT_MAX: i rises to n and exits there, so
// the increment never overflows; for n <= 0 the body never runs. The obvious
// answer is YES and the counted-loop prover gets it. Kept in the corpus
// because the near-miss `i += 2` variant below does NOT halt, and the two
// must not be confused.
{ "LoopBoundedByAnIntParameter",
  "void f(int n) { for( int i = 0; i < n; i++ ) { } }",
  "f", asHALTS_YES, false, 0 },

// Does not halt for n == INT_MAX: even i skips the single-value exit window
// and wraps negative forever. Not obvious on sight, and must never be YES.
{ "LoopSteppingPastItsExitWindow",
  "void f() { for( int i = 0; i < 0x7fffffff; i += 2 ) { } }",
  "f", asHALTS_UNKNOWN, false, 0 },

// Not obviously halting: fact(-1) recurses forever.
{ "RecursionWithABaseCaseThatNegativesEscape",
  "int fact(int n) { if( n <= 1 ) return 1; return n * fact(n-1); }",
  "fact", asHALTS_UNKNOWN, true, 0 },

{ "LoopWhoseExitDependsOnAMutatedParameter",
  "void f(int n) { while( n != 0 ) { n = n - 2; } }",
  "f", asHALTS_UNKNOWN, false, 0 },

{ "VirtualCallOnANonFinalClass",
  "class B { void m() { } }\n"
  "class D : B { void m() { while( true ) { } } }\n"
  "void f(B@ b) { b.m(); }",
  "f", asHALTS_UNKNOWN, true, 0 },
};

class AsHaltingObviousness : public ::testing::Test {
protected:
	asIScriptEngine* engine = nullptr;

	void SetUp() override {
		engine = asCreateScriptEngine();
		ASSERT_NE(engine, nullptr);
	}
	void TearDown() override {
		if (engine) engine->ShutDownAndRelease();
	}

	// Build one case in a fresh module. Returns 0 and marks a failure if the
	// script does not compile -- a corpus entry that does not build measures
	// nothing.
	asIScriptFunction* Build(const ObviousCase& c) {
		asIScriptModule* mod = engine->GetModule("obvious", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", c.code);
		if (mod->Build() < 0) {
			ADD_FAILURE() << c.name << ": corpus entry failed to build:\n" << c.code;
			return 0;
		}
		asIScriptFunction* f = mod->GetFunctionByName(c.fn);
		if (!f) ADD_FAILURE() << c.name << ": function not found: " << c.fn;
		return f;
	}
};

// A YES that should be NO is a hang; a NO that should be YES is a spurious
// refusal. Both are real bugs. No allowlist applies here, ever.
TEST_F(AsHaltingObviousness, NoVerdictIsTheOppositeOfTheObviousAnswer)
{
	for (size_t i = 0; i < sizeof(kCases)/sizeof(kCases[0]); ++i) {
		const ObviousCase& c = kCases[i];
		asIScriptFunction* f = Build(c);
		if (!f) continue;
		asEHalts got = c.transitive ? f->GetTransitiveHalts() : f->GetLocalHalts();
		if (c.expect == asHALTS_YES)
			EXPECT_NE(got, asHALTS_NO)
				<< c.name << ": obviously halts, analysis says it cannot";
		if (c.expect == asHALTS_NO)
			EXPECT_NE(got, asHALTS_YES)
				<< c.name << ": obviously does not halt, analysis says it does";
		if (c.expect == asHALTS_UNKNOWN)
			EXPECT_EQ(got, asHALTS_UNKNOWN)
				<< c.name << ": undecidable by reading, but analysis committed to "
				<< HaltsName(got);
	}
}

// The audit proper. Prints the full corpus with a verdict per row, then fails
// on any disagreement that is not a documented gap.
TEST_F(AsHaltingObviousness, ObviousCasesAreNotUnknown)
{
	const size_t n = sizeof(kCases)/sizeof(kCases[0]);
	size_t agree = 0, knownGaps = 0;
	std::string gapReport;

	fprintf(stderr, "\n[ OBVIOUSNESS AUDIT ] %zu cases\n", n);
	for (size_t i = 0; i < n; ++i) {
		const ObviousCase& c = kCases[i];
		asIScriptFunction* f = Build(c);
		if (!f) continue;
		asEHalts got = c.transitive ? f->GetTransitiveHalts() : f->GetLocalHalts();
		const bool ok = (got == c.expect);
		if (ok) ++agree;

		fprintf(stderr, "  %-7s obvious=%-7s got=%-7s %s%s\n",
			ok ? "ok" : (c.knownGap ? "GAP" : "NEW"),
			HaltsName(c.expect), HaltsName(got),
			c.transitive ? "[T] " : "[L] ",
			c.name);

		if (!ok && c.knownGap) {
			++knownGaps;
			gapReport += "  - ";
			gapReport += c.name;
			gapReport += ": ";
			gapReport += c.knownGap;
			gapReport += "\n";
		}
		// A disagreement with no recorded reason is a regression or a new gap.
		if (!ok && !c.knownGap)
			ADD_FAILURE() << c.name << ": obvious answer is " << HaltsName(c.expect)
				<< " but the analysis says " << HaltsName(got)
				<< ". Either fix the analysis or record a knownGap explaining why "
				<< "it cannot see this.\n" << c.code;

		// A gap that has been closed should stop being advertised as one.
		if (ok && c.knownGap)
			ADD_FAILURE() << c.name << ": knownGap is recorded but the analysis "
				<< "now answers correctly. Delete the knownGap string.";
	}

	fprintf(stderr, "[ OBVIOUSNESS AUDIT ] %zu/%zu exact, %zu documented gaps\n",
		agree, n, knownGaps);
	if (!gapReport.empty())
		fprintf(stderr, "[ OBVIOUSNESS AUDIT ] open gaps:\n%s", gapReport.c_str());
}

// The corpus builds one module, so its shared-function rows resolve to a
// callee this module compiled. The case the engine actually hits is the other
// one: a second module referencing a shared function the first module created,
// which arrives as a frozen external callee rather than a graph edge. YES has
// to survive that too, and a shared spinner still has to come back NO.
TEST_F(AsHaltingObviousness, VerdictsCrossIntoAFrozenSharedCallee)
{
	const char* provider =
		"shared void ok() { }\n"
		"shared void spin() { while( true ) { } }\n";
	const char* consumer =
		"shared void ok() { }\n"
		"shared void spin() { while( true ) { } }\n"
		"void callsOk() { ok(); }\n"
		"void callsSpin() { spin(); }\n";

	asIScriptModule* a = engine->GetModule("provider", asGM_ALWAYS_CREATE);
	a->AddScriptSection("s", provider);
	ASSERT_GE(a->Build(), 0);

	asIScriptModule* b = engine->GetModule("consumer", asGM_ALWAYS_CREATE);
	b->AddScriptSection("s", consumer);
	ASSERT_GE(b->Build(), 0);

	asIScriptFunction* callsOk = b->GetFunctionByName("callsOk");
	asIScriptFunction* callsSpin = b->GetFunctionByName("callsSpin");
	ASSERT_NE(callsOk, nullptr);
	ASSERT_NE(callsSpin, nullptr);

	EXPECT_EQ(callsOk->GetTransitiveHalts(), asHALTS_YES)
		<< "a call into a frozen shared function that halts must stay YES";
	EXPECT_EQ(callsSpin->GetTransitiveHalts(), asHALTS_NO)
		<< "an unconditional call into a frozen shared spinner never returns";
}

} // namespace
