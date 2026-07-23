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
  site caps the local result at `UNKNOWN` since the callee is unknown — with
  one narrow, statically-provable exception: see "Const-global funcdef call
  resolution" below.
- **Counted-loop prover** (`asProveCountedLoop` in `as_compiler.cpp`) upgrades
  provably-bounded loops from `UNKNOWN` to contributing toward `YES`: it
  recognizes both top-test (`CMP;Jcc-exit;...;step;JMP-back`) and
  bottom-test (`...;step;CMP;(JS|JP)-back`) shapes, where `step` is one of
  `IncVi`/`DecVi`/`ADDIi`/`SUBIi` in self-update form. The mutator's polarity
  must match the exit test (`JNS`/`JS` — stay while v < w — pairs only with
  an increment; `JNP`/`JP` — stay while v > w — pairs only with a
  decrement); a step of magnitude > 1 is accepted only against a *constant*
  bound with enough headroom before signed-int wraparound (`i += 2` against
  a variable bound, or against `0x7fffffff` where the exit window is
  narrower than the step, is refused — both can loop forever). The loop
  variable must be written only by the one sanctioned step instruction and
  never aliased (taking its address counts as a write), and increment-
  dominance is enforced so a branch that could skip the step refuses the
  proof — the two loop shapes need different dominance checks (global
  jump-target set for top-test; span-internal source check for bottom-test,
  since legitimate for-loop entry rotation jumps onto the bottom-test
  condition from outside the span). The accepted step forms are narrower
  than "any constant step" might suggest: a negative immediate (`i += -2`)
  is refused (`asCountedLoopStep`'s `if (k <= 0) return 0;` only classifies
  positive `k`, relying on ADDIi/SUBIi's own opcode to carry the sign);
  `int8`/`int16`/`int64` counters are refused at EVERY step magnitude,
  including unit steps (`++i` compiles to `INCi8`/`INCi16`/`INCi64`, none of
  which `asCountedLoopStep` recognizes as a sanctioned mutator, and the
  64-bit counter also compares via `CMPi64`, which the prover's `cmpOp`
  whitelist rejects outright before the window guard ever runs); `uint` is
  refused only for step magnitude > 1 (a unit step is accepted, matching
  `IncVi`/`CMPu`, but a step > 1 compiles its comparison to `CMPIu`, which
  the window guard's `cmpOp` check explicitly does not accept); and
  `<=`/`>=` loop guards are
  refused (the prover only recognizes the `<`/`>` shapes `JNS`/`JNP`/`JS`/
  `JP` compile to). All of these are sound-conservative: refusal only ever
  costs precision (a fold to `UNKNOWN` that could have been `YES`), never
  soundness.
- **NO never crosses a call edge** (`asCModule::ComputeTransitiveFunctionMetadata`,
  `as_module.cpp`) — a callee's `asHALTS_NO` is downgraded to `UNKNOWN` before
  folding into the caller, because whether a never-halting callee's call site
  is even reached is not something the call graph tracks; only a function's
  own control flow can earn it `NO`.
- **Delegate poisoning costs precision, not soundness** — any unresolved call
  edge (shared-interface dispatch, an unbound or non-foldable imported
  function, a map-miss on
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

#### Const-global funcdef call resolution

A funcdef call site normally caps the local result at `UNKNOWN`, because a
funcdef handle is a runtime value the analysis can't see through. One shape
is exempt: reading a **const-handle** global funcdef whose declaration
initializes it, in the same statement, to exactly one function literal —
`funcdef int F(); int target() {...} F@ const g = @target;` — resolves the
call statically instead of poisoning it, via `asGetConstFuncdefGlobalTarget`
(`as_compiler.cpp`) scanning the global's finalized initializer bytecode.

**What resolves:** a global declared `F@ const g` (trailing `const` — the
HANDLE itself immutable) whose initializer bytecode contains exactly one
`asBC_FuncPtr` and no call instruction of any kind (`asBC_CALL`,
`asBC_CALLSYS`, `asBC_CALLINTF`, `asBC_CALLBND`, `asBC_CallPtr`,
`asBC_ALLOC`). Only a bare `@f` or lambda literal produces that shape.

**What is refused (by design, not oversight):**
- `const F@ g` (leading `const`) — AngelScript's const-position grammar is
  C-pointer-like: this only const-qualifies the *pointee*, not the handle.
  The handle stays reassignable (`@g = @other;` compiles against it), so
  `IsReadOnly()` reports false and the resolve never fires. Only the
  trailing spelling reports `isConstHandle`.
- A non-const global, or any initializer that isn't a single literal
  function reference — a function call (`F@ const g = pick();`), a delegate
  construction (`F@ const g = F(o.m);`, which compiles to
  `FuncPtr(bestMethod); CALLSYS`), a ternary between two literals (two
  `FuncPtr`s in the init bytecode) — all fail the "exactly one FuncPtr, no
  calls" scan and return 0 (unresolved).
- Any read while `asEP_ALLOW_UNSAFE_REFERENCES` is enabled on the engine.
  With unsafe references on, a read-only global handle can still be aliased
  by an `F@ &inout` (or bare `F@ &`) parameter and reassigned through that
  alias — `IsReadOnly()` no longer guarantees a fixed value, so the
  global-read site additionally requires
  `!engine->ep.allowUnsafeReferences` before trusting the scan at all.

**Soundness contract (out of scope, by design):** application writes through
`asIScriptModule::GetAddressOfGlobalVar` are trusted the same as a
registered native's own behavior — the same trust level the rest of this
analysis extends to `asFUNC_SYSTEM` functions. Script-side aliasing under
`asEP_ALLOW_UNSAFE_REFERENCES` is explicitly guarded against (previous
paragraph) *for script compiled in the same pass as the read*, not merely
assumed away; the guard is a compile-time and analysis-time check, so it
cannot see script that does not exist yet.

Consequently, on the same trust level as `GetAddressOfGlobalVar`, each of the
following **invalidates every `asHALTS_YES` a module has already reported**,
and the application must not act on those verdicts afterwards:

- Flipping `asEP_ALLOW_UNSAFE_REFERENCES` on the engine after that module's
  `Build()`. The property is engine-global and settable at any time; verdicts
  were computed under the value in force during `Build()`.
- Adding script to a module after its verdicts were computed — i.e.
  `asIScriptModule::CompileFunction` with `asCOMP_ADD_TO_MODULE` (likewise
  `CompileGlobalVar`). Newly added code can alias or reassign a global that
  earlier verdicts assumed fixed, and the compile of the added code does not
  itself recompute them. A later bind/unbind on that module does recompute the
  whole module, so the stale verdict is not necessarily permanent — but it is
  not a fix either: whether and when that happens is up to the application, so
  the verdicts must be treated as invalid from the moment the script is added.

Together these mean a `YES` is only meaningful for a module that is built
once and then left alone, under an engine whose properties are set before the
build. `BuildCalleeList` additionally re-checks
`!engine->ep.allowUnsafeReferences` when consuming the stamped per-site
funcdef targets, so a re-run of the metadata pass fails those sites closed.
The pass does re-run: bind/unbind of an imported function recomputes the
importing module's metadata (next section). That is not a substitute for the
contract above — a re-run only re-reads current state, it does not re-parse
script added after the fact.

**Per-site scoping (not per-global):** the resolved target is carried as a
per-`asBC_CallPtr`-site value (`ctx->knownFuncdefTarget`, reset to 0 after
each `PerformFunctionCall` funcdef-branch emission, and persisted per-site in
`asCScriptFunction::funcdefCallTargets` after bytecode finalization for
`BuildCalleeList` to consume). A statement with two chained calls through one
resolved global, e.g. `g()()` where `g`'s target itself returns a handle,
only resolves the first `CallPtr`; the second is never provably fixed and
stays `UNKNOWN`.

#### Imported-function (`asBC_CALLBND`) refinement on bind/unbind

An `import`ed call site is a runtime-rebindable edge, so it poisons while
unbound. Because it can be resolved once a binding exists,
`asIScriptModule::BindImportedFunction` / `UnbindImportedFunction` /
`BindAllImportedFunctions` / `UnbindAllImportedFunctions` now recompute the
*importing* module's transitive metadata on every call — including calls that
return an error, since `BindImportedFunction` unbinds the previous target
before it can fail, and including `BindAllImportedFunctions`' early error
exits (exactly one recompute per call, on every exit path). Verdicts
therefore track bind state instead of being frozen at `Build()`.

Each of those entry points runs one full fixed-point pass over the importing
module — the same pass `Build()` runs — so binding K imports one at a time
costs K passes. Prefer `BindAllImportedFunctions()`, which binds every import
and then recomputes exactly once. `LoadByteCode()` pays one pass too (see
"`LoadByteCode` recomputes" below).

`BuildCalleeList` resolves a `CALLBND` site the same way execution does
(`m_engine->importedFunctions[id & ~FUNC_IMPORTED]->boundFunctionId`, `-1`
meaning unbound) and folds the bound target's stored verdict as a constant —
**only** when the target is either a registered native (`asFUNC_SYSTEM`, no
bytecode, fixed metadata) or a script function whose own module **declares no
imported functions at all** (`GetImportedFunctionCount() == 0`). Everything
else — unbound, target inside this module, target whose module was discarded
(`module == 0`), and any target in a module that declares an import, bound or
not — keeps the poison.

That condition is load-bearing, not caution. Guarding only on
`target->module != this` admits a false `YES`: nothing recomputes module *A*
when module *B* rebinds, so a verdict published while an import chain was
acyclic survives the rebind that closes the cycle. With `prov`/`mid`/`top`
(mid imports prov, top imports `mid::g`), binding mid→`prov::good` makes
`mid::g` `YES`; binding top→`mid::g` folds that into `top::f`; then repointing
mid's import at `top::f` makes `f` and `g` mutually infinitely recursive while
mid's recompute folds `top::f`'s stale `YES` — and both settle at `YES`.

The precise statement of what the shipped guard buys is: **a module that
declares no imports contains no `CALLBND` site at all**, so none of its
verdicts can depend on any binding, no bind edge can lead back into the
folding module, and no later bind in the target module can move what was
folded. That is a property of the target module alone, checkable on the spot.

The weaker "no import currently *bound*" test would buy the same thing only
under a whole-program premise — that every verdict in the engine was produced
by this pass from that module's current state — and any entry point that
installs verdicts another way silently breaks it. `LoadByteCode` was exactly
such an entry point (next section). It now recomputes, so the premise holds
again; the guard deliberately does not rely on that.

Cost: an import chain stops refining at the first hop — the importer of an
importer caps at `UNKNOWN`, and so does the importer of a module that merely
*declares* an import it never calls. Precision only.
`tests/test_as_halting.cpp` pins the two-module ring, the three-module ring,
the rebind-into-a-cycle attack above, and both precision losses.

#### `LoadByteCode` recomputes

Saved bytecode carries `localHalts` / `transitiveHalts` /
`transitiveCallsDelegate` verbatim, but not the bind state that justified
them: a module saved while its imports were bound loads with those imports
unbound, so a restored `asHALTS_YES` can describe a call graph the loaded
module does not have — a false `YES` readable straight off the loaded
function, with no importer and no rebind involved.
`asCModule::LoadByteCode` therefore re-runs the pass after a successful read,
so every verdict in the engine is one the pass derived from current state.

Two consequences:

- **Cost.** One fixed-point pass per load, the same pass `Build()` runs.
- **Precision.** `asCScriptFunction::funcdefCallTargets` is not serialized, so
  a const-global funcdef call site that resolved before the save is
  unresolved after the load, and its caller loads as `UNKNOWN` where it saved
  as `YES`. Safe direction, and it matches the documented contract that the
  table is "not serialized — treat as every site unknown".

A build configured with `AS_NO_COMPILER` has no pass to run, so its
`LoadByteCode` restores verdicts as-is and this recompute does not apply.

Module teardown does not recompute: `asCModule::InternalReset` unbinds through
the non-recomputing `UnbindAllImportedFunctionsInternal`. The reason is that a
recompute while the module and all of its verdicts are being destroyed is
wasted work — *not* that it would read destroyed state. It would not:
`asCGlobalProperty::DestroyInternal` releases and NULLs each property's
`initFunc` rather than leaving it dangling, and the pass skips a null
`initFunc`; routing teardown through the recomputing wrapper instead was
measured green across the whole suite (88/88), so no test discriminates the
two. The routing is kept because the work is pointless, not because it is
unsafe. The window in which the pass may read the un-refcounted
`asCScriptFunction::funcdefCallTargets` entries is documented at their
declaration in `as_scriptfunction.h`.

## Literal loop-guard folding in the bytecode optimizer

`asCByteCode::Optimize()` folds the triple `SetV1/SetV2/SetV4 v, const ;
CpyVtoR4 v ; JLowZ|JLowNZ` to unconditional flow: the branch becomes an
`asBC_JMP` when the constant makes it taken, and is deleted when it does not.
All three `SetV*` variants carry the constant in the DWORD argument slot and
write it to the low bytes of the variable, `CpyVtoR4` loads the variable into
the value register, and `JLowZ`/`JLowNZ` test only that register's low byte, so
the outcome is decided at compile time.

The safety condition is `->next` adjacency. Labels are instructions in this IR,
so requiring the three to be immediately adjacent proves no jump can enter the
middle of the pattern — the same reasoning the halting analysis uses when it
elides constant loop guards. The `SetV`/`CpyVtoR4` pair is kept, so variable and
value-register state after the fold is bit-identical; only control flow changes.
`asBC_JMP` and `asBC_JLowZ`/`asBC_JLowNZ` are all `asBCTYPE_DW_ARG` with a stack
increase of zero, so instruction size and stack accounting are unaffected, and
`PostProcess()` has already run by the time `Optimize()` is called (see
`asCByteCode::Finalize`), so no reachability or stack-size state is invalidated.

This is a size/speed optimization only. Halting verdicts do not depend on it:
the analysis elides constant guards on its own side, so `while (true) {}` is
`asHALTS_NO` with or without the fold. `LiteralGuardIsFoldedOutOfBytecode` pins
both halves — no `JLowZ`/`JLowNZ` survives, and the verdict is unchanged.

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

All `AsHalting*` tests (currently 89, across the `AsHalting` and
`AsHaltingConstGlobalFuncdef` fixtures — the latter builds its engine
*without* `asEP_ALLOW_UNSAFE_REFERENCES`, see "Const-global funcdef call
resolution" above) should pass; this is the suite every later
halting-analysis change adds to, in `tests/test_as_halting.cpp`.

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
