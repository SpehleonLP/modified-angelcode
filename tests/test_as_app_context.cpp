// asPushActiveContext / asPopActiveContext with an application-implemented
// context, and the engine's catch(...) sites reporting through it.
#include <gtest/gtest.h>
#include <angelscript.h>
#include <stdexcept>
#include <string>

namespace {

class CNativeCallContext : public asIScriptContext
{
public:
	CNativeCallContext(asIScriptEngine *e) : engine(e), hasException(false) {}

	int AddRef() const { return 1; }
	int Release() const { return 1; }
	asIScriptEngine *GetEngine() const { return engine; }

	int             Prepare(asIScriptFunction *) { return asNOT_SUPPORTED; }
	int             Unprepare() { return asNOT_SUPPORTED; }
	int             Execute() { return asNOT_SUPPORTED; }
	int             Abort() { return asNOT_SUPPORTED; }
	int             Suspend() { return asNOT_SUPPORTED; }
	asEContextState GetState() const { return hasException ? asEXECUTION_EXCEPTION : asEXECUTION_UNINITIALIZED; }
	int             PushState() { return asNOT_SUPPORTED; }
	int             PopState() { return asNOT_SUPPORTED; }
	bool            IsNested(asUINT *nestCount = 0) const { if( nestCount ) *nestCount = 0; return false; }
	int             SetObject(void *) { return asNOT_SUPPORTED; }

	int   SetArgByte(asUINT, asBYTE) { return asNOT_SUPPORTED; }
	int   SetArgWord(asUINT, asWORD) { return asNOT_SUPPORTED; }
	int   SetArgDWord(asUINT, asDWORD) { return asNOT_SUPPORTED; }
	int   SetArgQWord(asUINT, asQWORD) { return asNOT_SUPPORTED; }
	int   SetArgFloat(asUINT, float) { return asNOT_SUPPORTED; }
	int   SetArgDouble(asUINT, double) { return asNOT_SUPPORTED; }
	int   SetArgAddress(asUINT, void *) { return asNOT_SUPPORTED; }
	int   SetArgObject(asUINT, void *) { return asNOT_SUPPORTED; }
	int   SetArgVarType(asUINT, void *, int) { return asNOT_SUPPORTED; }
	void *GetAddressOfArg(asUINT) { return 0; }

	asBYTE  GetReturnByte() { return 0; }
	asWORD  GetReturnWord() { return 0; }
	asDWORD GetReturnDWord() { return 0; }
	asQWORD GetReturnQWord() { return 0; }
	float   GetReturnFloat() { return 0; }
	double  GetReturnDouble() { return 0; }
	void   *GetReturnAddress() { return 0; }
	void   *GetReturnObject() { return 0; }
	void   *GetAddressOfReturnValue() { return 0; }

	int SetException(const char *info, bool = true)
	{
		if( !hasException )
		{
			hasException = true;
			exception = info ? info : "";
		}
		return asSUCCESS;
	}
	int                GetExceptionLineNumber(int *column = 0, const char **sectionName = 0) { if( column ) *column = 0; if( sectionName ) *sectionName = 0; return 0; }
	asIScriptFunction *GetExceptionFunction() { return 0; }
	const char        *GetExceptionString() { return exception.c_str(); }
	bool               WillExceptionBeCaught() { return false; }
	int                SetExceptionCallback(const asSFuncPtr &, void *, int) { return asNOT_SUPPORTED; }
	void               ClearExceptionCallback() {}

	int                SetLineCallback(const asSFuncPtr &, void *, int) { return asNOT_SUPPORTED; }
	void               ClearLineCallback() {}
	asUINT             GetCallstackSize() const { return 0; }
	asIScriptFunction *GetFunction(asUINT = 0) { return 0; }
	int                GetLineNumber(asUINT = 0, int *column = 0, const char **sectionName = 0) { if( column ) *column = 0; if( sectionName ) *sectionName = 0; return 0; }
	int                GetVarCount(asUINT = 0) { return asNOT_SUPPORTED; }
	int                GetVar(asUINT, asUINT, const char **, int * = 0, asETypeModifiers * = 0, bool * = 0, int * = 0) { return asNOT_SUPPORTED; }
#ifdef AS_DEPRECATED
	const char        *GetVarName(asUINT, asUINT = 0) { return 0; }
	int                GetVarTypeId(asUINT, asUINT = 0) { return asNOT_SUPPORTED; }
#endif
	const char        *GetVarDeclaration(asUINT, asUINT = 0, bool = false) { return 0; }
	void              *GetAddressOfVar(asUINT, asUINT = 0, bool = false, bool = false) { return 0; }
	bool               IsVarInScope(asUINT, asUINT = 0) { return false; }
	int                GetThisTypeId(asUINT = 0) { return 0; }
	void              *GetThisPointer(asUINT = 0) { return 0; }
	asIScriptFunction *GetSystemFunction() { return 0; }

	void *SetUserData(void *, asPWORD = 0) { return 0; }
	void *GetUserData(asPWORD = 0) const { return 0; }

	int StartDeserialization() { return asNOT_SUPPORTED; }
	int FinishDeserialization() { return asNOT_SUPPORTED; }
	int PushFunction(asIScriptFunction *, void *) { return asNOT_SUPPORTED; }
	int GetStateRegisters(asUINT, asIScriptFunction **, asIScriptFunction **, asDWORD *, asDWORD *, asQWORD *, void **, asITypeInfo **) { return asNOT_SUPPORTED; }
	int GetCallStateRegisters(asUINT, asDWORD *, asIScriptFunction **, asDWORD *, asDWORD *, asDWORD *) { return asNOT_SUPPORTED; }
	int SetStateRegisters(asUINT, asIScriptFunction *, asIScriptFunction *, asDWORD, asDWORD, asQWORD, void *, asITypeInfo *) { return asNOT_SUPPORTED; }
	int SetCallStateRegisters(asUINT, asDWORD, asIScriptFunction *, asDWORD, asDWORD, asDWORD) { return asNOT_SUPPORTED; }
	int GetArgsOnStackCount(asUINT) { return asNOT_SUPPORTED; }
	int GetArgOnStack(asUINT, asUINT, int *, asUINT *, void **) { return asNOT_SUPPORTED; }

	asIScriptEngine *engine;
	bool             hasException;
	std::string      exception;
};

void RaiseFromNative() {
	if (asIScriptContext* ctx = asGetActiveContext())
		ctx->SetException("native failed");
}

struct Boom {
	int refs = 1;
	void AddRef() { ++refs; }
	void Release() { if (--refs == 0) delete this; }
};
Boom* BoomFactory() { throw std::runtime_error("factory threw"); }

void TranslateException(asIScriptContext* ctx, void*) { ctx->SetException("translated"); }

struct Engine {
	asIScriptEngine* engine = asCreateScriptEngine();
	~Engine() { engine->ShutDownAndRelease(); }
	void RegisterBoom() {
		ASSERT_GE(engine->RegisterObjectType("Boom", 0, asOBJ_REF), 0);
		ASSERT_GE(engine->RegisterObjectBehaviour("Boom", asBEHAVE_FACTORY, "Boom@ f()", asFUNCTION(BoomFactory), asCALL_CDECL), 0);
		ASSERT_GE(engine->RegisterObjectBehaviour("Boom", asBEHAVE_ADDREF, "void f()", asMETHOD(Boom, AddRef), asCALL_THISCALL), 0);
		ASSERT_GE(engine->RegisterObjectBehaviour("Boom", asBEHAVE_RELEASE, "void f()", asMETHOD(Boom, Release), asCALL_THISCALL), 0);
	}
};

} // namespace

TEST(AppContext, PushedContextIsActiveAndPopChecksTheTop) {
	Engine e;
	CNativeCallContext a(e.engine), b(e.engine);
	EXPECT_EQ(asGetActiveContext(), nullptr);
	EXPECT_EQ(asPushActiveContext(nullptr), asINVALID_ARG);
	EXPECT_LT(asPopActiveContext(&a), 0);
	ASSERT_EQ(asPushActiveContext(&a), asSUCCESS);
	ASSERT_EQ(asPushActiveContext(&b), asSUCCESS);
	EXPECT_LT(asPopActiveContext(&a), 0);
	EXPECT_EQ(asGetActiveContext(), &b);
	EXPECT_EQ(asPopActiveContext(&b), asSUCCESS);
	EXPECT_EQ(asPopActiveContext(&a), asSUCCESS);
	EXPECT_EQ(asGetActiveContext(), nullptr);
}

TEST(AppContext, DirectlyCalledNativeReportsToIt) {
	Engine e;
	CNativeCallContext ctx(e.engine);
	asPushActiveContext(&ctx);
	RaiseFromNative();
	asPopActiveContext(&ctx);
	EXPECT_EQ(ctx.GetState(), asEXECUTION_EXCEPTION);
	EXPECT_EQ(ctx.exception, "native failed");
}

// The engine used to reinterpret the active context as its own VM context
// here; with AS_SANITIZE that type confusion aborts on UBSan's vptr check.
TEST(AppContext, AppExceptionCaughtByEngineIsReportedToIt) {
	Engine e;
	e.RegisterBoom();
	CNativeCallContext ctx(e.engine);
	asPushActiveContext(&ctx);
	void* obj = e.engine->CreateScriptObject(e.engine->GetTypeInfoByName("Boom"));
	asPopActiveContext(&ctx);
	EXPECT_EQ(obj, nullptr);
	EXPECT_EQ(ctx.exception, "Caught an exception from the application");
}

TEST(AppContext, TranslateCallbackReceivesIt) {
	Engine e;
	ASSERT_GE(e.engine->SetTranslateAppExceptionCallback(asFUNCTION(TranslateException), nullptr, asCALL_CDECL), 0);
	e.RegisterBoom();
	CNativeCallContext ctx(e.engine);
	asPushActiveContext(&ctx);
	void* obj = e.engine->CreateScriptObject(e.engine->GetTypeInfoByName("Boom"));
	asPopActiveContext(&ctx);
	EXPECT_EQ(obj, nullptr);
	EXPECT_EQ(ctx.exception, "translated");
}

// It refuses PushState, so constructing a script object uses a fresh context
TEST(AppContext, ScriptObjectConstructionFallsBackToARealContext) {
	Engine e;
	asIScriptModule* mod = e.engine->GetModule("m", asGM_ALWAYS_CREATE);
	ASSERT_GE(mod->AddScriptSection("s", "class Foo { int v = 3; }"), 0);
	ASSERT_GE(mod->Build(), 0);
	CNativeCallContext ctx(e.engine);
	asPushActiveContext(&ctx);
	asIScriptObject* foo = static_cast<asIScriptObject*>(e.engine->CreateScriptObject(mod->GetTypeInfoByName("Foo")));
	EXPECT_EQ(asGetActiveContext(), &ctx);
	asPopActiveContext(&ctx);
	ASSERT_NE(foo, nullptr);
	EXPECT_EQ(*static_cast<int*>(foo->GetAddressOfProperty(0)), 3);
	EXPECT_FALSE(ctx.hasException);
	foo->Release();
}
