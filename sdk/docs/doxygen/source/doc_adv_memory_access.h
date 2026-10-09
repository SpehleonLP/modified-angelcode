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
<tr><td width=150 valign=top>\ref asMA_NONE</td><td>Local variables, and the objects passed as arguments to an application function: the caller counts what the 
 function can reach through its arguments, see \ref doc_adv_memory_access_args. The parameters of a script function are 
 counted as described in \ref doc_adv_memory_access_params.</td></tr>
<tr><td valign=top>\ref asMA_WORLD_STABLE</td><td>State that may change, but never while the VM runs, e.g. configuration or loaded tables. 
 It can only be declared for application functions, and only as a read scope. It is never inferred.</td></tr>
<tr><td valign=top>\ref asMA_THIS</td><td>The object that the method is called on, and its value members.</td></tr>
<tr><td valign=top>\ref asMA_OWNED</td><td>Objects reached from the object through members of reference types that are not handles, to any depth.</td></tr>
<tr><td valign=top>\ref asMA_MODULE</td><td>The global variables of the function's own module, and the objects of the module's own script classes 
 that are not shared, wherever they are reached from. See \ref doc_adv_memory_access_home.</td></tr>
<tr><td valign=top>\ref asMA_ENGINE</td><td>The registered global properties and the other state of the engine, including what shared entities share between modules, 
 and the objects of shared script classes and of the script classes of other modules.</td></tr>
<tr><td valign=top>\ref asMA_PROGRAM</td><td>Anything else, including the objects of registered types reached through a handle or a reference, 
 function handles and delegates, <tt>?</tt> arguments, anything of a type the analysis does not know, and an object 
 whose type differs between the paths of the function that reach it.</td></tr>
<tr><td valign=top>\ref asMA_UNSET</td><td>An application function that has not been declared. It reads as \ref asMA_PROGRAM.</td></tr>
</table>

Because the scopes form a ladder, a function that reads \ref asMA_MODULE may also read everything in \ref asMA_THIS 
and \ref asMA_OWNED, and every scope above \ref asMA_WORLD_STABLE contains \ref asMA_WORLD_STABLE. 
A function declared as <tt>{asMA_NONE, x}</tt> and one declared as <tt>{asMA_WORLD_STABLE, x}</tt> therefore 
give the same answer to both questions below.

The analysis only ever widens. When it cannot tell what a function does, e.g. for an unknown bytecode instruction, 
the function is given \ref asMA_PROGRAM for both scopes.

\subsection doc_adv_memory_access_home Objects reached through handles

The engine cannot know where an object reached through a handle came from. Code can only touch a member of a script object 
by naming it, though, so such an access is counted at the home of the class that declares what is named:

 - A field access is counted at the home of the class that declares the field. For an inherited field that is the base class 
   that declares it.
 - A method call is counted as a call on an object at the home of the class that declares the method, for each method the call 
   may run.
 - Any other use of the object as a whole, e.g. passing it to an application function, is counted at the home of its type. A 
   script class counts as shared when any class or interface it derives from or implements is shared, and so does a class or 
   interface of the module when any class of the module that derives from it or implements it does.

The home of a class, seen from a function of a module, is \ref asMA_MODULE for a script class or interface of that module that 
is not shared, \ref asMA_ENGINE for a shared script class or interface or one of another module, and \ref asMA_PROGRAM for 
a registered type, a function definition, or a type that is not known.

For example, with <tt>class In { int v; } In@ gh;</tt> a function that does <tt>gh.v = 1;</tt> writes \ref asMA_MODULE. 
If <tt>In</tt> were shared it would write \ref asMA_ENGINE, and if <tt>In</tt> were a registered type it would write 
\ref asMA_PROGRAM.

This is sound because another module can only reach an object of this module's classes through a shared class or 
interface or a <tt>?</tt>, all of which are \ref asMA_ENGINE or wider in that module, and \ref asMA_ENGINE overlaps 
every module of the engine. The host's own calls on such an object, e.g. a method that reports \ref asMA_THIS, are covered 
by the rule for \ref asMA_THIS and \ref asMA_OWNED in \ref doc_adv_memory_access_conflict.

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

Registered template functions, e.g. <tt>void f<T>(const T &in)</tt>, cannot be declared, and 
\ref asIScriptFunction::SetMemoryAccess returns \ref asNOT_SUPPORTED for them. Their instances report \ref asMA_UNSET, 
so a script function that calls one is given \ref asMA_UNSET for both scopes.

\subsection doc_adv_memory_access_args Arguments to application functions

The engine cannot see what an application function does with its arguments, so it counts them on the side of the 
caller. For every argument that is passed by reference as <tt>&in</tt> or <tt>&inout</tt>, by handle, or by value of 
a reference type, the call reads the memory that the argument refers to, at the scope that the argument has in the caller. 
It also writes that memory, unless that memory is const: a const object, e.g. <tt>const obj &in</tt>, or for a reference 
to a handle a const handle, e.g. <tt>obj@ const &in</tt>. A handle to a const object, e.g. <tt>const obj@ &inout</tt>, 
does not protect the handle itself, which the function may still reassign. For a reference to a handle the object that the 
handle refers to is counted too, and written unless it is a const object. An <tt>&out</tt> argument is a temporary of the caller, and a value type passed by value is a copy, 
so neither is counted.

The declared scopes therefore only need to cover what the function reaches beyond the objects passed as arguments, e.g. 
through a handle stored inside an argument, or by destroying an object that it releases.

\code
// Writes its argument and nothing else
int id = engine->RegisterGlobalFunction("void touch(obj &inout)", asFUNCTION(Touch), asCALL_CDECL);
engine->GetFunctionById(id)->SetMemoryAccess(asMA_NONE, asMA_NONE);
\endcode

With this declaration a script function that calls <tt>touch(g)</tt> on a global variable <tt>obj g</tt> of its own 
module reads and writes \ref asMA_MODULE, and one that calls <tt>touch(h)</tt> on a global handle <tt>obj@ h</tt> 
reads and writes \ref asMA_PROGRAM.

The engine declares the scopes of its own built-in behaviours, e.g. those of script objects and function definitions.

A template instance copies the scopes of its template when it is created, so declare the template's 
functions before the instances are made. The instance's destruction also covers the destruction of the 
object and handle subtypes of the instance, as each of them can run a script destructor.

\section doc_adv_memory_access_questions What the scopes answer

\subsection doc_adv_memory_access_conflict Can two calls run in parallel?

Two calls conflict when the write scope of one overlaps the read or write scope of the other.

 - \ref asMA_NONE and \ref asMA_WORLD_STABLE overlap nothing.
 - \ref asMA_THIS and \ref asMA_OWNED of a call on object X, and of a call on object Y, overlap when X is Y, or when 
   either object can be reached from the other through members that are not handles, e.g. when the host holds a handle 
   to the member <tt>outer.inner</tt> and calls methods on both <tt>outer</tt> and that member. 
   A host that cannot tell must treat them as overlapping.
 - \ref asMA_THIS and \ref asMA_OWNED of a call on object X overlap \ref asMA_MODULE of a module when X, or any object 
   that can be reached from X through members that are not handles, is of a script class of that module that is not shared, 
   wherever it is held, or when X is held in one of the module's global variables. They overlap \ref asMA_ENGINE of an engine 
   whenever X is a script object of that engine. A host that cannot tell must treat them as overlapping. 
   For example, with <tt>class E { int v; void m() { v++; } }</tt> and <tt>void param(E@ e) { e.v = 1; }</tt> in a module, 
   <tt>param</tt> is <tt>{asMA_NONE, asMA_MODULE}</tt> and <tt>E::m</tt> is <tt>{asMA_THIS, asMA_THIS}</tt>. 
   Called on the same object they conflict, even when no global variable holds it.
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

These rules apply to the parameters of script functions. The arguments of application functions are counted by the 
caller, see \ref doc_adv_memory_access_args.

A parameter of a reference type that is passed by value or as a non-const <tt>&in</tt> is an object held anywhere, 
because an argument that is already a temporary is passed without being copied, and a temporary handle's object may be shared. 
A <tt>const &in</tt> parameter of an object type is also an object held anywhere, as the compiler passes it without copying. 
So are handles and <tt>&inout</tt> references. Accesses to such an object are counted at the home of its type, see 
\ref doc_adv_memory_access_home. The handle variable behind a reference to a handle, other than <tt>&out</tt>, and 
<tt>?</tt> parameters count as \ref asMA_PROGRAM. 
Value types passed by value or as a non-const <tt>&in</tt>, primitives passed as <tt>&in</tt>, and any type passed as 
<tt>&out</tt>, count as \ref asMA_NONE, as the function only sees its own copy or a temporary of the caller.

A function that returns a reference is treated as reading the memory that the reference points into.

A handle returned by a function, other than a factory, is an object held anywhere, and is counted at the home of its type. 
A factory, list factory or copy factory returns a fresh object, which the caller treats as its own.

\section doc_adv_memory_access_contract What the host must guarantee

The analysis depends on the application functions behaving as declared.

 - A factory, list factory or copy factory returns a fresh object that nothing else references. The caller treats the new object 
   as its own, so a factory that returns a shared object breaks the analysis.
 - The declared scopes cover everything the function reaches beyond the objects passed as arguments, including 
   objects reached through handles stored inside the arguments.
 - An \ref asBEHAVE_ADDREF behaviour touches only the reference count of the object. The analysis never consults it.
 - An \ref asBEHAVE_RELEASE behaviour of a reference type, and the \ref asBEHAVE_DESTRUCT behaviour of a value type, 
   declare what destroying the object can do in the worst case. That includes releasing everything that the object holds, 
   which may in turn destroy those objects. A container that may hold script objects or delegates must declare 
   \ref asMA_PROGRAM. A template type is the exception for its subtypes: the analysis adds the destruction of the object 
   and handle subtypes of each instance itself.
 - The read scope of a function covers the memory that any reference returned by the function points into. 
   Taking an address reads nothing, so the analysis cannot see this for an application function.
 - A reference into world-stable state that a function returns must be const. The analysis counts nothing for the
   memory behind such a reference, as that state does not change while the VM runs.
 - The resolve function given to \ref asIScriptEngine::RegisterHandle "RegisterHandle" reads only state that
   functions with a write scope of \ref asMA_PROGRAM change, such as the factories and releases that create and
   destroy what the handles name. The analysis counts nothing for resolving a handle or testing it for null, and puts
   the resolved object at the home of its type.

\section doc_adv_memory_access_outside What the analysis does not cover

 - The \ref doc_gc "garbage collector". It is host code that the VM runs on the host's behalf, and it can destroy any object.
 - The \ref asIScriptContext::SetLineCallback "line callback". It is called at each suspend point in the script.
 - The \ref asIScriptEngine::SetScriptObjectUserDataCleanupCallback "user data clean-up callback" of script objects, 
   which runs when a script object is destroyed.
 - The \ref asIScriptContext::SetExceptionCallback "exception callback" and the 
   \ref asIScriptEngine::SetTranslateAppExceptionCallback "exception translation callback".
 - A host that releases script objects, or that changes world-stable state, while the VM runs.

The host must not run any of these, when they touch state visible to scripts, at the same time as calls that it has 
scheduled in parallel.

Automatic garbage collection is the one that is easy to miss. While \ref asEP_AUTO_GARBAGE_COLLECT is on, which is the 
default, creating any garbage collected object, whether it is a script object or an application type, can run a step of 
the garbage collector inside that call. The step can destroy garbage and run script destructors, whatever the scopes 
of the function that created the object say. A host that runs calls in parallel on the strength of the scopes must set 
\ref asEP_AUTO_GARBAGE_COLLECT to false, and run the garbage collector itself at a time when no scheduled calls are running.

\section doc_adv_memory_access_save Saved bytecode

The scopes of the script functions are stored in the bytecode saved by \ref asIScriptModule::SaveByteCode, one byte for 
each script function and each virtual or interface method. Bytecode saved by a library without this feature cannot be loaded.
When a library built with the compiler loads bytecode it computes the scopes again, and a library built without the compiler 
uses the stored values. The stored values describe the application functions as the saving engine declared them, so an 
application that loads bytecode without the compiler must declare its functions the same way as the one that saved it. 
Nothing detects a difference.

\see \ref doc_adv_concurrent, \ref doc_adv_multithread, \ref doc_gc







*/
