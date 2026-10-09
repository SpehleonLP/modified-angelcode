// Pure-call folding on the fork's compiler: a call to a {None, None} native with
// constant arguments becomes a constant, and a call that raises stays a call.
// Re-pins test_feature's TestFolds `fsqrt(2.0)` and TestNotFolded `raises` cases,
// TestStringArguments' `const &in` literal and TestNoGarbageCollect, which fork
// master cannot run (see the memory-access merge report).
#include <gtest/gtest.h>
#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>
#include <cmath>
#include <string>

namespace {

// Number of times a test native ran; a folded call runs at build time only
int g_pureFoldCalls = 0;

double NSqrt(double x) {
	++g_pureFoldCalls;
	return std::sqrt(x);
}

int NChecked(int x) {
	++g_pureFoldCalls;
	if (x < 0) {
		asGetActiveContext()->SetException("negative");
		return 0;
	}
	return x;
}

asUINT Fnv1a(const std::string& s) {
	asUINT h = 2166136261u;
	for (size_t n = 0; n < s.size(); n++) {
		h = (h ^ (unsigned char)s[n]) * 16777619u;
	}
	return h;
}

asUINT NHashConst(const std::string& s) {
	++g_pureFoldCalls;
	return Fnv1a(s);
}

void PureFoldMessageCallback(const asSMessageInfo* msg, void* param) {
	*static_cast<std::string*>(param) += std::string(msg->message) + "\n";
}

// True when func's bytecode contains a CALLSYS to calleeId
bool CallsSystem(asIScriptFunction* func, int calleeId) {
	asUINT length = 0;
	asDWORD* bc = func->GetByteCode(&length);
	for (asUINT i = 0; i < length; i += asBCTypeSize[asBCInfo[asBYTE(bc[i])].type]) {
		if (asBYTE(bc[i]) == asBC_CALLSYS && asBC_INTARG(&bc[i]) == calleeId) {
			return true;
		}
	}
	return false;
}

// fsqrt is registered under its own access bit so the test can check that a
// folded call still counts toward the caller's minimum local access mask.
constexpr asDWORD kSqrtAccessBit = 0x2;

struct PureFoldOnFork : ::testing::Test {
	asIScriptEngine* engine = nullptr;
	asIScriptModule* mod = nullptr;
	std::string messages;
	int sqrtId = -1;
	int checkedId = -1;
	int hashId = -1;
	int callsAfterBuild = 0;

	void SetUp() override {
		engine = asCreateScriptEngine();
		engine->SetMessageCallback(asFUNCTION(PureFoldMessageCallback), &messages, asCALL_CDECL);
		ASSERT_GE(engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 1), 0);

		engine->SetDefaultAccessMask(kSqrtAccessBit);
		sqrtId = engine->RegisterGlobalFunction("double fsqrt(double)", asFUNCTION(NSqrt), asCALL_CDECL);
		engine->SetDefaultAccessMask(1);
		ASSERT_GE(sqrtId, 0);
		ASSERT_GE(engine->GetFunctionById(sqrtId)->SetMemoryAccess(asMA_NONE, asMA_NONE), 0);
		checkedId = engine->RegisterGlobalFunction("int checked(int)", asFUNCTION(NChecked), asCALL_CDECL);
		ASSERT_GE(checkedId, 0);
		ASSERT_GE(engine->GetFunctionById(checkedId)->SetMemoryAccess(asMA_NONE, asMA_NONE), 0);
		RegisterStdString(engine);
		hashId = engine->RegisterGlobalFunction("uint hashc(const string &in)", asFUNCTION(NHashConst), asCALL_CDECL);
		ASSERT_GE(hashId, 0);
		ASSERT_GE(engine->GetFunctionById(hashId)->SetMemoryAccess(asMA_NONE, asMA_NONE), 0);

		mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		mod->SetAccessMask(1 | kSqrtAccessBit);
		ASSERT_GE(mod->AddScriptSection("s",
			"double root() { return fsqrt(2.0); }\n"
			"int raises() { return checked(-1); }\n"
			"uint hashed() { return hashc(\"abc\"); }\n"), 0);
		g_pureFoldCalls = 0;
		ASSERT_GE(mod->Build(), 0) << messages;
		callsAfterBuild = g_pureFoldCalls;
	}
	void TearDown() override { engine->ShutDownAndRelease(); }

	asIScriptFunction* Function(const char* name) {
		asIScriptFunction* f = mod->GetFunctionByName(name);
		EXPECT_NE(f, nullptr) << name;
		return f;
	}
};

} // namespace

TEST_F(PureFoldOnFork, ConstantPureCallFoldsToItsValue) {
	asIScriptFunction* root = Function("root");
	ASSERT_NE(root, nullptr);

	// Folded: the native ran while building, and no CALLSYS to it is left
	EXPECT_GE(callsAfterBuild, 1);
	EXPECT_FALSE(CallsSystem(root, sqrtId));

	// The constant is the right value, and running it calls nothing
	asIScriptContext* ctx = engine->CreateContext();
	ASSERT_GE(ctx->Prepare(root), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	EXPECT_EQ(ctx->GetReturnDouble(), std::sqrt(2.0));
	ctx->Release();
	EXPECT_EQ(g_pureFoldCalls, callsAfterBuild);
}

// The fork records the access bits a function references. The fold skips
// PerformFunctionCall, which also accumulates them, so the bit must come from
// the lookup of the global function instead.
TEST_F(PureFoldOnFork, FoldedCallKeepsTheCalleesAccessBit) {
	asIScriptFunction* root = Function("root");
	ASSERT_NE(root, nullptr);
	ASSERT_FALSE(CallsSystem(root, sqrtId));
	EXPECT_NE(root->GetMinLocalAccessMask() & kSqrtAccessBit, 0u);
}

// A call that raised at build time is compiled as a call, and still raises at run time
TEST_F(PureFoldOnFork, CallThatRaisesIsNotFolded) {
	asIScriptFunction* raises = Function("raises");
	ASSERT_NE(raises, nullptr);
	EXPECT_TRUE(CallsSystem(raises, checkedId));

	asIScriptContext* ctx = engine->CreateContext();
	ASSERT_GE(ctx->Prepare(raises), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_EXCEPTION);
	ctx->Release();
}

// A string literal passed const &in folds: the native gets the literal's object
TEST_F(PureFoldOnFork, StringLiteralArgumentFolds) {
	asIScriptFunction* hashed = Function("hashed");
	ASSERT_NE(hashed, nullptr);
	EXPECT_FALSE(CallsSystem(hashed, hashId));

	int before = g_pureFoldCalls;
	asIScriptContext* ctx = engine->CreateContext();
	ASSERT_GE(ctx->Prepare(hashed), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	EXPECT_EQ(ctx->GetReturnDWord(), Fnv1a("abc"));
	ctx->Release();
	EXPECT_EQ(g_pureFoldCalls, before);
}

// A fold must not change engine state, so the garbage collection step that follows
// an Execute does not run for it: garbage left before a build survives the build
TEST_F(PureFoldOnFork, FoldRunsNoGarbageCollection) {
	ASSERT_EQ(engine->GetEngineProperty(asEP_AUTO_GARBAGE_COLLECT), 1u);

	// A self-referencing object left unreferenced is garbage only the collector can free
	asIScriptModule* gmod = engine->GetModule("g", asGM_ALWAYS_CREATE);
	ASSERT_GE(gmod->AddScriptSection("g", "class Node { Node @next; }\n void make() { Node n; @n.next = n; }\n"), 0);
	ASSERT_GE(gmod->Build(), 0) << messages;
	asIScriptContext* ctx = engine->CreateContext();
	ASSERT_GE(ctx->Prepare(gmod->GetFunctionByName("make")), 0);
	EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	ctx->Release();

	asUINT sizeBefore = 0, destroyedBefore = 0;
	engine->GetGCStatistics(&sizeBefore, &destroyedBefore);
	ASSERT_NE(sizeBefore, 0u);

	// Enough folds that one collection step each would free the garbage
	std::string script = "double f() { return 0";
	for (int n = 0; n < 200; n++) {
		script += " + fsqrt(" + std::to_string(n) + ".0)";
	}
	script += "; }\n";
	asIScriptModule* fmod = engine->GetModule("f", asGM_ALWAYS_CREATE);
	fmod->SetAccessMask(1 | kSqrtAccessBit);
	ASSERT_GE(fmod->AddScriptSection("f", script.c_str()), 0);
	ASSERT_GE(fmod->Build(), 0) << messages;
	asIScriptFunction* f = fmod->GetFunctionByName("f");
	ASSERT_NE(f, nullptr);
	ASSERT_FALSE(CallsSystem(f, sqrtId));

	asUINT sizeAfter = 0, destroyedAfter = 0;
	engine->GetGCStatistics(&sizeAfter, &destroyedAfter);
	EXPECT_EQ(destroyedAfter, destroyedBefore) << "folds collected objects";
	EXPECT_EQ(sizeAfter, sizeBefore);

	// The garbage was real: a full cycle frees it
	engine->GarbageCollect(asGC_FULL_CYCLE);
	engine->GetGCStatistics(nullptr, &destroyedAfter);
	EXPECT_NE(destroyedAfter, destroyedBefore);
}
