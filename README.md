# AngelScript 2.38.0 + memory access scopes

This branch is a patch on **vanilla AngelScript 2.38.0**. With it, every function records which memory it may
read and which it may write. It is written so it can be offered upstream to AngelScript's author, so it
is based on the plain 2.38.0 release (`upstream-2.38.0`) and does not include the fork's other changes.
Those are on `master`. This README is not part of the patch.

## Why this exists

This is step 1 of a series. The series aims to split compiled AngelScript bytecode into
something like a `.text` segment (logic) and a `.data` segment (records).

Today a literal such as `array<int> a = {1, 2, 3, 4};` exists only as instructions that
build the array on every call. Data and logic are mixed together in the bytecode. Hot
reload is stuck on this: when a script changes, the engine cannot tell whether a record
changed or the logic that produces it did.

To pull data out of code, the compiler first has to know which code is safe to run ahead of
time, which means code that reads nothing that can change and writes nothing. This branch
adds that knowledge.

| Step | Branch | What it does | State |
|------|--------|--------------|-------|
| 1 | `memory-access` (this one) | Every function gets a read scope and a write scope | Done; merged into the fork's `master` |
| 2 | [`pure-call-folding`](https://github.com/SpehleonLP/modified-angelcode/tree/pure-call-folding) | The compiler evaluates calls to pure natives with constant arguments | Done; merged into the fork's `master` |
| 3 | not started | A `.data` segment for records (strings, arrays, value types) | Open design question, see below |
| 4 | not started | Folding pure *script* functions | After step 3 |

## What it adds

### Scopes

`asEMemoryAccess` is a ladder, and each scope contains every scope below it:

| Scope | Means |
|-------|-------|
| `asMA_NONE` | Locals, and the argument objects of a native (the caller is charged for those) |
| `asMA_WORLD_STABLE` | State that changes only while the VM is stopped (only valid as a native's read scope) |
| `asMA_THIS` | The object the method is called on |
| `asMA_OWNED` | Objects reached from it through non-handle members |
| `asMA_MODULE` | The module's globals and its non-shared script classes |
| `asMA_ENGINE` | Registered properties, shared state, shared classes and other modules' classes |
| `asMA_PROGRAM` | Anything else, including anything the analysis cannot see through |

`asMA_UNSET` marks a native that nobody declared. It reads as `asMA_PROGRAM`, so an undeclared native fails closed.

The scopes answer two questions:

- **Can calls F and G run at the same time?** No, if one call's write scope overlaps the other call's read or write scope.
- **Can a call's result be computed once and stored?** Yes, if it reads at most
  `asMA_WORLD_STABLE`, writes nothing, and passes and returns only values.

The patch only provides the facts. It adds no scheduler and no result cache.

### API

```cpp
int  asIScriptFunction::SetMemoryAccess(asEMemoryAccess read, asEMemoryAccess write);
void asIScriptFunction::GetMemoryAccess(asEMemoryAccess *read, asEMemoryAccess *write) const;
```

The host declares each native after registering it and before building scripts:

```cpp
int id = engine->RegisterGlobalFunction("double sqrt(double)", asFUNCTION(MySqrt), asCALL_CDECL);
engine->GetFunctionById(id)->SetMemoryAccess(asMA_NONE, asMA_NONE);
```

`Register*` signatures do not change. The engine declares its own built-in behaviours itself.

### Analysis

Script functions are not declared. Their scopes come from a forward pass over the bytecode,
in `as_memoryaccess.{h,cpp}`:

- The pass tracks where every pointer came from.
- A call adds the callee's scopes.
- A call to a native also charges the caller for each argument passed by reference or handle.
- A driver in `asCModule::ComputeTransitiveFunctionMetadata` repeats the pass until nothing changes.

The pass runs after `Build`, after `LoadByteCode`, in `CompileFunction` and in `CompileGlobalVar`.
Every approximation widens a scope; nothing narrows one. Anything the pass does not model
becomes `{Program, Program}`, including unknown opcodes, variadic callees, funcdef and
delegate calls, and imported functions.

### What the host must guarantee

The full list is in the documentation page `doc_adv_memory_access`
(`sdk/docs/doxygen/source/doc_adv_memory_access.h`). The main points:

- A native's declared scopes cover everything it reaches beyond its argument objects.
- Factories return fresh objects.
- Release and destructor behaviours declare the worst case of destroying the object.
- A host that schedules calls in parallel turns off `asEP_AUTO_GARBAGE_COLLECT` and runs the
  collector itself, at a time when no scheduled call is running.

### Other changes

- **Bytecode format.** Saved bytecode carries one extra scope byte per script function and per
  virtual entry. Bytecode saved by 2.38.0 does not load. The expected CRCs and sizes in
  `test_saveload`, `test_shared` and `test_getset` were updated to match.
- **Add-ons.** Every add-on function declares its scopes.
- **Dictionary iterators.** They now check which dictionary owns them. Passing one dictionary's
  iterator to another used to crash; it now raises a script exception.

## Trying it

```sh
cmake -S sdk/tests/test_feature/projects/cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd sdk/tests/test_feature/bin && ./test_feature
```

The test for this branch is `sdk/tests/test_feature/source/test_memoryaccess.cpp`. The full suite
passes with it.

## What is not done

- The xcode project is not updated.
- 32-bit builds are designed for but untested.
- No committed test runs with `AS_NO_COMPILER`.
- `weakref<T>` reports more than it does (the destruction of `T`, which it never owns).
- Configs written by `WriteConfigToStream` carry no scopes, so an engine configured from one treats every native as `Program`.

## The open question for step 3

The next step is a `.data` segment, and it is not designed yet. The hard question is how
records are named. Hot reload has to answer "what changed?". If records were named by their
position, adding one record would renumber every record after it, and the diff would be
noise. So a record needs an identity that survives unrelated edits. Two candidates:

- **Where it is declared.** The diff then reads "this declaration's value changed".
- **Its content.** The diff then reads "this value disappeared, that one appeared".

Which one to use, or how to combine them, is still undecided.
