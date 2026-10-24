// asIScriptModule::CompileMethod: a method compiled against a script class
// without being added to the class.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <string>

namespace {

void MessageCallback(const asSMessageInfo* msg, void* param) {
	std::string* out = static_cast<std::string*>(param);
	*out += msg->message;
	*out += "\n";
}

struct Fixture : ::testing::Test {
	asIScriptEngine* engine = nullptr;
	asIScriptModule* mod = nullptr;
	asITypeInfo* foo = nullptr;
	std::string messages;

	void SetUp() override {
		engine = asCreateScriptEngine();
		engine->SetMessageCallback(asFUNCTION(MessageCallback), &messages, asCALL_CDECL);
		mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("s",
			"int scale = 2;\n"
			"class Foo { int v = 5; int twice() { return v * 2; } }\n"), 0);
		ASSERT_GE(mod->Build(), 0) << messages;
		foo = mod->GetTypeInfoByName("Foo");
		ASSERT_NE(foo, nullptr);
	}
	void TearDown() override { engine->ShutDownAndRelease(); }

	int CallOnNewFoo(asIScriptFunction* method) {
		asIScriptObject* obj = static_cast<asIScriptObject*>(engine->CreateScriptObject(foo));
		asIScriptContext* ctx = engine->CreateContext();
		EXPECT_GE(ctx->Prepare(method), 0);
		EXPECT_GE(ctx->SetObject(obj), 0);
		EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
		int r = int(ctx->GetReturnDWord());
		ctx->Release();
		obj->Release();
		return r;
	}
};

} // namespace

TEST_F(Fixture, CompiledMethodSeesThisAndModuleScope) {
	asIScriptFunction* f = nullptr;
	ASSERT_EQ(mod->CompileMethod(foo, "footer", "int get() { return v * scale + twice(); }", 0, &f), asSUCCESS) << messages;
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->GetObjectType(), foo);
	EXPECT_EQ(CallOnNewFoo(f), 5 * 2 + 10);
	f->Release();
}

TEST_F(Fixture, IsNotAddedToTheTypeOrModule) {
	asUINT methods = foo->GetMethodCount();
	asUINT functions = mod->GetFunctionCount();
	asIScriptFunction* f = nullptr;
	ASSERT_EQ(mod->CompileMethod(foo, "footer", "int get() { return v; }", 0, &f), asSUCCESS) << messages;
	EXPECT_EQ(foo->GetMethodCount(), methods);
	EXPECT_EQ(foo->GetMethodByName("get"), nullptr);
	EXPECT_EQ(mod->GetFunctionCount(), functions);
	EXPECT_TRUE(f->IsFinal());
	f->Release();
}

TEST_F(Fixture, ConstQualifierIsKept) {
	asIScriptFunction* f = nullptr;
	ASSERT_EQ(mod->CompileMethod(foo, "footer", "int get() const { return v; }", 0, &f), asSUCCESS) << messages;
	EXPECT_TRUE(f->IsReadOnly());
	f->Release();
	ASSERT_LT(mod->CompileMethod(foo, "footer", "void set() const { v = 1; }", 0, &f), 0);
	EXPECT_EQ(f, nullptr);
}

TEST_F(Fixture, CompileErrorReturnsNoFunction) {
	asIScriptFunction* f = reinterpret_cast<asIScriptFunction*>(1);
	EXPECT_EQ(mod->CompileMethod(foo, "footer", "int get() { return missing; }", 0, &f), asERROR);
	EXPECT_EQ(f, nullptr);
	EXPECT_NE(messages.find("missing"), std::string::npos);
}

TEST_F(Fixture, RefusesConstructorsAndDestructors) {
	asIScriptFunction* f = nullptr;
	EXPECT_EQ(mod->CompileMethod(foo, "footer", "Foo() { v = 1; }", 0, &f), asNOT_SUPPORTED);
	EXPECT_EQ(f, nullptr);
	EXPECT_EQ(mod->CompileMethod(foo, "footer", "~Foo() { }", 0, &f), asNOT_SUPPORTED);
	EXPECT_EQ(f, nullptr);
}

TEST_F(Fixture, RefusesMoreThanOneFunction) {
	asIScriptFunction* f = nullptr;
	EXPECT_EQ(mod->CompileMethod(foo, "footer", "int a() { return 1; } int b() { return 2; }", 0, &f), asERROR);
	EXPECT_EQ(f, nullptr);
}

TEST_F(Fixture, RefusesATypeFromAnotherModule) {
	asIScriptModule* other = engine->GetModule("other", asGM_ALWAYS_CREATE);
	ASSERT_GE(other->AddScriptSection("s", "class Bar { int w; }"), 0);
	ASSERT_GE(other->Build(), 0) << messages;
	asIScriptFunction* f = nullptr;
	EXPECT_EQ(mod->CompileMethod(other->GetTypeInfoByName("Bar"), "footer", "int get() { return w; }", 0, &f), asINVALID_TYPE);
	EXPECT_EQ(f, nullptr);
}

TEST_F(Fixture, RefusesRegisteredAndNullTypes) {
	asIScriptFunction* f = nullptr;
	EXPECT_EQ(mod->CompileMethod(nullptr, "footer", "int get() { return 1; }", 0, &f), asINVALID_TYPE);
	ASSERT_GE(engine->RegisterObjectType("Native", 4, asOBJ_VALUE | asOBJ_POD), 0);
	EXPECT_EQ(mod->CompileMethod(engine->GetTypeInfoByName("Native"), "footer", "int get() { return 1; }", 0, &f), asINVALID_TYPE);
	EXPECT_EQ(f, nullptr);
}

// The caller's reference keeps the type alive past the module; releasing it
// afterwards must be clean (run under ASan/LSan).
TEST_F(Fixture, OutlivesItsModule) {
	asIScriptFunction* f = nullptr;
	ASSERT_EQ(mod->CompileMethod(foo, "footer", "int get() { return v; }", 0, &f), asSUCCESS) << messages;
	mod->Discard();
	mod = nullptr;
	engine->GarbageCollect();
	EXPECT_STREQ(f->GetObjectName(), "Foo");
	f->Release();
	engine->GarbageCollect();
}

namespace {
int OpenFn() { return 1; }
int SecretFn() { return 2; }
}

// A per-compile mask decides what the compile can name; the module's mask is neither
// used nor changed, so no compile needs a SetAccessMask round trip.
TEST(CompileAccessMask, PerCompileMaskWithoutTouchingTheModule) {
	asIScriptEngine* engine = asCreateScriptEngine();
	std::string messages;
	engine->SetMessageCallback(asFUNCTION(MessageCallback), &messages, asCALL_CDECL);
	engine->SetDefaultAccessMask(1);
	ASSERT_GE(engine->RegisterGlobalFunction("int open()", asFUNCTION(OpenFn), asCALL_CDECL), 0);
	engine->SetDefaultAccessMask(2);
	ASSERT_GE(engine->RegisterGlobalFunction("int secret()", asFUNCTION(SecretFn), asCALL_CDECL), 0);
	engine->SetDefaultAccessMask(1);

	asIScriptModule* mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	mod->SetAccessMask(3);
	ASSERT_GE(mod->AddScriptSection("s", "class Foo { int v = 5; }\n"), 0);
	ASSERT_GE(mod->Build(), 0) << messages;
	asITypeInfo* foo = mod->GetTypeInfoByName("Foo");
	ASSERT_NE(foo, nullptr);

	asIScriptFunction* f = nullptr;
	EXPECT_LT(mod->CompileFunction("narrow", "int f() { return secret(); }", 0, 0, 1u, &f), 0)
		<< "secret() is outside the compile's mask, though inside the module's";
	EXPECT_EQ(f, nullptr);
	ASSERT_GE(mod->CompileFunction("narrow", "int f() { return open(); }", 0, 0, 1u, &f), 0) << messages;
	f->Release();
	f = nullptr;
	EXPECT_LT(mod->CompileMethod(foo, "narrow", "int g() { return secret() + v; }", 0, 1u, &f), 0);
	EXPECT_EQ(f, nullptr);
	ASSERT_GE(mod->CompileMethod(foo, "wide", "int g() { return secret() + v; }", 0, 2u, &f), 0) << messages;
	f->Release();
	f = nullptr;

	EXPECT_EQ(mod->SetAccessMask(3), 3u) << "the module's mask is unchanged";
	// The original signatures still use the module's mask.
	ASSERT_GE(mod->CompileFunction("module", "int f() { return secret(); }", 0, 0, &f), 0) << messages;
	f->Release();
	engine->ShutDownAndRelease();
}
