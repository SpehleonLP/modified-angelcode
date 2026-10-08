//
// Tests for application-implemented contexts made active with
// asPushActiveContext / asPopActiveContext
//

#include "utils.h"
#include <stdexcept>
#include <string>

namespace TestAppContext
{

// The smallest useful asIScriptContext: it executes nothing and only keeps
// the exception a directly called function posts to it
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

// What a registered function does to report an error to its caller
static void RaiseFromNative()
{
	asIScriptContext *ctx = asGetActiveContext();
	if( ctx )
		ctx->SetException("native failed");
}

class Boom
{
public:
	Boom() : refCount(1) {}
	void AddRef() { refCount++; }
	void Release() { if( --refCount == 0 ) delete this; }
	int refCount;
};
static Boom *BoomFactory() { throw std::runtime_error("factory threw"); }

static void TranslateException(asIScriptContext *ctx, void *)
{
	ctx->SetException("translated");
}

static void RegisterBoom(asIScriptEngine *engine)
{
	engine->RegisterObjectType("Boom", 0, asOBJ_REF);
	engine->RegisterObjectBehaviour("Boom", asBEHAVE_FACTORY, "Boom @f()", asFUNCTION(BoomFactory), asCALL_CDECL);
	engine->RegisterObjectBehaviour("Boom", asBEHAVE_ADDREF, "void f()", asMETHOD(Boom, AddRef), asCALL_THISCALL);
	engine->RegisterObjectBehaviour("Boom", asBEHAVE_RELEASE, "void f()", asMETHOD(Boom, Release), asCALL_THISCALL);
}

bool Test()
{
	bool fail = false;
	int r;
	COutStream out;

	// The pushed context is what asGetActiveContext returns, and popping
	// checks that it is the top one
	{
		asIScriptEngine *engine = asCreateScriptEngine();
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);

		CNativeCallContext a(engine), b(engine);
		if( asGetActiveContext() != 0 )
			TEST_FAILED;
		if( asPushActiveContext(0) != asINVALID_ARG )
			TEST_FAILED;
		if( asPopActiveContext(&a) >= 0 )
			TEST_FAILED;

		r = asPushActiveContext(&a);
		if( r != asSUCCESS )
			TEST_FAILED;
		if( asGetActiveContext() != &a )
			TEST_FAILED;
		r = asPushActiveContext(&b);
		if( r != asSUCCESS )
			TEST_FAILED;
		if( asPopActiveContext(&a) >= 0 )
			TEST_FAILED;
		if( asGetActiveContext() != &b )
			TEST_FAILED;
		if( asPopActiveContext(&b) != asSUCCESS )
			TEST_FAILED;
		if( asPopActiveContext(&a) != asSUCCESS )
			TEST_FAILED;
		if( asGetActiveContext() != 0 )
			TEST_FAILED;

		engine->ShutDownAndRelease();
	}

	// A registered function called directly by the application reports its
	// error to the pushed context
	{
		asIScriptEngine *engine = asCreateScriptEngine();
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);

		CNativeCallContext ctx(engine);
		asPushActiveContext(&ctx);
		RaiseFromNative();
		asPopActiveContext(&ctx);
		if( ctx.GetState() != asEXECUTION_EXCEPTION )
			TEST_FAILED;
		if( ctx.exception != "native failed" )
			TEST_FAILED;

		engine->ShutDownAndRelease();
	}

#ifndef AS_NO_EXCEPTIONS
	// An application exception caught by the engine is reported to the active
	// context through its interface. The engine used to cast the active
	// context to its own context class here.
	{
		asIScriptEngine *engine = asCreateScriptEngine();
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		RegisterBoom(engine);

		CNativeCallContext ctx(engine);
		asPushActiveContext(&ctx);
		void *obj = engine->CreateScriptObject(engine->GetTypeInfoByName("Boom"));
		asPopActiveContext(&ctx);
		if( obj != 0 )
			TEST_FAILED;
		if( ctx.exception != "Caught an exception from the application" )
			TEST_FAILED;

		engine->ShutDownAndRelease();
	}

	// The translate callback receives the active context
	{
		asIScriptEngine *engine = asCreateScriptEngine();
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->SetTranslateAppExceptionCallback(asFUNCTION(TranslateException), 0, asCALL_CDECL);
		RegisterBoom(engine);

		CNativeCallContext ctx(engine);
		asPushActiveContext(&ctx);
		void *obj = engine->CreateScriptObject(engine->GetTypeInfoByName("Boom"));
		asPopActiveContext(&ctx);
		if( obj != 0 )
			TEST_FAILED;
		if( ctx.exception != "translated" )
			TEST_FAILED;

		engine->ShutDownAndRelease();
	}
#endif

	// Creating a script object needs a context to run the constructor in. The
	// pushed context refuses PushState, so the engine uses a new one.
	{
		asIScriptEngine *engine = asCreateScriptEngine();
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		asIScriptModule *mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("s", "class Foo { int v = 3; }");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;

		CNativeCallContext ctx(engine);
		asPushActiveContext(&ctx);
		asIScriptObject *foo = (asIScriptObject*)engine->CreateScriptObject(mod->GetTypeInfoByName("Foo"));
		if( asGetActiveContext() != &ctx )
			TEST_FAILED;
		asPopActiveContext(&ctx);
		if( foo == 0 )
			TEST_FAILED;
		else
		{
			if( *(int*)foo->GetAddressOfProperty(0) != 3 )
				TEST_FAILED;
			foo->Release();
		}
		if( ctx.hasException )
			TEST_FAILED;

		engine->ShutDownAndRelease();
	}

	return fail;
}

} // namespace
