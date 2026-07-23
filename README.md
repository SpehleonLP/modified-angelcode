# modified-angelcode

A modified fork of [AngelScript 2.38.0](https://www.angelcode.com/angelscript/), the
embeddable C++ scripting language by Andreas Jönsson. This fork powers the scripting
layer of a private game engine (Kreatures / Lifaundi). Full credit for the base
implementation goes to upstream AngelScript; this repository tracks a specific set of
engine-driven deviations on top of it.

## Changes vs upstream

The fork history is split into two layers (see Branch Layout below for the commits
this maps to). Grouped by theme, citing the primary files:

### Long-lived fork modifications

- **Handle-resolve system** — a new registration API, `asIScriptEngine::RegisterHandle(typeName, asRESOLVEHANDLEFUNC_t, userData, dead)`
  (`sdk/angelscript/include/angelscript.h`, `sdk/angelscript/source/as_scriptengine.cpp`),
  lets an application register a ref type whose "pointer" is really an opaque
  handle that must be resolved to a real object before any addref/release/GC
  call. `asCTypeInfo` grows `resolveHandle` / `handleUserData` / `deadHandle`
  fields (`as_typeinfo.h`); `asCScriptEngine::ResolveForRefCount()` is the
  single choke point every refcounting call site (`as_scriptobject.cpp`,
  `as_callfunc.cpp`, `as_generic.cpp`, `as_module.cpp`, `as_gc.cpp`) now
  routes through. Two death conventions are supported: a shared "dead handle"
  sentinel (nocount, skips addref/release), or a plain null return ("Erlang
  mode" — the null pointer exception fires at access time, not refcount
  time). Registering a non-const property or a value type against a
  handle-resolve type is rejected at registration time. The VM gets four new
  opcodes — `asBC_ResolveHandleV`, `asBC_PshHandlePtr`, `asBC_LoadHRObjR`,
  `asBC_IsHandleNull` (`asBC_MAXBYTECODE` raised 201→205) — and the bytecode
  optimizer (`as_bytecode.cpp`) fuses common PSF/RDSPtr/CHKREF/ADDSi sequences
  into them.
- **Access-mask usage propagation** — `asCScriptFunction` grows
  `minLocalAccessMask` / `minTransitiveAccessMask` (exposed via
  `GetMinLocalAccessMask()` / `GetMinTransitiveAccessMask()`), a separate
  accumulator from the vanilla `accessMask` visibility field: it tracks which
  registered-type access-mask categories a function's body and call graph
  actually *touch*, so an application can ask "does calling this function
  ever exercise category X" rather than just "is this function visible under
  mask X". `asCScriptEngine::GetDefaultAccessMask()` was added as the missing
  read side of `SetDefaultAccessMask()`. Script-defined classes/enums are
  explicitly excluded from the accumulation (their default 0xFFFFFFFF is
  builder visibility noise, not a runtime category) — see the comments in
  `as_builder.cpp` and `as_scriptengine.cpp` around
  `GetTemplateInstanceType`/`GenerateFactoryStubForTemplateObjectInstance`.
  Two new diagnostics report *why* a symbol was hidden by mask
  (`TXT_NO_MATCHING_SYMBOL_s_MASK_x_vs_x`, `TXT_s_NOT_MEMBER_OF_s_MASK_x_vs_x`
  in `as_texts.h`).
- **Halting-analysis scaffolding** (API surface only — the analysis itself is
  the 2026-07-22 layer below) — the `asEHalts` enum and per-function
  `GetLocalHalts()` / `GetTransitiveHalts()` / `GetLocalCallsDelegate()` /
  `GetTransitiveCallsDelegate()` accessors, plus `asCModule::BuildCalleeList()`
  and `ComputeTransitiveFunctionMetadata()` (a fixed-point call-graph pass
  invoked at the end of `asCModule::Build()`), were introduced here.
- **Template-callback and array internals rework** (`sdk/add_on/scriptarray`,
  documented in full in `sdk/patch.md`) — `CScriptArray` gains an
  `isValueType` fast path for POD element storage (`elementSize` narrowed to
  `int16_t`), an externally-supplied `ScriptArrayTemplateCallback2` replaces
  the built-in template callback, and `Construct`/`Destruct`/`At`/assignment/
  `EnumReferences` are guarded so POD arrays skip refcounting entirely. The
  engine-side counterpart (`ScriptArrayTemplateCallback2`,
  `as_arraysupport.h`) lives outside this SDK, in the Kreatures engine tree.
- **`CScriptAny` module-linkage removal** (`sdk/add_on/scriptany`) — guarded by
  `REMOVE_MODULE_LINKAGE`, adds a static `RemoverModule` / `Depends` pair so a
  `CScriptAny` instance can outlive the script module it was created from
  during hot-reload, instead of holding a module reference.
- **Misc add-on fixes** — `CScriptDictionary` gains `GetEngine()` and a cached
  `valueType`; `CScriptWeakRef::Equals` treats a destroyed target as equal to
  null; `CScriptSocket` drops its Windows-only guard and a `Close()` polarity
  bug (`m_socket == -1` → `!= -1`) is fixed; `CDateTime` gains a
  `time_point` constructor. Full itemized diffs are in `sdk/patch.md` and
  `sdk/CHANGES_FROM_STOCK.md` (the latter is a point-in-time snapshot from an
  earlier iteration of this work and predates the current bytecode-opcode
  scheme — treat it as historical context, not current truth).
- **Compiler/engine hardening** — a template-type constructor whose first
  parameter is the object type itself (missing the hidden subtype parameter)
  is now rejected at registration (`TXT_TEMPLATE_CTOR_FIRST_PARAM_MUST_BE_SUBTYPE`)
  instead of being silently misclassified as a default constructor;
  `asCModule::GetFunctionByDecl()` reports invalid declarations to the message
  stream instead of silently dropping them; a registered object property on a
  handle-resolve type must be const or absent.
- **`hasReturn` threaded through loops** (`as_compiler.cpp`, `CompileStatement`/
  `CompileWhileStatement`/`CompileDoWhileStatement`/`CompileForStatement`/
  `CompileBreakStatement`) — a non-void function ending in `while(true){...}`,
  `do{...}while(true);`, or `for(;;){...}` no longer wrongly demands a trailing
  `return`: a loop whose guard is a compile-time-constant true (or, for `for`,
  absent) and whose body never reaches a `break` is recognized as making the
  code after it unreachable. As a consequence, code that follows such a
  provably-infinite loop now produces an "Unreachable code" warning where it
  previously produced none. Confirmed present in vanilla upstream 2.38.0
  (`next-version` branch, same `CompileStatement`) — worth reporting upstream.
- Callback signature changes: `SetTranslateAppExceptionCallback`,
  `asIScriptContext::SetExceptionCallback`/`SetLineCallback` take `asSFuncPtr`
  by value instead of `const asSFuncPtr&`.
- Purpose unclear from diff / not investigated further: assorted copyright-year
  and warning-suppression pragmas noted in `sdk/patch.md`
  (`as_callfunc_x64_gcc.cpp`, `as_typeinfo.cpp`, `scriptstdstring.cpp`) — no
  behavioral effect, listed there for completeness when re-applying to a new
  upstream release.

### Halting analysis (2026-07-22 hardening)

The flagship fork feature. `asIScriptFunction::GetLocalHalts()` /
`GetTransitiveHalts()` return an `asEHalts` verdict — `asHALTS_YES=0 <
asHALTS_UNKNOWN=1 < asHALTS_NO=2`, ordered as a lattice so a `max()` fold
across callees is a sound join. The engine uses this to gate which script
functions may run in constrained contexts (two independent consumers: GUI
lambda admission gates on `GetLocalHalts()`, and a `CanCallMethod` utility
gates on `GetTransitiveHalts()`) — a function can only be trusted with
`YES`; `UNKNOWN` and `NO` are both refused.

This layer (isolate with `git diff our-upstream~1 our-upstream` /
`git diff 0de438f 91f4c2d` — note most of the `sdk/add_on/*` churn in that
range is a line-ending artifact of the vanilla zip vs the fork tree, not
real change; the substantive edits are `as_compiler.cpp`, `as_module.cpp`,
`as_scriptfunction.cpp`) made that analysis sound rather than merely
best-effort:

- **UNKNOWN resting default** — `asCScriptFunction`'s constructor now
  initializes `localHalts`/`transitiveHalts` to `asHALTS_UNKNOWN` for every
  function type except `asFUNC_SYSTEM` (registered natives are trusted by
  the application). Previously it defaulted to `YES`, which meant a function
  whose analysis never ran (e.g. a bare `CompileFunction` product) silently
  claimed the strongest guarantee it never earned.
- **Local analysis** (`as_compiler.cpp`) is a fail-closed CFG walk over the
  finalized bytecode: `YES` only if there's no cycle, or every cycle is
  proven bounded; `NO` only if there is an unbounded cycle, no try/catch, and
  no `RET` reachable from entry; everything else — including anything the
  decoder can't classify — is `UNKNOWN`. It recognizes `asBC_JLowZ`/
  `asBC_JLowNZ` (the optimizer's ClrHi+JZ/JNZ fusion) and `asBC_JMPP` (switch
  dispatch) as conditional/computed jumps, and elides constant-guard edges
  (literal `while(true)` isn't treated as escapable). A funcdef/delegate call
  site caps the local result at `UNKNOWN` since the callee is unknown.
- **Counted-loop prover** (`asProveCountedLoop` in `as_compiler.cpp`) upgrades
  provably-bounded loops from `UNKNOWN` to contributing toward `YES`: it
  recognizes both top-test (`CMP;Jcc-exit;...;IncVi;JMP-back`) and
  bottom-test (`...;IncVi;CMP;JS-back`) shapes, requires the loop variable be
  written only by the one sanctioned `IncVi` and never aliased (taking its
  address counts as a write), and enforces increment-dominance so a branch
  that could skip the increment refuses the proof — the two loop shapes need
  different dominance checks (global jump-target set for top-test; span-
  internal source check for bottom-test, since legitimate for-loop entry
  rotation jumps onto the bottom-test condition from outside the span).
- **NO never crosses a call edge** (`asCModule::ComputeTransitiveFunctionMetadata`,
  `as_module.cpp`) — a callee's `asHALTS_NO` is downgraded to `UNKNOWN` before
  folding into the caller, because whether a never-halting callee's call site
  is even reached is not something the call graph tracks; only a function's
  own control flow can earn it `NO`.
- **Delegate poisoning costs precision, not soundness** — any unresolved call
  edge (shared-interface dispatch, imported/bound functions, a map-miss on
  `CALL`/`ALLOC`) sets `localCallsDelegate`/`transitiveCallsDelegate`, and a
  function with that flag set can never report `transitiveHalts == YES`
  regardless of what its modeled edges say.
- **Virtual class-method resolution in `BuildCalleeList`** — `asBC_CALLINTF`
  is also emitted for `asFUNC_VIRTUAL` dispatch, not just interface calls;
  the fork now resolves it against every class in the module deriving from
  (or equal to) the static type, via each candidate's `virtualFunctionTable`
  at the same vtable slot, rather than falling through to delegate-poisoning
  for ordinary virtual calls.
- **Transitive delegate caps and CALL/ALLOC map-miss poisoning** — every call
  site whose target can't be resolved to an entry in the module's function
  map (not just the previously-handled cases) now sets the delegate flag
  instead of silently contributing no edge.

The full design/implementation record for this hardening pass lives in the
engine repository at
`docs/superpowers/plans/2026-07-22-as-halting-analysis-hardening.md`.

## Building and testing

This repo is a standalone CMake project (the vanilla `sdk/angelscript/projects/cmake`
subproject plus a `tests/` gtest suite; system GTest 1.14+ required):

```bash
cmake -B <build-dir> -S /mnt/Passport/Libraries/svn/angelscript-code -DCMAKE_BUILD_TYPE=Debug
cmake --build <build-dir> -j8
ctest --test-dir <build-dir> --output-on-failure
```

`<build-dir>` must live on a native (ext4) filesystem, not under `/mnt/Passport`
itself — that drive is NTFS via `ntfs-3g`/fuseblk, which cannot carry the
executable bit, so a build directory placed on it produces object/binary files
that fail to run (`ctest`/`gtest_discover_tests` sees "Permission denied").
This is the same constraint the engine's `scripts/wt-build.sh` works around by
building to `/home/anyuser/Developer/Build/...`; do the same here, e.g.
`/home/anyuser/Developer/Build/angelscript-fork`.

All `AsHalting.*` tests (currently 45) should pass; this is the suite every
later halting-analysis change adds to, in `tests/test_as_halting.cpp`.

## Branch layout

- **`master`** — the working fork. Edit, commit, and push here.
- **`our-upstream`** — the fork-changes line (vanilla baseline commit +
  hardening/remaining-deltas commit), merged into `master`. Kept around
  specifically so the fork can be diffed against a known baseline; do not
  develop directly on it except when carrying a new upstream release forward.
- **`next-version`** — parked at vanilla AngelScript 2.38.0 (commit `54b6fb4`).
  Reserved for the next upstream release drop.

### Pulling a new upstream release

1. Unpack the new release into the `next-version` worktree at
   `/mnt/Passport/Libraries/svn/next-version` and commit it there.
2. Diff/merge the new vanilla tree against `our-upstream` to carry the fork's
   modifications (this README's "Changes vs upstream" section, `sdk/patch.md`,
   and the halting-analysis hardening plan) forward onto the new base. The
   gtest suite cannot run against raw vanilla — vanilla lacks `GetLocalHalts`
   and the rest of the halting-analysis API the tests exercise — so build and
   run the suite (see "Building and testing" above) in the `our-upstream`
   worktree itself, after carrying the fork's changes onto the new base and
   before merging `our-upstream` into `master`.
3. Re-merge the updated `our-upstream` into `master`.

### Line endings

Sources are CRLF. The repository is configured with `core.autocrlf false` —
do not let git or an editor silently normalize line endings; changes should
stay byte-exact on lines you didn't intentionally touch.

## Trimmed from upstream

The `sdk/samples/*` sample programs and their project files (`asbuild`,
`asrun`, `concurrent`, `console`, `coroutine`, `events`, `game`, `include`,
`tutorial`) were deleted from the fork tree. They are not part of the
embeddable library and are not needed by the engine build.
