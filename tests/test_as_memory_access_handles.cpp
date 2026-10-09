// Memory access through registered handles: the scanner models the fork's
// handle-resolution opcodes instead of failing closed to {Program, Program}.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <string>

namespace {

struct H { int value = 7; };
H g_h;

void* Resolve(asPWORD bits, void* user) { return bits ? user : nullptr; }
int Get(H* self) { return self->value; }
void Set(int v, H* self) { self->value = v; }

void MessageCallback(const asSMessageInfo* msg, void* param) {
	*static_cast<std::string*>(param) += std::string(msg->message) + "\n";
}

bool Uses(asIScriptFunction* f, asEBCInstr op) {
	asUINT length = 0;
	asDWORD* bc = f->GetByteCode(&length);
	for (asUINT i = 0; i < length; i += asBCTypeSize[asBCInfo[asBYTE(bc[i])].type]) {
		if (asBYTE(bc[i]) == op) {
			return true;
		}
	}
	return false;
}

struct HandleAccess : ::testing::Test {
	asIScriptEngine* engine = nullptr;
	asIScriptModule* mod = nullptr;
	std::string messages;

	void SetUp() override {
		engine = asCreateScriptEngine();
		engine->SetMessageCallback(asFUNCTION(MessageCallback), &messages, asCALL_CDECL);
		ASSERT_GE(engine->RegisterObjectType("H", 0, asOBJ_REF | asOBJ_NOCOUNT), 0);
		ASSERT_GE(engine->RegisterObjectProperty("H", "int value", 0), 0);
		int get = engine->RegisterObjectMethod("H", "int get() const", asFUNCTION(Get), asCALL_CDECL_OBJLAST);
		ASSERT_GE(get, 0);
		ASSERT_GE(engine->GetFunctionById(get)->SetMemoryAccess(asMA_THIS, asMA_NONE), 0);
		int set = engine->RegisterObjectMethod("H", "void set(int)", asFUNCTION(Set), asCALL_CDECL_OBJLAST);
		ASSERT_GE(set, 0);
		ASSERT_GE(engine->GetFunctionById(set)->SetMemoryAccess(asMA_NONE, asMA_THIS), 0);
		ASSERT_GE(engine->RegisterHandle("H", Resolve, &g_h), 0);

		mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		ASSERT_GE(mod->AddScriptSection("s",
			"bool alive(H@ h) { return h !is null; }\n"
			"int field(H@ h) { return h.value; }\n"
			"int call(H@ h) { return h.get(); }\n"
			"void write(H@ h) { h.set(1); }\n"), 0);
		ASSERT_GE(mod->Build(), 0) << messages;
	}
	void TearDown() override { engine->ShutDownAndRelease(); }

	void Expect(const char* name, asEMemoryAccess read, asEMemoryAccess write) {
		asIScriptFunction* f = mod->GetFunctionByName(name);
		ASSERT_NE(f, nullptr) << name;
		asEMemoryAccess r, w;
		f->GetMemoryAccess(&r, &w);
		EXPECT_EQ(r, read) << name;
		EXPECT_EQ(w, write) << name;
	}
};

} // namespace

// The tests below are only meaningful while the compiler emits the handle
// opcodes for these functions.
TEST_F(HandleAccess, FunctionsUseTheHandleOpcodes) {
	EXPECT_TRUE(Uses(mod->GetFunctionByName("alive"), asBC_IsHandleNull));
	asIScriptFunction* field = mod->GetFunctionByName("field");
	EXPECT_TRUE(Uses(field, asBC_LoadHRObjR) || Uses(field, asBC_ResolveHandleV) || Uses(field, asBC_PshHandlePtr));
	asIScriptFunction* call = mod->GetFunctionByName("call");
	EXPECT_TRUE(Uses(call, asBC_PshHandlePtr) || Uses(call, asBC_ResolveHandleV));
}

// A liveness test touches no memory: only Program-scope writes change it.
TEST_F(HandleAccess, NullTestReadsNothing) {
	Expect("alive", asMA_NONE, asMA_NONE);
}

// A resolved object is a registered type's object: its home is Program, but a
// read through it is still only a read.
TEST_F(HandleAccess, ReadsThroughAResolvedHandleWriteNothing) {
	Expect("field", asMA_PROGRAM, asMA_NONE);
	Expect("call", asMA_PROGRAM, asMA_NONE);
}

TEST_F(HandleAccess, WritesThroughAResolvedHandleAreAtItsHome) {
	Expect("write", asMA_NONE, asMA_PROGRAM);
}
