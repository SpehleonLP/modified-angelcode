// Script-object lifecycle callbacks and the host handle: the host sees every
// script object at construction, before any field initializer, and at
// destruction.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

void MessageCallback(const asSMessageInfo* msg, void* param) {
	*static_cast<std::string*>(param) += std::string(msg->message) + "\n";
}

std::vector<asIScriptObject*> g_created;
std::vector<asQWORD>          g_destroyedHandles;
asQWORD                       g_nextHandle = 0;
int                           g_failAfter = -1;   // -1 = never raise

asIScriptEngine* g_engine = nullptr;
asITypeInfo*     g_fooType = nullptr;
asITypeInfo*     g_outerType = nullptr;
void*            g_uninitResult = nullptr;

void OnCreated(asIScriptObject* o) {
	if (g_failAfter >= 0 && int(g_created.size()) >= g_failAfter) {
		asGetActiveContext()->SetException("table full");
		return;
	}
	g_created.push_back(o);
	o->SetHostHandle(++g_nextHandle);
}
void OnDestroyed(asIScriptObject* o) { g_destroyedHandles.push_back(o->GetHostHandle()); }

int CreatedCount() { return int(g_created.size()); }

// A system function is the one place a host builds an uninitialized object
// while a context can take the created callback's exception.
void CreateUninitializedFoo() {
	g_uninitResult = g_engine->CreateUninitializedScriptObject(g_fooType);
}
void CreateUninitializedOuter() {
	g_uninitResult = g_engine->CreateUninitializedScriptObject(g_outerType);
}

struct Lifecycle : ::testing::Test {
	asIScriptEngine* engine = nullptr;
	asIScriptModule* mod = nullptr;
	std::string messages;

	void SetUp() override {
		g_created.clear();
		g_destroyedHandles.clear();
		g_nextHandle = 0;
		g_failAfter = -1;
		g_uninitResult = nullptr;
		engine = asCreateScriptEngine();
		engine->SetMessageCallback(asFUNCTION(MessageCallback), &messages, asCALL_CDECL);
		engine->SetScriptObjectCreatedCallback(OnCreated);
		engine->SetScriptObjectDestroyedCallback(OnDestroyed);
		ASSERT_GE(engine->RegisterGlobalFunction("int createdCount()", asFUNCTION(CreatedCount), asCALL_CDECL), 0);
		ASSERT_GE(engine->RegisterGlobalFunction("void createUninitializedFoo()", asFUNCTION(CreateUninitializedFoo), asCALL_CDECL), 0);
		ASSERT_GE(engine->RegisterGlobalFunction("void createUninitializedOuter()", asFUNCTION(CreateUninitializedOuter), asCALL_CDECL), 0);
		mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s",
			"class Foo { int v = 5; }\n"
			"class Probe { int seenAtInit = createdCount(); }\n"
			"class Inner { int x = 1; }\n"
			"class Outer { Inner inner; }\n"
			"class Held { Held(Inner@ i) {} }\n"
			"Foo@ makeFoo() { return Foo(); }\n"
			"void makeOuter() { Outer o; }\n"
			"void makeHeld() { Inner i; Held h(i); }\n"
			"void makeUninitialized() { createUninitializedFoo(); }\n"
			"void makeUninitializedOuter() { createUninitializedOuter(); }\n");
		ASSERT_GE(mod->Build(), 0) << messages;
		g_engine = engine;
		g_fooType = Type("Foo");
		g_outerType = Type("Outer");
	}
	void TearDown() override {
		engine->ShutDownAndRelease();
		g_engine = nullptr;
		g_fooType = nullptr;
		g_outerType = nullptr;
	}

	asITypeInfo* Type(const char* name) { return mod->GetTypeInfoByName(name); }
	asIScriptObject* Call(const char* decl, int expected = asEXECUTION_FINISHED) {
		asIScriptContext* ctx = engine->CreateContext();
		ctx->Prepare(mod->GetFunctionByDecl(decl));
		EXPECT_EQ(ctx->Execute(), expected);
		asIScriptObject* o = nullptr;
		if (expected == asEXECUTION_FINISHED) {
			o = static_cast<asIScriptObject*>(ctx->GetReturnObject());
			if (o) {
				o->AddRef();
			}
		}
		ctx->Release();
		return o;
	}
};

} // namespace

TEST_F(Lifecycle, EveryAllocationPathFiresCreated) {
	auto* a = static_cast<asIScriptObject*>(engine->CreateScriptObject(Type("Foo")));
	auto* b = static_cast<asIScriptObject*>(engine->CreateUninitializedScriptObject(Type("Foo")));
	auto* c = static_cast<asIScriptObject*>(engine->CreateScriptObjectCopy(a, Type("Foo")));
	asIScriptObject* d = Call("Foo@ makeFoo()");
	for (asIScriptObject* o : {a, b, c, d}) {
		ASSERT_NE(o, nullptr);
		EXPECT_NE(o->GetHostHandle(), 0u);
		EXPECT_NE(std::find(g_created.begin(), g_created.end(), o), g_created.end());
		o->Release();
	}
}

// Review focus 5: the header is never copied.
TEST_F(Lifecycle, ACopyGetsItsOwnHandle) {
	auto* a = static_cast<asIScriptObject*>(engine->CreateScriptObject(Type("Foo")));
	auto* c = static_cast<asIScriptObject*>(engine->CreateScriptObjectCopy(a, Type("Foo")));
	EXPECT_NE(a->GetHostHandle(), c->GetHostHandle());
	asQWORD const before = c->GetHostHandle();
	c->CopyFrom(a);
	EXPECT_EQ(c->GetHostHandle(), before);
	a->Release();
	c->Release();
}

TEST_F(Lifecycle, CreatedFiresBeforeFieldInitializers) {
	auto* p = static_cast<asIScriptObject*>(engine->CreateScriptObject(Type("Probe")));
	ASSERT_NE(p, nullptr);
	// The initializer saw this object already counted; without the count of
	// one, a callback that never fired would compare 0 with 0.
	ASSERT_EQ(CreatedCount(), 1);
	EXPECT_EQ(*static_cast<int*>(p->GetAddressOfProperty(0)), CreatedCount());
	p->Release();
}

TEST_F(Lifecycle, DestroyedFiresWithTheHandle) {
	auto* a = static_cast<asIScriptObject*>(engine->CreateScriptObject(Type("Foo")));
	asQWORD const h = a->GetHostHandle();
	a->Release();
	ASSERT_EQ(g_destroyedHandles.size(), 1u);
	EXPECT_EQ(g_destroyedHandles[0], h);
}

TEST_F(Lifecycle, HostHandleIsZeroWithoutACallback) {
	asIScriptEngine* plain = asCreateScriptEngine();
	asIScriptModule* m = plain->GetModule("m", asGM_ALWAYS_CREATE);
	m->AddScriptSection("s", "class Foo { int v = 5; }\n");
	ASSERT_GE(m->Build(), 0);
	auto* o = static_cast<asIScriptObject*>(plain->CreateScriptObject(m->GetTypeInfoByName("Foo")));
	EXPECT_EQ(o->GetHostHandle(), 0u);
	o->Release();
	plain->ShutDownAndRelease();
}

TEST_F(Lifecycle, ACallbackExceptionAbandonsTheAllocation) {
	g_failAfter = 0;
	Call("Foo@ makeFoo()", asEXECUTION_EXCEPTION);
	EXPECT_TRUE(g_created.empty());
	EXPECT_EQ(g_destroyedHandles.size(), 1u);   // the half-made object was destroyed
}

// Review focus 4: Outer's member allocation raises after Outer itself registered.
TEST_F(Lifecycle, ANestedCallbackExceptionDestroysBoth) {
	g_failAfter = 1;
	Call("void makeOuter()", asEXECUTION_EXCEPTION);
	EXPECT_EQ(g_created.size(), 1u);
	EXPECT_EQ(g_destroyedHandles.size(), 2u);   // AS_SANITIZE reports any leak
}

// The constructor's arguments are already on the stack when the callback
// refuses the object, and nothing else would release the handle to Inner.
TEST_F(Lifecycle, ACallbackExceptionReleasesTheConstructorArguments) {
	g_failAfter = 1;
	Call("void makeHeld()", asEXECUTION_EXCEPTION);
	EXPECT_EQ(g_created.size(), 1u);
	EXPECT_EQ(g_destroyedHandles.size(), 2u);   // Held abandoned, then Inner released
}

// The host-side factory runs in its own context: the refusal comes back as null.
TEST_F(Lifecycle, AHostCreateThatRaisesReturnsNull) {
	g_failAfter = 0;
	EXPECT_EQ(engine->CreateScriptObject(Type("Foo")), nullptr);
	EXPECT_TRUE(g_created.empty());
	EXPECT_EQ(g_destroyedHandles.size(), 1u);
}

TEST_F(Lifecycle, AnUninitializedCreateThatRaisesReturnsNull) {
	g_failAfter = 0;
	g_uninitResult = reinterpret_cast<void*>(1);
	Call("void makeUninitialized()", asEXECUTION_EXCEPTION);
	EXPECT_EQ(g_uninitResult, nullptr);
	EXPECT_TRUE(g_created.empty());
	EXPECT_EQ(g_destroyedHandles.size(), 1u);
}

// An uninitialized owner allocates its members uninitialized too. A refused
// member is destroyed at the refusal, before its owner unwinds, rather than
// living on unregistered inside it.
TEST_F(Lifecycle, AnUninitializedMemberThatRaisesIsDestroyedAtTheRefusal) {
	g_failAfter = 1;
	g_uninitResult = reinterpret_cast<void*>(1);
	Call("void makeUninitializedOuter()", asEXECUTION_EXCEPTION);
	EXPECT_EQ(g_uninitResult, nullptr);
	ASSERT_EQ(g_created.size(), 1u);
	ASSERT_EQ(g_destroyedHandles.size(), 2u);   // AS_SANITIZE reports any leak
	EXPECT_EQ(g_destroyedHandles[0], 0u);   // the refused Inner, which never got a handle
	EXPECT_EQ(g_destroyedHandles[1], 1u);   // then Outer
}

TEST(LifecycleThreads, CallbacksFireOnEveryThread) {
	static std::atomic<int> created{0};
	static std::atomic<int> destroyed{0};
	created = 0;
	destroyed = 0;
	asIScriptEngine* engine = asCreateScriptEngine();
	engine->SetScriptObjectCreatedCallback([](asIScriptObject*) { created.fetch_add(1); });
	engine->SetScriptObjectDestroyedCallback([](asIScriptObject*) { destroyed.fetch_add(1); });
	asIScriptModule* m = engine->GetModule("m", asGM_ALWAYS_CREATE);
	m->AddScriptSection("s", "class Foo { int v = 5; }\nvoid churn() { for (int i = 0; i < 1000; ++i) { Foo f; } }\n");
	ASSERT_GE(m->Build(), 0);
	asIScriptFunction* churn = m->GetFunctionByDecl("void churn()");
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t) {
		threads.emplace_back([&] {
			asIScriptContext* ctx = engine->CreateContext();
			ctx->Prepare(churn);
			EXPECT_EQ(ctx->Execute(), asEXECUTION_FINISHED);
			ctx->Release();
			asThreadCleanup();
		});
	}
	for (auto& t : threads) {
		t.join();
	}
	EXPECT_EQ(created.load(), 4000);
	EXPECT_EQ(destroyed.load(), 4000);
	engine->ShutDownAndRelease();
}
