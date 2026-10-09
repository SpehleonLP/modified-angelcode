#include "utils.h"
#include "../../../add_on/scriptstdstring/scriptstdstring.h"
#include "../../../add_on/scripthelper/scripthelper.h"
#include <math.h>
#include <string>

namespace TestPureFold
{

// Number of times any test native ran; a folded call runs at build time only
static int g_calls = 0;

static double NSqrt(double x) { ++g_calls; return sqrt(x); }

// True when func's bytecode contains a CALLSYS to calleeId
static bool CallsSystem(asIScriptFunction *func, int calleeId)
{
	asUINT length = 0;
	asDWORD *bc = func->GetByteCode(&length);
	for( asUINT i = 0; i < length; i += asBCTypeSize[asBCInfo[asBYTE(bc[i])].type] )
	{
		if( asBYTE(bc[i]) == asBC_CALLSYS && asBC_INTARG(&bc[i]) == calleeId )
		{
			return true;
		}
	}
	return false;
}

// Registers fsqrt as {None, None} and returns its id
static int RegisterSqrt(asIScriptEngine *engine)
{
	int id = engine->RegisterGlobalFunction("double fsqrt(double)", asFUNCTION(NSqrt), asCALL_CDECL);
	engine->GetFunctionById(id)->SetMemoryAccess(asMA_NONE, asMA_NONE);
	return id;
}

static asIScriptModule *Build(asIScriptEngine *engine, const char *script)
{
	asIScriptModule *mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("s", script);
	if( mod->Build() < 0 )
	{
		return 0;
	}
	return mod;
}

static bool TestProperty()
{
	bool fail = false;
	COutStream out;
	asIScriptEngine *engine = asCreateScriptEngine();
	engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);

	// Off by default, and only 0 or 1
	if( engine->GetEngineProperty(asEP_FOLD_PURE_CALLS) != 0 ) TEST_FAILED;
	if( engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 2) != asINVALID_ARG ) TEST_FAILED;
	if( engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 1) < 0 ) TEST_FAILED;
	if( engine->GetEngineProperty(asEP_FOLD_PURE_CALLS) != 1 ) TEST_FAILED;
	if( engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 0) < 0 ) TEST_FAILED;

	// With the property off, a pure call is compiled as a call
	int sqrtId = RegisterSqrt(engine);
	asIScriptModule *mod = Build(engine, "double f() { return fsqrt(4.0); }");
	if( mod == 0 ) TEST_FAILED;
	else if( !CallsSystem(mod->GetFunctionByName("f"), sqrtId) ) TEST_FAILED;

	engine->ShutDownAndRelease();
	return fail;
}

static int NMax(int a, int b) { ++g_calls; return a > b ? a : b; }
static int NChecked(int x)
{
	++g_calls;
	if( x < 0 )
	{
		asGetActiveContext()->SetException("negative");
		return 0;
	}
	return x;
}
static double NNow() { ++g_calls; return 42.0; }
static int NUndeclared(int x) { ++g_calls; return x; }
static int NColor(int c) { ++g_calls; return c * 10; }
static int NByRef(const int &x) { ++g_calls; return x + 1; }
static void GSqrt(asIScriptGeneric *gen) { ++g_calls; gen->SetReturnDouble(sqrt(gen->GetArgDouble(0))); }
static void GSumV(asIScriptGeneric *gen)
{
	++g_calls;
	int s = 0;
	for( int n = 0; n < gen->GetArgCount(); n++ )
	{
		s += *(int*)gen->GetAddressOfArg(n);
	}
	gen->SetReturnDWord(s);
}
static asINT8 NNeg8(asINT8 x) { ++g_calls; return asINT8(-x); }
static bool NIsNeg(int x) { ++g_calls; return x < 0; }
static asINT16 NTwice16(asINT16 x) { ++g_calls; return asINT16(x * 2); }
static float NHalf(float x) { ++g_calls; return x * 0.5f; }
static int NBump(int &x) { ++g_calls; x += 10; return x; }
struct Obj { int Pure() const { return 5; } };
static Obj g_obj;
static Obj *GetObj() { return &g_obj; }

static void Declare(asIScriptEngine *engine, int id, asEMemoryAccess r, asEMemoryAccess w)
{
	engine->GetFunctionById(id)->SetMemoryAccess(r, w);
}

static asIScriptEngine *MakeEngine(COutStream &out, int &sqrtId)
{
	asIScriptEngine *engine = asCreateScriptEngine();
	engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
	engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 1);
	sqrtId = RegisterSqrt(engine);
	Declare(engine, engine->RegisterGlobalFunction("int imax(int, int)", asFUNCTION(NMax), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("int checked(int)", asFUNCTION(NChecked), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("double now()", asFUNCTION(NNow), asCALL_CDECL), asMA_WORLD_STABLE, asMA_NONE);
	engine->RegisterGlobalFunction("int undeclared(int)", asFUNCTION(NUndeclared), asCALL_CDECL);
	engine->RegisterEnum("Color");
	engine->RegisterEnumValue("Color", "Red", 1);
	engine->RegisterEnumValue("Color", "Blue", 2);
	Declare(engine, engine->RegisterGlobalFunction("int colorValue(Color)", asFUNCTION(NColor), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("int byRef(const int &in)", asFUNCTION(NByRef), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("double gsqrt(double)", asFUNCTION(GSqrt), asCALL_GENERIC), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("int sumv(int ...)", asFUNCTION(GSumV), asCALL_GENERIC), asMA_NONE, asMA_NONE);
	engine->RegisterObjectType("Obj", 0, asOBJ_REF | asOBJ_NOCOUNT);
	Declare(engine, engine->RegisterObjectMethod("Obj", "int pure() const", asMETHOD(Obj, Pure), asCALL_THISCALL), asMA_NONE, asMA_NONE);
	engine->RegisterGlobalFunction("Obj @getObj()", asFUNCTION(GetObj), asCALL_CDECL);
	return engine;
}

static int IdOf(asIScriptEngine *engine, const char *decl)
{
	return engine->GetGlobalFunctionByDecl(decl)->GetId();
}

// Runs a no-argument function returning a primitive, reading the result by its
// return type because each GetReturn* reads only its own width; returns the context result
static int Run(asIScriptEngine *engine, asIScriptFunction *func, double &result)
{
	asIScriptContext *ctx = engine->CreateContext();
	ctx->Prepare(func);
	int r = ctx->Execute();
	int typeId = func->GetReturnTypeId();
	if( typeId == asTYPEID_DOUBLE )
	{
		result = ctx->GetReturnDouble();
	}
	else if( typeId == asTYPEID_FLOAT )
	{
		result = ctx->GetReturnFloat();
	}
	else if( typeId == asTYPEID_BOOL )
	{
		result = ctx->GetReturnByte();
	}
	else if( typeId == asTYPEID_INT8 )
	{
		result = asINT8(ctx->GetReturnByte());
	}
	else if( typeId == asTYPEID_INT16 )
	{
		result = asINT16(ctx->GetReturnWord());
	}
	else
	{
		result = int(ctx->GetReturnDWord());
	}
	ctx->Release();
	return r;
}

static bool TestFolds()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);

	g_calls = 0;
	asIScriptModule *mod = Build(engine,
		"double root() { return fsqrt(2.0); }\n"
		"double chain() { return fsqrt(2.0) * 3.0 + 1.0; }\n"
		"int biggest() { return imax(3, 7); }\n"
		"int color() { return colorValue(Color::Blue); }\n"
		"int ref() { return byRef(4); }\n"
		"double generic() { return gsqrt(9.0); }\n");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }

	// Every call folded at build time (at least one run each; the spec allows more), no CALLSYS left
	int afterBuild = g_calls;
	if( afterBuild < 6 ) { PRINTF("expected at least 6 build-time calls, got %d\n", afterBuild); TEST_FAILED; }
	if( CallsSystem(mod->GetFunctionByName("root"), sqrtId) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("chain"), sqrtId) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("biggest"), IdOf(engine, "int imax(int, int)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("color"), IdOf(engine, "int colorValue(Color)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("ref"), IdOf(engine, "int byRef(const int &in)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("generic"), IdOf(engine, "double gsqrt(double)")) ) TEST_FAILED;

	// The constants are the right values, and running them calls nothing
	double v = 0;
	if( Run(engine, mod->GetFunctionByName("root"), v) != asEXECUTION_FINISHED ) TEST_FAILED;
	if( v != sqrt(2.0) ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("chain"), v) != asEXECUTION_FINISHED ) TEST_FAILED;
	if( v != sqrt(2.0) * 3.0 + 1.0 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("biggest"), v) != asEXECUTION_FINISHED || v != 7 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("color"), v) != asEXECUTION_FINISHED || v != 20 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("ref"), v) != asEXECUTION_FINISHED || v != 5 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("generic"), v) != asEXECUTION_FINISHED ) TEST_FAILED;
	if( v != 3.0 ) TEST_FAILED;
	if( g_calls != afterBuild ) TEST_FAILED;

	engine->ShutDownAndRelease();
	return fail;
}

static bool TestConversion()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);
	asIScriptModule *mod = Build(engine, "double f() { return fsqrt(2); }");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }
	if( CallsSystem(mod->GetFunctionByName("f"), sqrtId) ) TEST_FAILED;
	double v = 0;
	Run(engine, mod->GetFunctionByName("f"), v);
	if( v != sqrt(2.0) ) TEST_FAILED;
	engine->ShutDownAndRelease();
	return fail;
}

static bool TestNesting()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);
	asIScriptModule *mod = Build(engine, "double f() { return fsqrt(fsqrt(16.0)); }");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }
	if( CallsSystem(mod->GetFunctionByName("f"), sqrtId) ) TEST_FAILED;
	double v = 0;
	Run(engine, mod->GetFunctionByName("f"), v);
	if( v != 2.0 ) TEST_FAILED;
	engine->ShutDownAndRelease();
	return fail;
}

static bool TestConstGlobal()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);
	// A folded initialiser makes the global a compile-time constant usable as a case label
	asIScriptModule *mod = Build(engine,
		"const int R = imax(2, 3);\n"
		"int other = 1;\n"
		"int sw(int x) { switch( x ) { case R: return 1; } return other; }\n"
		"int pick() { return sw(3) * 10 + sw(4); }\n");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }
	double v = 0;
	if( Run(engine, mod->GetFunctionByName("pick"), v) != asEXECUTION_FINISHED || v != 11 ) TEST_FAILED;
	engine->ShutDownAndRelease();
	return fail;
}

static bool TestNotFolded()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);
	// Each half of the purity check on its own: a write, or a read, beyond None never folds.
	// A global function cannot be This or Owned, so Engine and Program are the writes to try.
	Declare(engine, engine->RegisterGlobalFunction("int writesProgram(int)", asFUNCTION(NUndeclared), asCALL_CDECL), asMA_NONE, asMA_PROGRAM);
	Declare(engine, engine->RegisterGlobalFunction("int writesEngine(int)", asFUNCTION(NUndeclared), asCALL_CDECL), asMA_NONE, asMA_ENGINE);
	Declare(engine, engine->RegisterGlobalFunction("int readsProgram(int)", asFUNCTION(NUndeclared), asCALL_CDECL), asMA_PROGRAM, asMA_NONE);
	asIScriptModule *mod = Build(engine,
		"int wProgram() { return writesProgram(1); }\n"
		"int wEngine() { return writesEngine(1); }\n"
		"int rProgram() { return readsProgram(1); }\n"
		"double variable(double x) { return fsqrt(x); }\n"
		"double stable() { return now(); }\n"
		"int unset() { return undeclared(1); }\n"
		"int method() { return getObj().pure(); }\n"
		"int variadic() { return sumv(1, 2, 3); }\n"
		"int raises() { return checked(-1); }\n"
		"int fine() { return checked(5); }\n");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }

	if( !CallsSystem(mod->GetFunctionByName("wProgram"), IdOf(engine, "int writesProgram(int)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("wEngine"), IdOf(engine, "int writesEngine(int)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("rProgram"), IdOf(engine, "int readsProgram(int)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("variable"), sqrtId) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("stable"), IdOf(engine, "double now()")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("unset"), IdOf(engine, "int undeclared(int)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("variadic"), IdOf(engine, "int sumv(int ...)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("raises"), IdOf(engine, "int checked(int)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("fine"), IdOf(engine, "int checked(int)")) ) TEST_FAILED;
	asITypeInfo *objType = engine->GetTypeInfoByName("Obj");
	if( !CallsSystem(mod->GetFunctionByName("method"), objType->GetMethodByName("pure")->GetId()) ) TEST_FAILED;

	// A call that raised at build time still raises at run time
	double v = 0;
	if( Run(engine, mod->GetFunctionByName("raises"), v) != asEXECUTION_EXCEPTION ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("fine"), v) != asEXECUTION_FINISHED || v != 5 ) TEST_FAILED;

	engine->ShutDownAndRelease();
	return fail;
}

static bool TestWidths()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);
	Declare(engine, engine->RegisterGlobalFunction("int8 neg8(int8)", asFUNCTION(NNeg8), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("bool isNeg(int)", asFUNCTION(NIsNeg), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("int16 twice16(int16)", asFUNCTION(NTwice16), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("float half(float)", asFUNCTION(NHalf), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("int bump(int &in)", asFUNCTION(NBump), asCALL_CDECL), asMA_NONE, asMA_NONE);

	g_calls = 0;
	asIScriptModule *mod = Build(engine,
		"int8 n8() { return neg8(5); }\n"
		"bool neg() { return isNeg(-3); }\n"
		"int16 n16() { return twice16(-300); }\n"
		"float fh() { return half(3.0f); }\n"
		"int bl() { return bump(4); }\n"
		"const int K = 5;\n"
		"int bk() { return bump(K) * 100 + K; }\n");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }

	// 1-byte, 2-byte and float arguments and results all fold
	int afterBuild = g_calls;
	if( CallsSystem(mod->GetFunctionByName("n8"), IdOf(engine, "int8 neg8(int8)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("neg"), IdOf(engine, "bool isNeg(int)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("n16"), IdOf(engine, "int16 twice16(int16)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("fh"), IdOf(engine, "float half(float)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("bl"), IdOf(engine, "int bump(int &in)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("bk"), IdOf(engine, "int bump(int &in)")) ) TEST_FAILED;

	double v = 0;
	if( Run(engine, mod->GetFunctionByName("n8"), v) != asEXECUTION_FINISHED || v != -5 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("neg"), v) != asEXECUTION_FINISHED || v != 1 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("n16"), v) != asEXECUTION_FINISHED || v != -600 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("fh"), v) != asEXECUTION_FINISHED || v != 1.5 ) TEST_FAILED;

	// A non-const &in native that alters its argument changes only the fold's own copy
	if( Run(engine, mod->GetFunctionByName("bl"), v) != asEXECUTION_FINISHED || v != 14 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("bk"), v) != asEXECUTION_FINISHED || v != 1505 ) TEST_FAILED;
	if( g_calls != afterBuild ) TEST_FAILED;

	engine->ShutDownAndRelease();
	return fail;
}

static bool TestSaveLoad()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);
	asIScriptModule *mod = Build(engine, "double f() { return fsqrt(2.0) * 2.0; }");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }
	CBytecodeStream stream(__FILE__"1");
	if( mod->SaveByteCode(&stream) < 0 ) TEST_FAILED;
	engine->ShutDownAndRelease();

	// Load into an engine that does not fold: the constant comes with the bytecode
	engine = MakeEngine(out, sqrtId);
	engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 0);
	mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	if( mod->LoadByteCode(&stream) < 0 ) TEST_FAILED;
	g_calls = 0;
	double v = 0;
	if( Run(engine, mod->GetFunctionByName("f"), v) != asEXECUTION_FINISHED ) TEST_FAILED;
	if( v != sqrt(2.0) * 2.0 ) TEST_FAILED;
	if( g_calls != 0 ) TEST_FAILED;
	engine->ShutDownAndRelease();
	return fail;
}

bool Test()
{
	bool fail = false;
	fail = TestProperty() || fail;
	fail = TestFolds() || fail;
	fail = TestConversion() || fail;
	fail = TestNesting() || fail;
	fail = TestConstGlobal() || fail;
	fail = TestNotFolded() || fail;
	fail = TestWidths() || fail;
	fail = TestSaveLoad() || fail;
	if( fail )
	{
		PRINTF("TestPureFold failed\n");
	}
	return fail;
}

} // namespace TestPureFold
