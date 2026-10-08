#include "utils.h"

namespace TestMemoryAccess
{

static const char *const g_names[] = { "None", "WorldStable", "This", "Owned", "Module", "Engine", "Program", "Unset" };

// Returns true on mismatch, after printing which function and what it got
static bool CheckAccess(asIScriptFunction *func, asEMemoryAccess read, asEMemoryAccess write, int line)
{
	if( func == 0 )
	{
		PRINTF("memory access: function not found (line %d)\n", line);
		return true;
	}
	asEMemoryAccess r = asMA_UNSET, w = asMA_UNSET;
	func->GetMemoryAccess(&r, &w);
	if( r != read || w != write )
	{
		PRINTF("memory access of '%s' is {%s, %s}, expected {%s, %s} (line %d)\n",
			func->GetDeclaration(true, true, true), g_names[r & 7], g_names[w & 7], g_names[read & 7], g_names[write & 7], line);
		return true;
	}
	return false;
}

#define EXPECT_ACCESS(func, read, write) if( CheckAccess((func), (read), (write), __LINE__) ) TEST_FAILED

static void NativeNoop() {}
static void NativeMethodNoop(void *) {}

class CNoCount
{
public:
	int value;
};
static CNoCount g_noCount;

static bool TestApi()
{
	bool fail = false;
	COutStream out;
	asIScriptEngine *engine = asCreateScriptEngine();
	engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);

	int id = engine->RegisterGlobalFunction("void noop()", asFUNCTION(NativeNoop), asCALL_CDECL);
	if( id < 0 ) TEST_FAILED;
	asIScriptFunction *global = engine->GetFunctionById(id);

	// Undeclared natives report Unset
	EXPECT_ACCESS(global, asMA_UNSET, asMA_UNSET);

	if( global->SetMemoryAccess(asMA_NONE, asMA_NONE) < 0 ) TEST_FAILED;
	EXPECT_ACCESS(global, asMA_NONE, asMA_NONE);

	// A function without an object cannot touch one
	if( global->SetMemoryAccess(asMA_THIS, asMA_NONE) != asINVALID_ARG ) TEST_FAILED;
	if( global->SetMemoryAccess(asMA_NONE, asMA_OWNED) != asINVALID_ARG ) TEST_FAILED;
	// Unset is not declarable, and nothing writes world-stable state while the VM runs
	if( global->SetMemoryAccess(asMA_UNSET, asMA_NONE) != asINVALID_ARG ) TEST_FAILED;
	if( global->SetMemoryAccess(asMA_NONE, asMA_WORLD_STABLE) != asINVALID_ARG ) TEST_FAILED;
	// A rejected call leaves the previous declaration in place
	EXPECT_ACCESS(global, asMA_NONE, asMA_NONE);

	if( global->SetMemoryAccess(asMA_WORLD_STABLE, asMA_PROGRAM) < 0 ) TEST_FAILED;
	EXPECT_ACCESS(global, asMA_WORLD_STABLE, asMA_PROGRAM);

	engine->RegisterObjectType("NoCount", 0, asOBJ_REF | asOBJ_NOCOUNT);
	id = engine->RegisterObjectMethod("NoCount", "void m()", asFUNCTION(NativeMethodNoop), asCALL_CDECL_OBJLAST);
	asIScriptFunction *method = engine->GetFunctionById(id);
	if( method->SetMemoryAccess(asMA_THIS, asMA_OWNED) < 0 ) TEST_FAILED;
	EXPECT_ACCESS(method, asMA_THIS, asMA_OWNED);

	// Script functions are inferred, never declared
	asIScriptModule *mod = engine->GetModule("api", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("api", "void s() {}");
	if( mod->Build() < 0 ) TEST_FAILED;
	asIScriptFunction *script = mod->GetFunctionByDecl("void s()");
	if( script == 0 || script->SetMemoryAccess(asMA_NONE, asMA_NONE) != asNOT_SUPPORTED ) TEST_FAILED;

	engine->ShutDownAndRelease();
	return fail;
}

// The engine's own registrations must not leave Unset behind for hosts to trip on
static bool TestEngineBuiltins()
{
	bool fail = false;
	asIScriptEngine *engine = asCreateScriptEngine();
	for( int id = 0; id <= engine->GetLastFunctionId(); id++ )
	{
		asIScriptFunction *f = engine->GetFunctionById(id);
		if( f == 0 || f->GetFuncType() != asFUNC_SYSTEM )
		{
			continue;
		}
		asEMemoryAccess r, w;
		f->GetMemoryAccess(&r, &w);
		if( r == asMA_UNSET || w == asMA_UNSET )
		{
			PRINTF("engine built-in '%s' (id %d) is undeclared\n", f->GetDeclaration(true, true, true), id);
			TEST_FAILED;
		}
	}
	engine->ShutDownAndRelease();
	return fail;
}

bool Test()
{
	bool fail = false;
	if( TestApi() ) fail = true;
	if( TestEngineBuiltins() ) fail = true;
	return fail;
}

} // namespace TestMemoryAccess
