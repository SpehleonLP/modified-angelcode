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
	else if( typeId == asTYPEID_UINT32 )
	{
		result = ctx->GetReturnDWord();
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

static asUINT Fnv1a(const std::string &s)
{
	asUINT h = 2166136261u;
	for( size_t n = 0; n < s.size(); n++ )
	{
		h = (h ^ (unsigned char)s[n]) * 16777619u;
	}
	return h;
}
static asUINT NHashConst(const std::string &s) { ++g_calls; return Fnv1a(s); }
static asUINT NHashRef(std::string &s) { ++g_calls; asUINT h = Fnv1a(s); s = "clobbered"; return h; }
static asUINT NHashValue(std::string s) { ++g_calls; return Fnv1a(s); }
static std::string NFormat(int x) { ++g_calls; return std::to_string(x); }
static asUINT NHash2(const std::string &s, int x) { ++g_calls; return Fnv1a(s) ^ asUINT(x); }

static asIScriptEngine *MakeStringEngine(COutStream &out)
{
	asIScriptEngine *engine = asCreateScriptEngine();
	engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
	engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, 1);
	RegisterStdString(engine);
	Declare(engine, engine->RegisterGlobalFunction("uint hashc(const string &in)", asFUNCTION(NHashConst), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("uint hashr(string &in)", asFUNCTION(NHashRef), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("uint hashv(string)", asFUNCTION(NHashValue), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("string fmt(int)", asFUNCTION(NFormat), asCALL_CDECL), asMA_NONE, asMA_NONE);
	Declare(engine, engine->RegisterGlobalFunction("uint hash2(const string &in, int)", asFUNCTION(NHash2), asCALL_CDECL), asMA_NONE, asMA_NONE);
	return engine;
}

static bool TestStringArguments()
{
	bool fail = false;
	COutStream out;
	asIScriptEngine *engine = MakeStringEngine(out);
	g_calls = 0;
	asIScriptModule *mod = Build(engine,
		"uint c() { return hashc(\"Moniker\"); }\n"
		"uint r() { return hashr(\"Moniker\"); }\n"
		"uint v() { return hashv(\"Moniker\"); }\n"
		"const uint kId = hashc(\"Moniker\");\n"
		"int sw(uint x) { switch( x ) { case kId: return 1; } return 0; }\n"
		"string s() { return fmt(5); }\n"
		"uint nonLiteral(const string &in x) { return hashc(x); }\n"
		"const string kName = \"Moniker\";\n"
		"uint constGlobal() { return hashc(kName); }\n"
		"uint viaCtor() { return hashc(string(\"Moniker\")); }\n"
		"uint viaTernary(bool b) { return hashc(b ? \"Moniker\" : \"Other\"); }\n"
		"uint viaConcat() { return hashc(\"Mon\" + \"iker\"); }\n"
		"uint mixed() { return hash2(\"M\", 3); }\n"
		"uint mixedVar(int x) { return hash2(\"M\", x); }\n");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }
	int afterBuild = g_calls;

	if( CallsSystem(mod->GetFunctionByName("c"), IdOf(engine, "uint hashc(const string &in)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("r"), IdOf(engine, "uint hashr(string &in)")) ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("v"), IdOf(engine, "uint hashv(string)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("s"), IdOf(engine, "string fmt(int)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("nonLiteral"), IdOf(engine, "uint hashc(const string &in)")) ) TEST_FAILED;
	// A const global is a variable, not a literal, so it does not fold in this step
	if( !CallsSystem(mod->GetFunctionByName("constGlobal"), IdOf(engine, "uint hashc(const string &in)")) ) TEST_FAILED;
	// A string expression with code of its own is not a literal, even with a constant value
	if( !CallsSystem(mod->GetFunctionByName("viaCtor"), IdOf(engine, "uint hashc(const string &in)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("viaTernary"), IdOf(engine, "uint hashc(const string &in)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("viaConcat"), IdOf(engine, "uint hashc(const string &in)")) ) TEST_FAILED;
	// A literal mixed with a primitive folds only when the primitive is constant too
	if( CallsSystem(mod->GetFunctionByName("mixed"), IdOf(engine, "uint hash2(const string &in, int)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("mixedVar"), IdOf(engine, "uint hash2(const string &in, int)")) ) TEST_FAILED;

	double q = 0;
	asUINT expected = Fnv1a("Moniker");
	if( Run(engine, mod->GetFunctionByName("c"), q) != asEXECUTION_FINISHED || q != expected ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("r"), q) != asEXECUTION_FINISHED || q != expected ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("v"), q) != asEXECUTION_FINISHED || q != expected ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("mixed"), q) != asEXECUTION_FINISHED || q != (Fnv1a("M") ^ 3u) ) TEST_FAILED;
	if( g_calls != afterBuild ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("constGlobal"), q) != asEXECUTION_FINISHED || q != expected ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("viaCtor"), q) != asEXECUTION_FINISHED || q != expected ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("viaConcat"), q) != asEXECUTION_FINISHED || q != expected ) TEST_FAILED;

	asIScriptContext *ctx = engine->CreateContext();
	ctx->Prepare(mod->GetFunctionByName("sw"));
	ctx->SetArgDWord(0, expected);
	if( ctx->Execute() != asEXECUTION_FINISHED || ctx->GetReturnDWord() != 1 ) TEST_FAILED;
	ctx->Prepare(mod->GetFunctionByName("mixedVar"));
	ctx->SetArgDWord(0, 5);
	if( ctx->Execute() != asEXECUTION_FINISHED || ctx->GetReturnDWord() != (Fnv1a("M") ^ 5u) ) TEST_FAILED;
	// Both branches, so a fold that took the first literal in the code would show
	ctx->Prepare(mod->GetFunctionByName("viaTernary"));
	ctx->SetArgByte(0, 1);
	if( ctx->Execute() != asEXECUTION_FINISHED || ctx->GetReturnDWord() != expected ) TEST_FAILED;
	ctx->Prepare(mod->GetFunctionByName("viaTernary"));
	ctx->SetArgByte(0, 0);
	if( ctx->Execute() != asEXECUTION_FINISHED || ctx->GetReturnDWord() != Fnv1a("Other") ) TEST_FAILED;
	ctx->Release();

	engine->ShutDownAndRelease();
	return fail;
}

// hashr clobbers its argument; the literal itself must survive the fold. The std string
// factory caches a literal by its text, so a clobbered object no longer matches "Moniker"
// and later uses of the literal get a fresh one. Only code compiled before the fold still
// holds the shared object, hence before() and after(): whichever is compiled first sees it.
// They check the length, as "clobbered" is 9 long, because a literal compared in script
// may itself be the clobbered object.
static bool TestStringInRefCopy()
{
	bool fail = false;
	COutStream out;
	asIScriptEngine *engine = MakeStringEngine(out);
	asIScriptModule *mod = Build(engine,
		"string before() { return \"Moniker\"; }\n"
		"uint r() { return hashr(\"Moniker\"); }\n"
		"string after() { return \"Moniker\"; }\n"
		"bool same() { return \"Moniker\" == \"Moniker\" && hashc(\"Moniker\") == r(); }\n"
		"bool intact() { return before().length() == 7 && after().length() == 7; }\n");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }
	if( CallsSystem(mod->GetFunctionByName("r"), IdOf(engine, "uint hashr(string &in)")) ) TEST_FAILED;
	double q = 0;
	if( Run(engine, mod->GetFunctionByName("same"), q) != asEXECUTION_FINISHED || q != 1 ) TEST_FAILED;
	if( Run(engine, mod->GetFunctionByName("intact"), q) != asEXECUTION_FINISHED || q != 1 ) TEST_FAILED;
	engine->ShutDownAndRelease();
	return fail;
}

static int g_requests = 0;
static int g_returns = 0;
static asIScriptContext *RequestCtx(asIScriptEngine *engine, void *) { ++g_requests; return engine->CreateContext(); }
static void ReturnCtx(asIScriptEngine *, asIScriptContext *ctx, void *) { ++g_returns; ctx->Release(); }

// A fold runs on its own context: the host's context pool, whose return callback may
// treat an exception as a script crash, never sees it, even for a call that raises
static bool TestNoContextCallbacks()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);
	engine->SetContextCallbacks(RequestCtx, ReturnCtx, 0);
	g_requests = 0;
	g_returns = 0;
	g_calls = 0;
	asIScriptModule *mod = Build(engine,
		"int fine() { return checked(5); }\n"
		"int raises() { return checked(-1); }\n");
	if( mod == 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }

	// Both calls ran at build time; only the one that finished folded
	if( g_calls < 2 ) TEST_FAILED;
	if( CallsSystem(mod->GetFunctionByName("fine"), IdOf(engine, "int checked(int)")) ) TEST_FAILED;
	if( !CallsSystem(mod->GetFunctionByName("raises"), IdOf(engine, "int checked(int)")) ) TEST_FAILED;
	if( g_requests != 0 ) { PRINTF("fold requested %d pooled contexts\n", g_requests); TEST_FAILED; }
	if( g_returns != 0 ) { PRINTF("fold returned %d pooled contexts\n", g_returns); TEST_FAILED; }

	engine->ShutDownAndRelease();
	return fail;
}

// A fold must not change engine state, so the garbage collection step that follows an
// Execute does not run for it: garbage left before a build survives the build
static bool TestNoGarbageCollect()
{
	bool fail = false;
	COutStream out;
	int sqrtId = 0;
	asIScriptEngine *engine = MakeEngine(out, sqrtId);
	if( engine->GetEngineProperty(asEP_AUTO_GARBAGE_COLLECT) != 1 ) TEST_FAILED;

	// A self-referencing object left unreferenced is garbage only the collector can free
	asIScriptModule *gmod = engine->GetModule("g", asGM_ALWAYS_CREATE);
	gmod->AddScriptSection("g", "class Node { Node @next; }\n void make() { Node n; @n.next = n; }\n");
	if( gmod->Build() < 0 ) { TEST_FAILED; engine->ShutDownAndRelease(); return fail; }
	asIScriptContext *ctx = engine->CreateContext();
	ctx->Prepare(gmod->GetFunctionByName("make"));
	if( ctx->Execute() != asEXECUTION_FINISHED ) TEST_FAILED;
	ctx->Release();

	asUINT sizeBefore = 0, destroyedBefore = 0;
	engine->GetGCStatistics(&sizeBefore, &destroyedBefore);
	if( sizeBefore == 0 ) TEST_FAILED;

	// Enough folds that one collection step each would free the garbage
	std::string script = "int f() { return 0";
	for( int n = 0; n < 200; n++ )
	{
		script += " + imax(" + std::to_string(n) + ", 1)";
	}
	script += "; }\n";
	asIScriptModule *mod = Build(engine, script.c_str());
	if( mod == 0 ) TEST_FAILED;
	else if( CallsSystem(mod->GetFunctionByName("f"), IdOf(engine, "int imax(int, int)")) ) TEST_FAILED;

	asUINT sizeAfter = 0, destroyedAfter = 0;
	engine->GetGCStatistics(&sizeAfter, &destroyedAfter);
	if( destroyedAfter != destroyedBefore ) { PRINTF("folds collected %u objects\n", destroyedAfter - destroyedBefore); TEST_FAILED; }
	if( sizeAfter != sizeBefore ) TEST_FAILED;

	// The garbage was real: a full cycle frees it
	engine->GarbageCollect(asGC_FULL_CYCLE);
	engine->GetGCStatistics(0, &destroyedAfter);
	if( destroyedAfter == destroyedBefore ) TEST_FAILED;

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
	fail = TestStringArguments() || fail;
	fail = TestStringInRefCopy() || fail;
	fail = TestNoContextCallbacks() || fail;
	fail = TestNoGarbageCollect() || fail;
	if( fail )
	{
		PRINTF("TestPureFold failed\n");
	}
	return fail;
}

} // namespace TestPureFold
