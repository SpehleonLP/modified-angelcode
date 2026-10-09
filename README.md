# AngelScript 2.38.0 + memory access scopes + pure-call folding

This branch is the second patch of a series. It is built on top of
[`memory-access`](https://github.com/SpehleonLP/modified-angelcode/tree/memory-access), which is based on
**vanilla AngelScript 2.38.0**. Like that branch, it is kept apart from the fork's other changes (those are on
`master`) so it can be offered upstream on its own. This README is not part of the patch.

## Why this exists

The series aims to split compiled AngelScript bytecode into something like a `.text` segment
(logic) and a `.data` segment (records).

Today, a value worked out from constants is recomputed every time the code runs. A literal
`array<int> a = {1, 2, 3, 4};` is rebuilt by instructions on every call. So the data lives
inside the logic, and hot reload cannot tell whether a script edit changed a record or the
code that produces it.

This branch is the first place where the compiler runs code ahead of time and keeps the
result. It covers only the simplest safe case: a call to a native function that touches
no memory and is given only constant arguments.

| Step | Branch | What it does | State |
|------|--------|--------------|-------|
| 1 | [`memory-access`](https://github.com/SpehleonLP/modified-angelcode/tree/memory-access) | Every function gets a read scope and a write scope | Done; merged into the fork's `master` |
| 2 | `pure-call-folding` (this one) | The compiler evaluates calls to pure natives with constant arguments | Done; merged into the fork's `master` |
| 3 | not started | A `.data` segment for records (strings, arrays, value types) | Open design question, see below |
| 4 | not started | Folding pure *script* functions | After step 3 |

## What it does

A host turns it on with an engine property. It is off by default:

```cpp
engine->SetEngineProperty(asEP_FOLD_PURE_CALLS, true);   // asEP_FOLD_PURE_CALLS = 41
```

The host then declares a native as pure, using the step 1 API:

```cpp
int id = engine->RegisterGlobalFunction("double sqrt(double)", asFUNCTION(MySqrt), asCALL_CDECL);
engine->GetFunctionById(id)->SetMemoryAccess(asMA_NONE, asMA_NONE);
```

From then on, `sqrt(2.0) * 3.0` compiles to one constant, with no call. The result is a real
compile-time constant, so it can go anywhere the language demands one:

```angelscript
const uint kApple = hashc("apple");          // a const global

int pick(const string &in s)
{
    switch( hashc(s) )                       // a switch on a string, through its hash
    {
    case hashc("apple"):  return 1;          // case labels must be constants
    case hashc("banana"): return 2;
    }
    return 0;
}
```

Default and named arguments are filled in before the fold. With
`int sum3(int a = 1, int b = 2, int c = 3)`, the call `sum3(b: 4)` folds to `143`.

### When a call folds

A call folds only when all of these hold:

- **The callee** is a registered global function declared `{asMA_NONE, asMA_NONE}`. These never fold:
  - methods, behaviours, factories and constructors
  - script functions, function handles and delegates
  - variadic functions
  - functions that read `asMA_WORLD_STABLE`
- **Every argument** is constant once converted: a primitive, an enum, or a string literal, passed
  by value, as `const &in`, or as `&in`. With `&in`, the native gets a copy, so it cannot alter the
  constant. A call with an `&out` or `&inout` parameter does not fold.
- **The result** is a primitive or an enum, returned by value.

### How it runs the call

- The compiler runs the native once, on a private context, using the same call path as at run
  time. So every calling convention works, including `asCALL_GENERIC`.
- The host's context-pool callbacks do not fire, and no garbage-collection step runs after the call.
- If the call raises a script exception or does not finish, it is not folded. It compiles as a
  normal call and raises at run time. Folding never turns a run-time error into a compile error.
- Saved bytecode holds the constant, so an engine loading it needs no folding support. The value
  is the one computed by the machine that compiled it.

### Why variadic functions are excluded

A variadic call passes a hidden argument count that the compiler pushes itself. The public
`Prepare`/`SetArg` interface the fold uses has no way to pass it. Supporting variadics would
need a hand-built call path, which better fits step 4.

### What the host promises

A native declared `{asMA_NONE, asMA_NONE}` must return the same result for the same arguments
and have no observable effect. If that declaration is wrong, the wrong value is baked into the
bytecode. The documentation page `doc_adv_memory_access` covers this in "Folding pure calls".

### Where the code is

- **The fold.** `asCCompiler::TryFoldCall` in `as_compiler.cpp`. It is called from `MakeFunctionCall`,
  after overload resolution and argument conversion, before the call is emitted. It sits in the
  front end and is not a bytecode peephole, because `case` labels and `const` initialisers need to
  know a value is constant while compiling.
- **The garbage-collection opt-out.** A private flag set by `asCContext::SuppressAutoGarbageCollect()`,
  in `as_context.{h,cpp}`.
- **The property.** `angelscript.h` and `as_scriptengine.{h,cpp}`.

The library change is about 300 lines.

## Trying it

```sh
cmake -S sdk/tests/test_feature/projects/cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd sdk/tests/test_feature/bin && ./test_feature
```

The test for this branch is `sdk/tests/test_feature/source/test_purefold.cpp`. Every case checks
both the value and the bytecode (whether the call is still there), and, where the native counts
its runs, that the folded code calls nothing at run time. Its groups:

- property on and off
- plain folds, conversions and nesting
- default and named arguments
- const globals used as `case` labels
- calls that must *not* fold
- argument widths
- save and load
- string literals, the string `switch` above, and an `&in` string the native overwrites
- no context callbacks and no garbage collection during a fold

Each guard was proved by a mutation: removing it turns a test red. The full suite passes in
Release and Debug.

## What is not done

- **Not folded yet.** Script functions, constants carried through variables, and results that
  are strings, arrays or value types. These need steps 3 and 4.
- **`const` string globals.** A `const string` global is a variable, not a literal, so a call on
  it does not fold.
- **Build files.** The xcode project is not updated.

## The open question for step 3

Step 3 lets the compiler keep a non-primitive result, like an array, as a record in a `.data`
segment instead of instructions. It is not designed yet.

The hard question is how records are named. Hot reload has to answer "what changed?". If
records were named by position, adding one record would renumber every record after it, and the
diff would be noise. So a record needs an identity that survives unrelated edits. Two candidates:

- **Where it is declared.** The diff then reads "this declaration's value changed".
- **Its content.** The diff then reads "this value disappeared, that one appeared".

Which one to use, or how to combine them, is still undecided.
