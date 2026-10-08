//
// Tests for asIScriptModule::CompileMethod
//

#include "utils.h"
#include <string>

namespace TestCompileMethod
{

static int CallOnNewObject(asIScriptEngine *engine, asITypeInfo *type, asIScriptFunction *method, bool &fail)
{
	asIScriptObject *obj = (asIScriptObject*)engine->CreateScriptObject(type);
	asIScriptContext *ctx = engine->CreateContext();
	int result = 0;
	if( ctx->Prepare(method) < 0 || ctx->SetObject(obj) < 0 || ctx->Execute() != asEXECUTION_FINISHED )
		TEST_FAILED;
	else
		result = (int)ctx->GetReturnDWord();
	ctx->Release();
	obj->Release();
	return result;
}

bool Test()
{
	bool fail = false;
	int r;
	CBufferedOutStream bout;
	asIScriptFunction *func;

	asIScriptEngine *engine = asCreateScriptEngine();
	engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);

	asIScriptModule *mod = engine->GetModule("m", asGM_ALWAYS_CREATE);
	mod->AddScriptSection("s",
		"int scale = 2;\n"
		"class Foo { int v = 5; int twice() { return v * 2; } }\n"
		"interface I { int get(); }\n");
	r = mod->Build();
	if( r < 0 )
		TEST_FAILED;
	asITypeInfo *foo = mod->GetTypeInfoByName("Foo");
	asUINT methodCount = foo->GetMethodCount();
	asUINT functionCount = mod->GetFunctionCount();

	// The method sees this, the other members and the module scope, but it is
	// not added to the class or the module
	func = 0;
	r = mod->CompileMethod(foo, "method", "int get() { return v * scale + twice(); }", 0, &func);
	if( r != asSUCCESS || func == 0 )
		TEST_FAILED;
	else
	{
		if( func->GetObjectType() != foo )
			TEST_FAILED;
		if( !func->IsFinal() )
			TEST_FAILED;
		if( CallOnNewObject(engine, foo, func, fail) != 5 * 2 + 10 )
			TEST_FAILED;
		func->Release();
	}
	if( foo->GetMethodCount() != methodCount || foo->GetMethodByName("get") != 0 )
		TEST_FAILED;
	if( mod->GetFunctionCount() != functionCount )
		TEST_FAILED;

	// const is kept, and enforced
	func = 0;
	r = mod->CompileMethod(foo, "method", "int get() const { return v; }", 0, &func);
	if( r != asSUCCESS || func == 0 || !func->IsReadOnly() )
		TEST_FAILED;
	if( func )
		func->Release();
	bout.buffer = "";
	r = mod->CompileMethod(foo, "method", "void set() const { v = 1; }", 0, &func);
	if( r >= 0 || func != 0 )
		TEST_FAILED;

	// A compile error returns no function
	bout.buffer = "";
	func = (asIScriptFunction*)1;
	r = mod->CompileMethod(foo, "method", "int get() { return missing; }", 0, &func);
	if( r != asERROR || func != 0 )
		TEST_FAILED;
	if( bout.buffer.find("missing") == std::string::npos )
		TEST_FAILED;

	// Constructors need the class declaration's member initializers
	r = mod->CompileMethod(foo, "method", "Foo() { v = 1; }", 0, &func);
	if( r != asNOT_SUPPORTED || func != 0 )
		TEST_FAILED;
	r = mod->CompileMethod(foo, "method", "~Foo() {}", 0, &func);
	if( r != asNOT_SUPPORTED || func != 0 )
		TEST_FAILED;

	// Only one function
	bout.buffer = "";
	r = mod->CompileMethod(foo, "method", "int a() { return 1; } int b() { return 2; }", 0, &func);
	if( r != asERROR || func != 0 )
		TEST_FAILED;

	// Only a script class that this module declares
	r = mod->CompileMethod(0, "method", "int get() { return 1; }", 0, &func);
	if( r != asINVALID_TYPE || func != 0 )
		TEST_FAILED;
	r = mod->CompileMethod(mod->GetTypeInfoByName("I"), "method", "int get() { return 1; }", 0, &func);
	if( r != asINVALID_TYPE || func != 0 )
		TEST_FAILED;
	engine->RegisterObjectType("Native", 4, asOBJ_VALUE | asOBJ_POD);
	r = mod->CompileMethod(engine->GetTypeInfoByName("Native"), "method", "int get() { return 1; }", 0, &func);
	if( r != asINVALID_TYPE || func != 0 )
		TEST_FAILED;
	asIScriptModule *other = engine->GetModule("other", asGM_ALWAYS_CREATE);
	other->AddScriptSection("s", "class Bar { int w; }");
	r = other->Build();
	if( r < 0 )
		TEST_FAILED;
	r = mod->CompileMethod(other->GetTypeInfoByName("Bar"), "method", "int get() { return w; }", 0, &func);
	if( r != asINVALID_TYPE || func != 0 )
		TEST_FAILED;

	// The caller's reference keeps the type alive after the module is discarded
	func = 0;
	r = mod->CompileMethod(foo, "method", "int get() { return v; }", 0, &func);
	if( r != asSUCCESS || func == 0 )
		TEST_FAILED;
	mod->Discard();
	engine->GarbageCollect();
	if( func )
	{
		if( std::string(func->GetObjectName()) != "Foo" )
			TEST_FAILED;
		func->Release();
	}

	engine->ShutDownAndRelease();

	return fail;
}

} // namespace
