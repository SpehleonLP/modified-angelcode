/**

\page doc_adv_memory_access Memory access scopes

Every function that the engine knows has a pair of scopes, one for the memory it reads and one for the memory 
it writes. A host that runs several script calls at the same time can use them to decide which calls may overlap. 
The scopes are \ref asEMemoryAccess values. Read them with \ref asIScriptFunction::GetMemoryAccess.

The engine works out the scopes of script functions itself, when a module is built or loaded. The application declares 
the scopes of the functions that it registers.

\section doc_adv_memory_access_scopes The scopes

The scopes form a ladder. Each scope contains every scope below it.

<table border=0 cellspacing=0 cellpadding=0>
<tr><td width=150 valign=top>\ref asMA_NONE</td><td>Arguments and local variables only.</td></tr>
<tr><td valign=top>\ref asMA_WORLD_STABLE</td><td>State that may change, but never while the VM runs, e.g. configuration or loaded tables. 
 It can only be declared for application functions, and only as a read scope. It is never inferred.</td></tr>
<tr><td valign=top>\ref asMA_THIS</td><td>The object that the method is called on, and its value members.</td></tr>
<tr><td valign=top>\ref asMA_OWNED</td><td>Objects reached from the object through members of reference types that are not handles, to any depth.</td></tr>
<tr><td valign=top>\ref asMA_MODULE</td><td>The global variables of the function's own module.</td></tr>
<tr><td valign=top>\ref asMA_ENGINE</td><td>The registered global properties and the other state of the engine, including what shared entities share between modules.</td></tr>
<tr><td valign=top>\ref asMA_PROGRAM</td><td>Anything else, including everything reached through a handle.</td></tr>
<tr><td valign=top>\ref asMA_UNSET</td><td>An application function that has not been declared. It reads as \ref asMA_PROGRAM.</td></tr>
</table>

Objects reached through a handle are \ref asMA_PROGRAM, because the engine cannot know where the object came from. 
Because the scopes form a ladder, a function that reads \ref asMA_MODULE may also read everything in \ref asMA_THIS 
and \ref asMA_OWNED, and every scope above \ref asMA_WORLD_STABLE contains \ref asMA_WORLD_STABLE. 
A function declared as <tt>{asMA_NONE, x}</tt> and one declared as <tt>{asMA_WORLD_STABLE, x}</tt> therefore 
give the same answer to both questions below.

The analysis only ever widens. When it cannot tell what a function does, e.g. for an unknown bytecode instruction, 
the function is given \ref asMA_PROGRAM for both scopes.

\section doc_adv_memory_access_declare Declaring the scopes of application functions

Register the function as usual, then call \ref asIScriptFunction::SetMemoryAccess on it.
Do this after the registration and before building any script, because the analysis of the scripts uses the declared scopes.

\code
int id = engine->RegisterGlobalFunction("double sqrt(double)", asFUNCTION(sqrt), asCALL_CDECL);
engine->GetFunctionById(id)->SetMemoryAccess(asMA_NONE, asMA_NONE);
\endcode

\ref asIScriptFunction::SetMemoryAccess fails for script functions, for \ref asMA_UNSET as an argument, 
for \ref asMA_WORLD_STABLE as the write scope, and for \ref asMA_THIS or \ref asMA_OWNED on a function that has 
no object, i.e. a global function, a factory, a list factory or a template callback.

A function that is not declared reports \ref asMA_UNSET, which a host must treat as \ref asMA_PROGRAM. 
The host can look for undeclared functions by testing for it.

The engine declares the scopes of its own built-in behaviours, e.g. those of script objects and function definitions.

A template instance copies the scopes of its template when it is created, so declare the template's 
functions before the instances are made. The instance's destruction also covers the destruction of the 
object and handle subtypes of the instance, as each of them can run a script destructor.

\section doc_adv_memory_access_questions What the scopes answer

\subsection doc_adv_memory_access_conflict Can two calls run in parallel?

Two calls conflict when the write scope of one overlaps the read or write scope of the other.

 - \ref asMA_NONE and \ref asMA_WORLD_STABLE overlap nothing.
 - \ref asMA_THIS and \ref asMA_OWNED overlap only when both calls are made on the same object. The caller knows which object that is.
 - \ref asMA_MODULE overlaps only within the same module.
 - \ref asMA_ENGINE overlaps only within the same engine.
 - \ref asMA_PROGRAM overlaps everything.

\subsection doc_adv_memory_access_store Can the result of a call be stored?

The result of a call can be stored and reused instead of calling again, e.g. <tt>sqrt(2)</tt>, when all of these hold:

 - the read scope is at most \ref asMA_WORLD_STABLE,
 - the write scope is \ref asMA_NONE,
 - the return type and every parameter are values, not handles or references.

A function that returns a handle can be <tt>{asMA_NONE, asMA_NONE}</tt>, but each call returns a distinct object, 
so the result cannot be shared.

The host must discard stored results when it changes world-stable state. It may only change that state while the VM is stopped.

\section doc_adv_memory_access_params Parameters

A parameter of a reference type that is passed by value or as a non-const <tt>&in</tt> counts as \ref asMA_PROGRAM, 
because an argument that is already a temporary is passed without being copied, and a temporary handle's object may be shared. 
A <tt>const &in</tt> parameter of an object type also counts as \ref asMA_PROGRAM, as the compiler passes it without copying. 
Value types passed by value, as <tt>&out</tt> or as a non-const <tt>&in</tt>, and primitives passed as <tt>&in</tt>, count as \ref asMA_NONE.

A function that returns a reference is treated as reading the memory that the reference points into.

\section doc_adv_memory_access_contract What the host must guarantee

The analysis depends on the application functions behaving as declared.

 - A factory, list factory or copy factory returns a fresh object that nothing else references. The caller treats the new object 
   as its own, so a factory that returns a shared object breaks the analysis.
 - An \ref asBEHAVE_ADDREF behaviour touches only the reference count of the object. The analysis never consults it.
 - The read scope of a function covers the memory that any reference returned by the function points into. 
   Taking an address reads nothing, so the analysis cannot see this for an application function.

\section doc_adv_memory_access_outside What the analysis does not cover

 - The \ref doc_gc "garbage collector". It is host code that the VM runs on the host's behalf, and it can destroy any object.
 - The \ref asIScriptContext::SetLineCallback "line callback". It is called at each suspend point in the script.
 - A host that changes world-stable state while the VM runs.

The host must not run the garbage collector, or a line callback that touches state visible to scripts, at the same time 
as calls that it has scheduled in parallel.

\section doc_adv_memory_access_save Saved bytecode

The scopes of the script functions are stored in the bytecode saved by \ref asIScriptModule::SaveByteCode, one byte for 
each script function and each virtual or interface method. Bytecode saved by a library without this feature cannot be loaded.
When a library built with the compiler loads bytecode it computes the scopes again, and a library built without the compiler 
uses the stored values.

\see \ref doc_adv_concurrent, \ref doc_adv_multithread, \ref doc_gc







*/
