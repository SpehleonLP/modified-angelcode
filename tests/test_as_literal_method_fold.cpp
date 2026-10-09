// A pure method called on a string literal folds like a global function, and a comparison
// prefers the conversion to the other operand's exact enum. The Hash/MessageNo pair mirrors
// the engine: string converts to both, MessageNo (registered first) is impure.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>
#include <string>
#include "bytecode_scan.h"

namespace {

int ToHash(const std::string& s)
{
	int h = 7;
	for( char c : s )
	{
		h = h * 31 + c;
	}
	return h;
}

int ToMessageNo(const std::string& s)
{
	return 1000 + int(s.size());
}

asUINT PureLength(const std::string& s)
{
	return asUINT(s.size());
}

struct LiteralMethodFold : ::testing::Test
{
	asIScriptEngine *engine = nullptr;
	asIScriptModule *mod = nullptr;
	std::string messages;

	static void Message(const asSMessageInfo *msg, void *param)
	{
		*static_cast<std::string*>(param) += std::string(msg->message) + "\n";
	}

	void SetUp() override
	{
		engine = asCreateScriptEngine();
		engine->SetMessageCallback(asFUNCTION(Message), &messages, asCALL_CDECL);
		RegisterStdString(engine);

		// MessageNo first: string's existing conversion in the engine, impure (it interns).
		ASSERT_GE(engine->RegisterEnum("MessageNo"), 0);
		int r = engine->RegisterObjectMethod("string", "MessageNo opImplConv() const", asFUNCTION(ToMessageNo), asCALL_CDECL_OBJFIRST);
		ASSERT_GE(r, 0);
		engine->GetFunctionById(r)->SetMemoryAccess(asMA_PROGRAM, asMA_PROGRAM);
		ASSERT_GE(engine->RegisterEnum("Hash"), 0);
		r = engine->RegisterObjectMethod("string", "Hash opImplConv() const", asFUNCTION(ToHash), asCALL_CDECL_OBJFIRST);
		ASSERT_GE(r, 0);
		engine->GetFunctionById(r)->SetMemoryAccess(asMA_NONE, asMA_NONE);
		r = engine->RegisterObjectMethod("string", "uint pureLength() const", asFUNCTION(PureLength), asCALL_CDECL_OBJFIRST);
		ASSERT_GE(r, 0);
		engine->GetFunctionById(r)->SetMemoryAccess(asMA_NONE, asMA_NONE);
		engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 1);
	}

	void TearDown() override { engine->ShutDownAndRelease(); }

	asIScriptFunction *Build(const char *script)
	{
		mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", script);
		EXPECT_GE(mod->Build(), 0) << messages;
		return mod->GetFunctionByName("f");
	}

	// Runs fn with at most one 32-bit argument and returns its 32-bit result
	asDWORD Run(asIScriptFunction *fn, bool hasArg = false, asDWORD arg = 0)
	{
		asIScriptContext *ctx = engine->CreateContext();
		EXPECT_GE(ctx->Prepare(fn), 0);
		if( hasArg )
		{
			ctx->SetArgDWord(0, arg);
		}
		EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
		asDWORD result = ctx->GetReturnDWord();
		ctx->Release();
		return result;
	}
};

} // namespace

TEST_F(LiteralMethodFold, ImplicitConversionInAReturnFolds)
{
	asIScriptFunction *f = Build("Hash f() { return \"On\"; }");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(Run(f), asDWORD(ToHash("On")));
	EXPECT_FALSE(CallsSystemFunction(f));
}

TEST_F(LiteralMethodFold, ImplicitConversionInAnArgumentFolds)
{
	asIScriptFunction *f = Build("Hash id(Hash h) { return h; } Hash f() { return id(\"On\"); }");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(Run(f), asDWORD(ToHash("On")));
	EXPECT_FALSE(CallsSystemFunction(f));
}

TEST_F(LiteralMethodFold, ImplicitConversionInAnInitializerFolds)
{
	asIScriptFunction *f = Build("Hash f() { Hash h = \"On\"; return h; }");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(Run(f), asDWORD(ToHash("On")));
	EXPECT_FALSE(CallsSystemFunction(f));
}

TEST_F(LiteralMethodFold, ComparisonUsesTheOtherSidesEnum)
{
	asIScriptFunction *f = Build("bool f(Hash h) { return h == \"On\"; }");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(Run(f, true, asDWORD(ToHash("On"))) & 0xFF, 1u);
	EXPECT_EQ(Run(f, true, asDWORD(ToHash("Off"))) & 0xFF, 0u);
	EXPECT_FALSE(CallsSystemFunction(f));
}

TEST_F(LiteralMethodFold, ComparisonKeepsMessageNoForMessageNo)
{
	asIScriptFunction *f = Build("bool f(MessageNo m) { return m == \"abc\"; }");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(Run(f, true, 1003) & 0xFF, 1u);
	EXPECT_EQ(Run(f, true, 1004) & 0xFF, 0u);
	EXPECT_TRUE(CallsSystemFunction(f));
}

TEST_F(LiteralMethodFold, ExplicitPureMethodOnLiteralFolds)
{
	asIScriptFunction *f = Build("uint f() { return \"abcd\".pureLength(); }");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(Run(f), 4u);
	EXPECT_FALSE(CallsSystemFunction(f));
}

TEST_F(LiteralMethodFold, NonLiteralObjectDoesNotFold)
{
	asIScriptFunction *f = Build("Hash f(const string &in s) { return s; }");
	ASSERT_NE(f, nullptr);
	asIScriptContext *ctx = engine->CreateContext();
	ASSERT_GE(ctx->Prepare(f), 0);
	std::string s = "On";
	ctx->SetArgAddress(0, &s);
	ASSERT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
	EXPECT_EQ(ctx->GetReturnDWord(), asDWORD(ToHash("On")));
	ctx->Release();
	EXPECT_TRUE(CallsSystemFunction(f));
}

TEST_F(LiteralMethodFold, PropertyOffDoesNotFold)
{
	engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 0);
	asIScriptFunction *f = Build("Hash f() { return \"On\"; }");
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(Run(f), asDWORD(ToHash("On")));
	EXPECT_TRUE(CallsSystemFunction(f));
}
