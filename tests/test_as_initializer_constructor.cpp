// The initializer constructor: every script class gets a hidden factory that
// runs field initializers only, so hot reload can diff declared defaults
// without running any constructor body.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <string>

namespace {

void MessageCallback(const asSMessageInfo* msg, void* param) {
	std::string* out = static_cast<std::string*>(param);
	*out += msg->message;
	*out += "\n";
}

struct InitCtor : ::testing::Test {
	asIScriptEngine* engine = nullptr;
	std::string messages;

	void SetUp() override {
		engine = asCreateScriptEngine();
		engine->SetMessageCallback(asFUNCTION(MessageCallback), &messages, asCALL_CDECL);
	}
	void TearDown() override { engine->ShutDownAndRelease(); }

	int Build(const char* name, const char* code) {
		asIScriptModule* mod = engine->GetModule(name, asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", code);
		return mod->Build();
	}
	asIScriptModule* Mod(const char* name = "m") { return engine->GetModule(name, asGM_ONLY_IF_EXISTS); }

	asIScriptObject* Run(asIScriptFunction* f) {
		asIScriptContext* ctx = engine->CreateContext();
		EXPECT_GE(ctx->Prepare(f), 0);
		EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
		auto* obj = static_cast<asIScriptObject*>(ctx->GetReturnObject());
		if (obj) {
			obj->AddRef();
		}
		ctx->Release();
		return obj;
	}
	asIScriptObject* MakeWithInitializer(const char* type, const char* module = "m") {
		asITypeInfo* ti = Mod(module)->GetTypeInfoByName(type);
		EXPECT_NE(ti, nullptr);
		asIScriptFunction* f = ti ? ti->GetInitializerFactory() : nullptr;
		EXPECT_NE(f, nullptr);
		return f ? Run(f) : nullptr;
	}
	int IntProp(asIScriptObject* o, const char* name) {
		for (asUINT i = 0; i < o->GetPropertyCount(); ++i) {
			if (std::string(o->GetPropertyName(i)) == name) {
				return *static_cast<int*>(o->GetAddressOfProperty(i));
			}
		}
		ADD_FAILURE() << "no property " << name;
		return -1;
	}
	int GlobalInt(const char* name) {
		asIScriptModule* m = Mod();
		return *static_cast<int*>(m->GetAddressOfGlobalVar(m->GetGlobalVarIndexByName(name)));
	}
};

} // namespace

TEST_F(InitCtor, ExistsWhenOnlyAParameterisedConstructorIsDeclared) {
	ASSERT_GE(Build("m",
		"class Transform { int x; }\n"
		"class Foo { int hp = 100; Foo(const Transform &in t) { hp = 1; } }\n"), 0) << messages;
	asIScriptObject* o = MakeWithInitializer("Foo");
	ASSERT_NE(o, nullptr);
	EXPECT_EQ(IntProp(o, "hp"), 100);
	o->Release();
}

TEST_F(InitCtor, IgnoresTheBodyOfADeclaredDefaultConstructor) {
	ASSERT_GE(Build("m", "class Foo { int hp = 100; Foo() { hp = 7; } }\n"), 0) << messages;
	asIScriptObject* hidden = MakeWithInitializer("Foo");
	ASSERT_NE(hidden, nullptr);
	EXPECT_EQ(IntProp(hidden, "hp"), 100);
	hidden->Release();
	// Ordinary construction is unchanged: the body still runs.
	auto* ordinary = static_cast<asIScriptObject*>(engine->CreateScriptObject(Mod()->GetTypeInfoByName("Foo")));
	ASSERT_NE(ordinary, nullptr);
	EXPECT_EQ(IntProp(ordinary, "hp"), 7);
	ordinary->Release();
}

TEST_F(InitCtor, ExistsWhenTheDefaultConstructorIsDeleted) {
	ASSERT_GE(Build("m", "class Foo { int hp = 100; Foo() delete; Foo(int x) { hp = x; } }\n"), 0) << messages;
	asIScriptObject* o = MakeWithInitializer("Foo");
	ASSERT_NE(o, nullptr);
	EXPECT_EQ(IntProp(o, "hp"), 100);
	o->Release();
	messages.clear();
	EXPECT_LT(Build("n", "class Foo { Foo() delete; Foo(int x) {} }\nvoid f() { Foo@ a = Foo(); }\n"), 0);
}

TEST_F(InitCtor, NeverRunsABaseConstructorBody) {
	ASSERT_GE(Build("m",
		"int g_baseRan = 0;\n"
		"class Base { int b = 3; Base() { g_baseRan++; b = 99; } }\n"
		"class Derived : Base { int d = b + 1; Derived(int x) { super(); d = x; } }\n"), 0) << messages;
	asIScriptObject* o = MakeWithInitializer("Derived");
	ASSERT_NE(o, nullptr);
	EXPECT_EQ(GlobalInt("g_baseRan"), 0);
	EXPECT_EQ(IntProp(o, "b"), 3);
	EXPECT_EQ(IntProp(o, "d"), 4);   // initializers see the base's initialized fields
	o->Release();
}

TEST_F(InitCtor, IsInvisibleToOverloadResolution) {
	EXPECT_LT(Build("m", "class Foo { Foo(int x) {} }\nvoid f() { Foo@ a = Foo(); }\n"), 0);
	messages.clear();
	ASSERT_GE(Build("n", "class Foo { Foo(int x) {} }\n"), 0) << messages;
	asITypeInfo* foo = Mod("n")->GetTypeInfoByName("Foo");
	EXPECT_EQ(foo->GetFactoryCount(), 1u);
	EXPECT_EQ(foo->GetFactoryByDecl("Foo@ Foo()"), nullptr);
	EXPECT_NE(foo->GetInitializerFactory(), nullptr);
}

TEST_F(InitCtor, AbstractClassHasNoFactoryButDerivedClassesInitializeItsFields) {
	ASSERT_GE(Build("m",
		"abstract class A { int a = 1; }\n"
		"class B : A { int c = 2; }\n"), 0) << messages;
	EXPECT_EQ(Mod()->GetTypeInfoByName("A")->GetInitializerFactory(), nullptr);
	asIScriptObject* o = MakeWithInitializer("B");
	ASSERT_NE(o, nullptr);
	EXPECT_EQ(IntProp(o, "a"), 1);
	EXPECT_EQ(IntProp(o, "c"), 2);
	o->Release();
}

TEST_F(InitCtor, InterfaceHasNone) {
	ASSERT_GE(Build("m", "interface I {}\n"), 0) << messages;
	EXPECT_EQ(Mod()->GetTypeInfoByName("I")->GetInitializerFactory(), nullptr);
}

// Review focus 1: the second module reuses the shared type and must not
// regenerate or double-release its behaviours.
TEST_F(InitCtor, SharedClassAcrossModules) {
	const char* code = "shared class S { int v = 3; S(int x) { v = x; } }\n";
	ASSERT_GE(Build("a", code), 0) << messages;
	ASSERT_GE(Build("b", code), 0) << messages;
	asITypeInfo* sa = Mod("a")->GetTypeInfoByName("S");
	asITypeInfo* sb = Mod("b")->GetTypeInfoByName("S");
	ASSERT_EQ(sa, sb);
	asIScriptFunction* f = sa->GetInitializerFactory();
	ASSERT_NE(f, nullptr);
	Mod("a")->Discard();
	engine->GarbageCollect();
	asIScriptObject* o = MakeWithInitializer("S", "b");
	ASSERT_NE(o, nullptr);
	EXPECT_EQ(IntProp(o, "v"), 3);
	o->Release();
	Mod("b")->Discard();
	engine->GarbageCollect();   // AS_SANITIZE's leak check covers the release
}

TEST_F(InitCtor, DiscardReleasesTheNewFunctions) {
	ASSERT_GE(Build("m", "class Foo { int hp = 100; Foo(int x) {} }\n"), 0) << messages;
	Mod()->Discard();
	engine->GarbageCollect();   // a missing release shows as a LeakSanitizer report at exit
}

namespace {
struct Pinned { int v; };
void PinnedCtor(int x, Pinned* self) { self->v = x; }

struct NPinned { int v; explicit NPinned(int x) : v(x) {} ~NPinned() {} };
void NPinnedCtor(int x, NPinned* self) { new (self) NPinned(x); }
void NPinnedDtor(NPinned* self) { self->~NPinned(); }
}

TEST_F(InitCtor, RefusesAMemberWithNoDefaultConstructorAndNoInitializer) {
	EXPECT_LT(Build("m",
		"class Bar { Bar(int x) {} }\n"
		"class Foo { Bar b; Foo() { b = Bar(3); } }\n"), 0);
	EXPECT_NE(messages.find("member 'b' has no default constructor; give it a declaration initializer"), std::string::npos) << messages;
}

TEST_F(InitCtor, RefusesARegisteredValueTypeWithNoDefaultConstructor) {
	ASSERT_GE(engine->RegisterObjectType("Pinned", sizeof(Pinned), asOBJ_VALUE | asOBJ_POD | asOBJ_APP_PRIMITIVE), 0);
	ASSERT_GE(engine->RegisterObjectBehaviour("Pinned", asBEHAVE_CONSTRUCT, "void f(int)", asFUNCTION(PinnedCtor), asCALL_CDECL_OBJLAST), 0);
	// asOBJ_POD makes Pinned zero-constructible, so this must build:
	ASSERT_GE(Build("m", "class Foo { Pinned p; Foo() { p = Pinned(3); } }\n"), 0) << messages;
}

TEST_F(InitCtor, RefusesANonPodValueTypeWithNoDefaultConstructor) {
	ASSERT_GE(engine->RegisterObjectType("NPinned", sizeof(NPinned), asOBJ_VALUE | asOBJ_APP_CLASS_CD), 0);
	ASSERT_GE(engine->RegisterObjectBehaviour("NPinned", asBEHAVE_CONSTRUCT, "void f(int)", asFUNCTION(NPinnedCtor), asCALL_CDECL_OBJLAST), 0);
	ASSERT_GE(engine->RegisterObjectBehaviour("NPinned", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(NPinnedDtor), asCALL_CDECL_OBJLAST), 0);
	EXPECT_LT(Build("m", "class Foo { NPinned p; Foo() { p = NPinned(3); } }\n"), 0);
	EXPECT_NE(messages.find("member 'p' has no default constructor; give it a declaration initializer"), std::string::npos) << messages;
}

TEST_F(InitCtor, AcceptsAnInitializerOrAHandle) {
	ASSERT_GE(Build("m",
		"class Bar { int v; Bar(int x) { v = x; } }\n"
		"class WithInit { Bar b = Bar(3); }\n"
		"class WithHandle { Bar@ b; WithHandle() { @b = Bar(3); } }\n"), 0) << messages;
	asIScriptObject* o = MakeWithInitializer("WithHandle");
	ASSERT_NE(o, nullptr);
	EXPECT_EQ(*static_cast<void**>(o->GetAddressOfProperty(0)), nullptr);
	o->Release();
}

namespace {
size_t CountOf(const std::string& haystack, const std::string& needle) {
	size_t count = 0;
	for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + needle.size())) {
		++count;
	}
	return count;
}
}

// The initializer constructor compiles every field initializer a second time;
// an author must still see each diagnostic once.
TEST_F(InitCtor, AnInitializerWarningIsReportedOnce) {
	ASSERT_GE(Build("m", "class Foo { int x = 1.5f; Foo() {} }\n"), 0) << messages;
	EXPECT_EQ(CountOf(messages, "Implicit conversion"), 1u) << messages;
}

TEST_F(InitCtor, AnInitializerErrorIsReportedOnceAndStillFailsTheBuild) {
	EXPECT_LT(Build("m", "class Foo { int x = noSuchSymbol; Foo() {} }\n"), 0);
	EXPECT_EQ(CountOf(messages, "No matching symbol 'noSuchSymbol'"), 1u) << messages;
}

// Under member-init mode 1 a constructor that assigns the field in its body skips
// the initializer, so the initializer constructor is the only one to report it.
TEST_F(InitCtor, AnInitializerOnlyTheInitializerConstructorCompilesIsStillReported) {
	ASSERT_GE(engine->SetEngineProperty(asEP_MEMBER_INIT_MODE, 1), 0);
	EXPECT_LT(Build("m", "class Foo { int x = noSuchSymbol; Foo() { x = 2; } }\n"), 0);
	EXPECT_EQ(CountOf(messages, "No matching symbol 'noSuchSymbol'"), 1u) << messages;
}

TEST_F(InitCtor, NamesTheFieldInitializersInItsPreMessage) {
	ASSERT_GE(engine->SetEngineProperty(asEP_MEMBER_INIT_MODE, 1), 0);
	EXPECT_LT(Build("m", "class Foo { int x = noSuchSymbol; Foo() { x = 2; } }\n"), 0);
	EXPECT_NE(messages.find("Compiling field initializers of Foo"), std::string::npos) << messages;
}
