# Patch: Script Object Init Callback

## Purpose

AngelScript has `SetScriptObjectUserDataCleanupCallback` which fires during
`~asCScriptObject`, but no corresponding callback when a script object is
constructed. This patch adds `SetScriptObjectInitCallback` so the host
application can be notified when any script object finishes construction.

This is needed by the Kreatures engine to register newly-created script
objects into a slot map (mailbox, instance tracking) without relying on
the script-side factory function.

## Callback signature

```cpp
// Same signature as the cleanup callback — receives the fully constructed object.
typedef void (*asINITSCRIPTOBJECTFUNC_t)(asIScriptObject *obj);
```

## Timing

The callback fires **after the script constructor has finished executing**,
meaning all script-level member initialization and constructor body code has
already run. This mirrors the cleanup callback, which fires during
`~asCScriptObject` after the script destructor has already been called via
bytecode.

Specifically, it fires at the point where `ScriptObjectFactory` /
`ScriptObjectCopyFactory` have a valid, fully-constructed object pointer
before returning it to the caller.

The callback does NOT fire for:
- Uninitialized allocations (`AllocateUninitializedObject`)
- Value-type script objects constructed in-place
- Objects created during deserialization (Zodiac handles its own init)

## Files to modify

### 1. `include/angelscript.h`

Add the typedef (near `asCLEANSCRIPTOBJECTFUNC_t`, line ~417):

```cpp
typedef void (*asINITSCRIPTOBJECTFUNC_t)(asIScriptObject *);
```

Add the method to `asIScriptEngine` (near `SetScriptObjectUserDataCleanupCallback`, line ~805):

```cpp
virtual void SetScriptObjectInitCallback(asINITSCRIPTOBJECTFUNC_t callback) = 0;
```

### 2. `source/as_scriptengine.h`

Add the virtual override declaration (near line ~207):

```cpp
virtual void SetScriptObjectInitCallback(asINITSCRIPTOBJECTFUNC_t callback);
```

Add the member (near `cleanScriptObjectFuncs`, line ~487):

```cpp
asINITSCRIPTOBJECTFUNC_t initScriptObjectFunc = nullptr;
```

### 3. `source/as_scriptengine.cpp`

Add the implementation (near `SetScriptObjectUserDataCleanupCallback`):

```cpp
// interface
void asCScriptEngine::SetScriptObjectInitCallback(asINITSCRIPTOBJECTFUNC_t callback)
{
    ACQUIREEXCLUSIVE(engineRWLock);
    initScriptObjectFunc = callback;
    RELEASEEXCLUSIVE(engineRWLock);
}
```

### 4. `source/as_scriptobject.cpp`

In `ScriptObjectFactory` — after `ptr->AddRef()` (line ~114), before return:

```cpp
    // Notify host of new script object
    if( engine->initScriptObjectFunc )
        engine->initScriptObjectFunc(ptr);

    if( isNested )
        ctx->PopState();
    else
        engine->ReturnContext(ctx);

    return ptr;
```

In `ScriptObjectCopyFactory` — same position (after AddRef, before cleanup/return):

```cpp
    // Notify host of new script object
    if( engine->initScriptObjectFunc )
        engine->initScriptObjectFunc(ptr);
```

## What this does NOT include

- No `asPWORD type` parameter (unlike cleanup). There is only one init
  callback, not a per-userdata-slot callback. The host can dispatch
  internally if needed.
- No changes to `CreateScriptObject` for registered (non-script) types.
  Those types already have their own factory functions.
- No changes to deserialization paths. Zodiac calls
  `AllocateUninitializedObject` then manually restores state.

## Verification

After applying, the following should work:

```cpp
engine->SetScriptObjectInitCallback([](asIScriptObject * obj) {
    printf("Script object created: %s\n", obj->GetObjectType()->GetName());
});
```

Every script-class instantiation (`MyClass()` in script code, or
`engine->CreateScriptObject(type)` for script types) should print.

## Re-applying after AS update

1. Check if `ScriptObjectFactory` / `ScriptObjectCopyFactory` signatures changed
2. Check if `cleanScriptObjectFuncs` moved or was renamed
3. Apply the 4 file changes above — they are minimal and isolated
4. grep for `initScriptObjectFunc` to verify all insertion points
