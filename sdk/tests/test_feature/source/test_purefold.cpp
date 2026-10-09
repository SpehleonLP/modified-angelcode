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

bool Test()
{
	bool fail = false;
	fail = TestProperty() || fail;
	if( fail )
	{
		PRINTF("TestPureFold failed\n");
	}
	return fail;
}

} // namespace TestPureFold
