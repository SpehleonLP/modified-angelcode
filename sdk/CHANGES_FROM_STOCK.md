# AngelScript 2.38.0 — All Modifications from Stock

**Base:** `angelscript_2.38.0 (1).zip` (the stock release)
**Compared against:** current source tree as of 2026-03-16
**Purpose:** Document every difference so intentional patches can be identified and re-applied to future AS versions.

> Lines marked **[AI-TODAY]** were added by an AI agent on 2026-03-16 implementing the "handle resolve" feature.
> These should NOT be carried forward unless you decide to re-implement that feature.
> Lines marked **[OLDER-THAN-STOCK]** appear to be from a pre-release AS version (your code predates the 2.38.0 release for that area).
> Lines marked **[YOUR-PATCH]** appear to be intentional engine modifications.
> Lines marked **[WARNING-FIX]** are compiler warning suppressions.
> Lines marked **[COPYRIGHT-DRIFT]** are copyright year mismatches (likely from patching forward from older AS).

---

## Deleted Files

| File | Notes |
|------|-------|
| `source/as_callfunc_e2k.cpp` | Elbrus 2000 native calling convention — removed because E2K forced to `AS_MAX_PORTABILITY` |
| `source/as_callfunc_e2k.S` | Same |

---

## include/angelscript.h

### [YOUR-PATCH] Version string
```diff
-#define ANGELSCRIPT_VERSION_STRING "2.38.0"
+#define ANGELSCRIPT_VERSION_STRING "2.38.0 WIP"
```

### [AI-TODAY] Handle resolve flag and mask
```diff
+	asOBJ_HANDLE_RESOLVE              = (asQWORD(1)<<33),
-	asOBJ_MASK_VALID_FLAGS            = 0x1801FFFFFul,
+	asOBJ_MASK_VALID_FLAGS            = 0x3801FFFFFul,
```

### [OLDER-THAN-STOCK] Removed GetMessageCallback
The 2.38.0 release added `GetMessageCallback` to `asIScriptEngine`. Your copy doesn't have it.
```diff
-	virtual int GetMessageCallback(asSFuncPtr *callback, void **obj, asDWORD *callConv) = 0;
```

### [AI-TODAY] SetHandleResolveId in interface
```diff
+	virtual int            SetHandleResolveId(const char *obj, int resolveId) = 0;
```

### [YOUR-PATCH] Callback signatures — pass by value instead of const ref
Three methods changed from `const asSFuncPtr &` to `asSFuncPtr`:
- `asIScriptEngine::SetTranslateAppExceptionCallback`
- `asIScriptContext::SetExceptionCallback`
- `asIScriptContext::SetLineCallback`

### [OLDER-THAN-STOCK] Removed isConst from GetProperty
```diff
-	virtual int GetProperty(..., bool *isConst = 0) const = 0;
+	virtual int GetProperty(...) const = 0;  // no isConst param
```

### [OLDER-THAN-STOCK] GetScriptSectionName not deprecated
```diff
-#ifdef AS_DEPRECATED
-	// deprecated since 2025-04-25, 2.38.0
 	virtual const char *GetScriptSectionName() const = 0;
-#endif
```

### [AI-TODAY] New opcodes 201-204
```diff
+	asBC_LoadThisAndChk     = 201,
+	asBC_ResolveThisAndChk  = 202,
+	asBC_CmpResolveNull     = 203,
+	asBC_RefCpyResolve      = 204,
-	asBC_MAXBYTECODE = 201,
+	asBC_MAXBYTECODE = 205,
```

### [AI-TODAY] New asBCInfo entries
```diff
-	asBCINFO_DUMMY(201-204)
+	asBCINFO(LoadThisAndChk, rW_ARG, AS_PTR_SIZE),
+	asBCINFO(ResolveThisAndChk, rW_PTR_ARG, AS_PTR_SIZE),
+	asBCINFO(CmpResolveNull, rW_PTR_ARG, 0),
+	asBCINFO(RefCpyResolve, PTR_ARG, -AS_PTR_SIZE),
```

---

## source/as_texts.h

### [YOUR-PATCH] Changed TXT_MEMBER error message (removed format specifier)
```diff
-#define TXT_MEMBER_s_ACCESSED_BEFORE_INIT  "The member '%s' is accessed before the initialization"
+#define TXT_MEMBER_ACCESSED_BEFORE_INIT    "The member has been accessed before the initialization"
```

### [AI-TODAY] Dead handle access string
```diff
+#define TXT_DEAD_HANDLE_ACCESS  "Attempted access on a dead handle"
```

---

## source/as_objecttype.h

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] Removed isConst from GetProperty
```diff
-	int GetProperty(..., bool *isConst) const;
+	int GetProperty(...) const;  // no isConst
```

### [AI-TODAY] resolveId field
```diff
+	// Handle resolve ID for asOBJ_HANDLE_RESOLVE types. -1 = not a handle-resolve type.
+	int resolveId;
```

---

## source/as_objecttype.cpp

### [COPYRIGHT-DRIFT] 2025→2017

### [AI-TODAY] resolveId initialization (2 constructors)
```diff
+	resolveId = -1;
```

### [OLDER-THAN-STOCK] Removed isConst output from GetProperty implementation
```diff
-	if (out_isConst)
-		*out_isConst = prop->type.IsReadOnly();
```

---

## source/as_typeinfo.h

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] Removed isConst from GetProperty declaration
```diff
-	int GetProperty(..., bool *isConst) const;
+	int GetProperty(...) const;
```

---

## source/as_typeinfo.cpp

### [WARNING-FIX] Suppress -Wextra
```diff
+#pragma GCC diagnostic ignored "-Wextra"
```

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] Removed isConst from GetProperty implementation
```diff
-	if (out_isConst) *out_isConst = false;
```

---

## source/as_config.h

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] E2K/Elbrus changes
- Changed `__MCST__` references to `__e2k__`
- Changed E2K comment text
- Forced E2K to `AS_MAX_PORTABILITY` (stock 2.38.0 added native E2K support)
- Removed `RETURN_VALUE_MAX_SIZE`, `HAS_128_BIT_PRIMITIVES`, `AS_NO_THISCALL_FUNCTOR_METHOD` for E2K
- Removed `RETURN_VALUE_MAX_SIZE` comment block
- Removed E2K from portability fallback `#if` check
- Single-line reformatting of the portability `#if`

---

## source/as_callfunc.cpp

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] Removed RETURN_VALUE_MAX_SIZE support
Large change: the 2.38.0 release added `RETURN_VALUE_MAX_SIZE` for E2K 128-bit returns. Your copy removes all of this:
- `retQW` changed from array `retQW[2]` (or `retQW[(RETURN_VALUE_MAX_SIZE+1)/2]`) back to two separate variables `retQW` + `retQW2`
- Removed the alternate `CallSystemFunctionNative` signature that takes `asQWORD *retQW` array
- All `retQW[0]` → `retQW`, `retQW[1]` → `retQW2`
- Removed `#ifndef RETURN_VALUE_MAX_SIZE` / `#else` blocks
- Removed `memcpy` fallback for large return sizes
- Changed `hostReturnSize == 4` from separate case to `else` fallback

### [OLDER-THAN-STOCK] Removed HAS_128_BIT_PRIMITIVES guard alternate
```diff
-#ifdef RETURN_VALUE_MAX_SIZE
-	if( func->returnType.GetSizeInMemoryDWords() > RETURN_VALUE_MAX_SIZE )
-#elif defined(HAS_128_BIT_PRIMITIVES)
+#ifdef HAS_128_BIT_PRIMITIVES
```

---

## source/as_callfunc_x64_gcc.cpp

### [WARNING-FIX] Suppress -Wimplicit-fallthrough + add [[fallthrough]]
```diff
+#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
```
Plus two `[[fallthrough]];` annotations in the switch statement.

---

## source/as_callfunc_arm.cpp

### [COPYRIGHT-DRIFT] 2025→2015

### [OLDER-THAN-STOCK] Suppress unused retQW2 parameter
```diff
-asQWORD CallSystemFunctionNative(..., asQWORD &retQW2, ...)
+asQWORD CallSystemFunctionNative(..., asQWORD &/*retQW2*/, ...)
```

### [OLDER-THAN-STOCK] Return size check 16→8
```diff
-	descr->returnType.GetSizeInMemoryBytes() <= 16 )
+	descr->returnType.GetSizeInMemoryBytes() <= 8 )
```

### [OLDER-THAN-STOCK] Removed ttQuestion vartype arg handling for ARM
Removed ~30 lines handling `ttQuestion` token type in ARM parameter passing.

### [OLDER-THAN-STOCK] Removed retQW2 for hostReturnSize > 2
```diff
-	if (sysFunc->hostReturnSize > 2)
-		retQW2 = *((asQWORD*)&paramBuffer[VFP_OFFSET + 2]);
```

---

## source/as_context.h

### [COPYRIGHT-DRIFT] 2025→2024

### [YOUR-PATCH] Callback signatures by value
```diff
-	int SetExceptionCallback(const asSFuncPtr &callback, void *obj, int callConv);
+	int SetExceptionCallback(asSFuncPtr callback, void *obj, int callConv);
-	int SetLineCallback(const asSFuncPtr &callback, void *obj, int callConv);
+	int SetLineCallback(asSFuncPtr callback, void *obj, int callConv);
```

---

## source/as_context.cpp

### [AI-TODAY] Handle resolve additions
- `#include "as_objecttype.h"`
- `extern "C" void* ResolveHandle(int resolveId, void* handle);`
- 4 new instruction handlers (LoadThisAndChk, ResolveThisAndChk, CmpResolveNull, RefCpyResolve)
- Dispatch table entries for opcodes 201-204
- Removed FAULT entries 201-204 from non-computed-goto switch
- Resolve-before-Release/AddRef at 8 C++ cleanup sites (SetArgObject, FREE, CleanReturnObject, CleanArgsOnStack, CleanStackFrame x2, generic return, generic arg cleanup)

### [YOUR-PATCH] Callback signatures by value
```diff
-int asCContext::SetLineCallback(const asSFuncPtr &callback, ...)
+int asCContext::SetLineCallback(asSFuncPtr callback, ...)
```
Same for `SetExceptionCallback`.

### [OLDER-THAN-STOCK] HUGE_VALF → HUGE_VAL
Three pow overflow checks changed:
```diff
-if( r == HUGE_VALF || isinf(r) )
+if( r == float(HUGE_VAL) )
```
```diff
-if( r == HUGE_VAL || isinf(r) )
+if( r == HUGE_VAL )
```

### [OLDER-THAN-STOCK] Removed MSVC HUGE_VALF warning pragma
```diff
-// Apparently a bug in MSVC (or perhaps Windows SDK) caused use HUGE_VALF to issue a warning
-#pragma warning(disable:4756)
```

### [YOUR-PATCH] Computed goto cast simplification
```diff
-#define NEXT_INSTRUCTION() goto *(void*) dispatch_table[*(asBYTE*)l_bc]
+#define NEXT_INSTRUCTION() goto *dispatch_table[*(asBYTE*)l_bc]
```

### [OLDER-THAN-STOCK] Stack serialization simplifications
- Removed negative checks on serialized stack pointers (`if (int(sp) < 0) return asERROR`)
- Changed `asNO_FUNCTION` → `asERROR` for pushed state detection
- Removed intermediate variables for `SerializeStackPointer` calls
- Removed `asASSERT(int(stackIndex) >= 0)` and bounds check in `SerializeStackPointer`

### [OLDER-THAN-STOCK] Removed const type modifier from variables
```diff
-	if (func->scriptData &&
-		func->scriptData->variables[varIndex]->type.IsReadOnly())
-		*typeModifiers = (asETypeModifiers)(*typeModifiers | asTM_CONST);
```

### [YOUR-PATCH] liveObjects tracking — changed conditional to assertion
```diff
-	if( var != asUINT(-1) )
+	asASSERT(var != asUINT(-1));
 		liveObjects[var] += 1;
```

### [OLDER-THAN-STOCK] Removed try/catch stackSize
```diff
-	m_regs.stackPointer = m_regs.stackFramePointer - tryCatchInfo->stackSize - ...;
 	m_regs.programPointer = m_currentFunction->scriptData->byteCode.AddressOf() + tryCatchInfo->catchPos;
```

---

## source/as_scriptengine.h

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] Removed GetMessageCallback
```diff
-	virtual int GetMessageCallback(asSFuncPtr* callback, void** obj, asDWORD* callConv);
```

### [AI-TODAY] SetHandleResolveId declaration
```diff
+	int SetHandleResolveId(const char *obj, int resolveId);
```

### [YOUR-PATCH] SetTranslateAppExceptionCallback by value
```diff
-	virtual int SetTranslateAppExceptionCallback(const asSFuncPtr &callback, ...);
+	virtual int SetTranslateAppExceptionCallback(asSFuncPtr callback, ...);
```

### [OLDER-THAN-STOCK] Removed ParseNamespace
```diff
-	int ParseNamespace(const char* ns, asCArray<asCString>& nsStrings) const;
```

### [OLDER-THAN-STOCK] Removed msgCallback storage fields
```diff
-	asSFuncPtr  msgCallbackOriginalFuncPtr;
-	asDWORD     msgCallbackOriginalCallConv;
```

---

## source/as_scriptengine.cpp

### [OLDER-THAN-STOCK] Removed ParseNamespace and its usage
Large change: the 2.38.0 release refactored namespace parsing into a shared `ParseNamespace` method. Your copy has the older inline implementation in `SetDefaultNamespace`.

### [OLDER-THAN-STOCK] Removed GetMessageCallback implementation

### [OLDER-THAN-STOCK] Removed msgCallback storage
```diff
-	msgCallbackOriginalFuncPtr = callback;
-	msgCallbackOriginalCallConv = callConv;
```

### [AI-TODAY] asOBJ_HANDLE_RESOLVE registration validation
```diff
+	if( flags & ~(... | asOBJ_HANDLE_RESOLVE) )
+	if( (flags & asOBJ_HANDLE_RESOLVE) && (flags & (asOBJ_NOHANDLE|asOBJ_SCOPED|asOBJ_NOCOUNT)) )
```

### [AI-TODAY] SetHandleResolveId implementation (~15 lines)

### [YOUR-PATCH] SetTranslateAppExceptionCallback by value

### [OLDER-THAN-STOCK] Removed template function namespace
```diff
-	newFunc->nameSpace = baseFunc->nameSpace;
```

### [OLDER-THAN-STOCK] Removed CDECL_OBJFIRST/OBJLAST factory support
Removed ~16 lines handling `ICC_CDECL_OBJFIRST` and `ICC_CDECL_OBJLAST` in `CallGlobalFunctionRetPtr` (both 0-arg and 1-arg variants).

---

## source/as_scriptfunction.h

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] Removed tryCatchInfo stackSize field
```diff
-	asUINT stackSize;
```

### [OLDER-THAN-STOCK] GetScriptSectionName not deprecated
```diff
-#ifdef AS_DEPRECATED
-	// deprecated since 2025-04-25, 2.38.0
 	const char *GetScriptSectionName() const;
-#endif
```

---

## source/as_scriptfunction.cpp

### [OLDER-THAN-STOCK] Removed scriptSectionIdx bounds check
```diff
-	if (scriptSection) *scriptSection = scriptData->scriptSectionIdx >= 0 ? engine->scriptSectionNames[...] : 0;
+	if (scriptSection) *scriptSection = engine->scriptSectionNames[scriptData->scriptSectionIdx]->AddressOf();
```

### [AI-TODAY] New opcodes in type info reference counting (2 switch cases)
```diff
+	case asBC_ResolveThisAndChk:
+	case asBC_CmpResolveNull:
+	case asBC_RefCpyResolve:
```

### [OLDER-THAN-STOCK] GetScriptSectionName not deprecated
```diff
-#ifdef AS_DEPRECATED
-// deprecated since 2025-04-25, 2.38.0
 const char *asCScriptFunction::GetScriptSectionName() const
-#endif
```

---

## source/as_bytecode.cpp

### [COPYRIGHT-DRIFT] 2025→2024

### [YOUR-PATCH] Fixed register read/overwrite tracking
Moved `JMP`, `JMPP` from "reads the temp register" to "overwrites/discards" category. Added `JS`, `JNS`, `JP`, `JNP`, `JMPP`, `JMP`, `JZ`, `JNZ`, `JLowZ`, `JLowNZ`, `LABEL` to the overwrite list. This is a correctness fix — jumps don't read the register, they consume/discard it.

### [OLDER-THAN-STOCK] Removed tryCatchInfo stackSize
```diff
-	info.stackSize = asUINT(instr->stackSize);
```

### [AI-TODAY] InstrW_PTR assertion loosened
```diff
-	asASSERT(asBCInfo[bc].type == asBCTYPE_wW_PTR_ARG);
+	asASSERT(asBCInfo[bc].type == asBCTYPE_wW_PTR_ARG || asBCInfo[bc].type == asBCTYPE_rW_PTR_ARG);
```

---

## source/as_bytecode.h

No semantic changes (whitespace only or copyright drift).

---

## source/as_compiler.h

### [YOUR-PATCH] Moved MatchFunctions declaration (reordered, not new)

### [AI-TODAY] EmitPrepareObject and EmitRefCpy declarations
```diff
+	void EmitPrepareObject(asCByteCode *bc, asCDataType &type, short varOffset);
+	void EmitRefCpy(asCByteCode *bc, asCTypeInfo *typeInfo);
```

### [OLDER-THAN-STOCK] Removed m_inheritedPropertyAccess
```diff
-	asCMap<asCObjectProperty*, asCScriptNode*> m_inheritedPropertyAccess;
```

---

## source/as_compiler.cpp

### [OLDER-THAN-STOCK] Removed MSVC HUGE_VALF warning pragma

### [AI-TODAY] All EmitRefCpy calls (~14 sites)
Every `bc.InstrPTR(asBC_REFCPY, typeInfo)` for non-funcdef types was changed to `EmitRefCpy(&bc, typeInfo)`.

### [AI-TODAY] EmitPrepareObject call (1 site)
```diff
-	ctx->bc.InstrSHORT(asBC_ChkNullV, (short)ctx->type.stackOffset);
+	EmitPrepareObject(&ctx->bc, ctx->type.dataType, (short)ctx->type.stackOffset);
```

### [AI-TODAY] CmpResolveNull emission block (~35 lines in CompileHandleComparison)

### [AI-TODAY] EmitRefCpy and EmitPrepareObject function definitions (~15 lines at end of file)

### [OLDER-THAN-STOCK] Removed inherited property access validation
Removed `m_inheritedPropertyAccess` tracking and deferred validation for accessing inherited properties before parent constructor call.

### [YOUR-PATCH] Changed member access error message
```diff
-	msg.Format(TXT_MEMBER_s_ACCESSED_BEFORE_INIT, prop->name.AddressOf());
-	Error(msg, opNode);
+	Error(TXT_MEMBER_ACCESSED_BEFORE_INIT, opNode);
```

### [OLDER-THAN-STOCK] Removed early compile error bail-out
```diff
-	if (hasCompileErrors || builder->numErrors != buildErrors)
-		return -1;
```

### [OLDER-THAN-STOCK] Removed MakeReadOnly(false) workaround (2 sites)
```diff
-	// Consider the argument as non-const already to avoid PrepareForAssignment
-	// trying to make another copy, leading to infinite recursive loop
-	arg->type.dataType.MakeReadOnly(false);
```

### [OLDER-THAN-STOCK] Removed funcdef handle forcing for var types
```diff
-	param.MakeHandle(ctx->type.isExplicitHandle || ctx->type.IsNullConstant() || CastToFuncdefType(...));
+	param.MakeHandle(ctx->type.isExplicitHandle || ctx->type.IsNullConstant());
```

### [OLDER-THAN-STOCK] Removed explicit handle forcing
```diff
-	ctx->type.isExplicitHandle = param.IsObjectHandle();
```

### [OLDER-THAN-STOCK] Removed lambda deterministic type check (2 sites)
```diff
-	if (param.GetTypeInfo() == &engine->functionBehaviours)
-	{ Error(TXT_INVALID_EXPRESSION_LAMBDA, node); return -1; }
```

### [YOUR-PATCH] For-each auto: changed handle forcing
```diff
-	dt.MakeHandle(true); // Always use handle for auto if possible
+	dt.MakeHandle(isHandle);
```

### [OLDER-THAN-STOCK] HUGE_VALF → HUGE_VAL (3 sites in constant folding)

### [OLDER-THAN-STOCK] Removed template function instantiation error checking
Simplified `InstantiateTemplateFunctions` — removed template count validation and error messages.

### [OLDER-THAN-STOCK] Constructor call validation change
```diff
-	// Only set m_isConstructorCalled after the call is actually made
-	// m_isConstructorCalled = true;
+	else if (m_isConstructorCalled)
+		Error(TXT_CANNOT_CALL_CONSTRUCTOR_TWICE, node);
+	m_isConstructorCalled = true;
```
Plus removed duplicate constructor-called check later in function.

### [OLDER-THAN-STOCK] Removed &out reference matching improvement
Removed ~20 lines that checked if output reference parameter types could be converted back to argument types.

### [OLDER-THAN-STOCK] MatchArgument cost calculation change
Moved the final `return cost` / `return -1` logic around — the stock version returns -1 if types don't match after implicit conversion (checked early), your version checks at the end.

### [YOUR-PATCH] Const-to-non-const copy: added allowObjectConstruct guard
```diff
-	if( ctx->type.dataType.IsReadOnly() && !to.IsReadOnly() )
+	if( ctx->type.dataType.IsReadOnly() && !to.IsReadOnly() && allowObjectConstruct )
```

### [OLDER-THAN-STOCK] Dereference simplification
```diff
-	if( !(ctx->type.isVariable || ctx->type.isTemporary) || IsVariableOnHeap(ctx->type.stackOffset) )
+	if( IsVariableOnHeap(ctx->type.stackOffset) )
```

### [OLDER-THAN-STOCK] Removed temporary object lvalue forcing
```diff
-	if (lctx->type.dataType.IsObject() && !lctx->type.dataType.IsObjectHandle())
-		lctx->type.isLValue = true;
```

### [OLDER-THAN-STOCK] Removed allowObjectConstruct=false for MatchFunctions in copy constructor path
```diff
-	// Don't allow making copy of argument here
 	cost = asCC_TO_OBJECT_CONV + MatchFunctions(funcs, args, node, 0, 0, 0, false, true, false);
```

### [YOUR-PATCH] Escape sequence \e support
```diff
+	else if( cstr[n] == 'e' )
+		val = '\e';
```

### [OLDER-THAN-STOCK] Removed variadic argument count emission (moved position)
```diff
-	if (builder->GetFunctionDescription(funcs[0])->IsVariadic())
-		ctx->bc.InstrDWORD(asBC_PshC4, (asDWORD)args.GetLength());
```

### [OLDER-THAN-STOCK] Variadic arg count position change in PerformFunctionCall
The variadic argument count push was moved before the `if (r < 0) return r` check.

---

## source/as_restore.cpp

### [OLDER-THAN-STOCK] Removed template function support in bytecode restore
Large change (~80 lines removed): removed template function lookup/instantiation during bytecode deserialization. Includes:
- Removed `asASSERT(func.templateSubTypes.GetLength() == 0)` checks (3 sites)
- Removed template function search in global and class method resolution
- Removed `isTemplateFunc` encoding bit
- Removed `templateSubTypes` reading
- Added fallback loop with "should never happen" comment

### [OLDER-THAN-STOCK] Removed tryCatchInfo stackSize
```diff
-	func->scriptData->tryCatchInfo[i].stackSize = SanityCheck(ReadEncodedUInt(), 100000);
```

### [OLDER-THAN-STOCK] Removed savedDataTypes bounds check
```diff
-	if (idx-1 >= savedDataTypes.GetLength())
-	{ Error(...); return; }
```

### [AI-TODAY] New opcodes in bytecode translation table
```diff
+	c == asBC_ResolveThisAndChk ||
+	c == asBC_CmpResolveNull ||
+	c == asBC_RefCpyResolve )
```

---

## source/as_builder.h

### [YOUR-PATCH] Moved `#endif // AS_NO_COMPILER` to include sMixinClass in non-compiler builds
### [YOUR-PATCH] Removed `protected:` before friend declarations
### [OLDER-THAN-STOCK] Removed `FindObjectTypeOrMixinInNsHierarchy` declaration
### [YOUR-PATCH] Moved `AddVisibleNamespaces` / `FindNextVisibleNamespace` to compiler section
### [YOUR-PATCH] Moved `namespaceVisibility` map to compiler section
### [OLDER-THAN-STOCK] Removed `namespaceVisibility` from non-compiler section

---

## source/as_builder.cpp

### [OLDER-THAN-STOCK] Moved `#endif // AS_NO_COMPILER` and `#ifndef AS_NO_COMPILER`
Functions `AddVisibleNamespaces` and `FindNextVisibleNamespace` moved outside the `AS_NO_COMPILER` guard.

### [YOUR-PATCH] Inlined FindObjectTypeOrMixinInNsHierarchy
The shared method `FindObjectTypeOrMixinInNsHierarchy` was removed (~73 lines) and its logic was inlined at both call sites (interface resolution and class base resolution) with slight differences. Both inline versions have `// TODO: using: review` and `// TODO: clean up` comments.

### [YOUR-PATCH] Simplified template type lookup in GetNameSpaceFromNode
```diff
-	FindObjectTypeOrMixinInNsHierarchy(templateName, ns, ...)
+	asCObjectType *templateType = GetObjectType(templateName.AddressOf(), ns);
```

### [OLDER-THAN-STOCK] Removed namespace parameter from AddScriptFunction call
```diff
-	module->AddScriptFunction(..., objType->nameSpace);
+	module->AddScriptFunction(...);
```

---

## source/as_parser.cpp / as_parser.h

### [OLDER-THAN-STOCK] BNF comment syntax changes
All BNF comments changed from regex-style `(X)?` `(X)*` to bracket-style `[X]` `{X}`:
- `(X)?` → `[X]` (optional)
- `(X)*` → `{X}` (zero or more)
- Also simplified `IDENTIFIER`, `NUMBER`, `STRING`, `BITS`, `COMMENT`, `WHITESPACE` BNF comments to remove regex patterns

These are purely comment changes with no code impact.

---

## source/as_property.h

### [YOUR-PATCH] Made GetInitFunc const
```diff
-	asCScriptFunction *GetInitFunc();
+	asCScriptFunction *GetInitFunc() const;
```

---

## source/as_globalproperty.cpp

### [YOUR-PATCH] Made GetInitFunc const (implementation)
```diff
-asCScriptFunction *asCGlobalProperty::GetInitFunc()
+asCScriptFunction *asCGlobalProperty::GetInitFunc() const
```

---

## source/as_scriptobject.h

### [YOUR-PATCH] Forward declaration
```diff
+class asCScriptEngine;
```

---

## source/as_scriptobject.cpp

### [YOUR-PATCH] Moved Release/objType cleanup after refCount assertion
```diff
-	objType->Release();
-	objType = 0;
 	asASSERT( refCount.get() == 0 );
+	objType->Release();
+	objType = 0;
```

---

## source/as_generic.h

### [COPYRIGHT-DRIFT] 2025→2024

### [YOUR-PATCH] Forward declaration
```diff
+class asCDataType;
```

### [OLDER-THAN-STOCK] Removed SetReturnObject
```diff
-	int SetReturnObject(void* obj);
```

---

## source/as_generic.cpp

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] Removed SetReturnObject implementation (~42 lines)

---

## source/as_module.cpp

### [COPYRIGHT-DRIFT] 2025→2024

### [OLDER-THAN-STOCK] Inline namespace parsing in SetDefaultNamespace
Replaced `ParseNamespace` usage with inline parsing (same pattern as `as_scriptengine.cpp`).

### [OLDER-THAN-STOCK] Changed GetDeclaredAt to direct access
Replaced `GetDeclaredAt(&scriptSection, &row, &col)` with direct `scriptSectionIdx` / `GetLineNumber` access.

### [OLDER-THAN-STOCK] Changed to use GetScriptSectionName()
```diff
-	function->GetDeclaredAt(&scriptSection, 0, 0);
-	m_engine->WriteMessage(scriptSection ? scriptSection : "", ...);
+	m_engine->WriteMessage(function->GetScriptSectionName(), ...);
```

---

## source/as_module.h

No semantic changes noted.

---

## source/as_restore.h

No semantic changes noted (may have whitespace only).

---

## source/as_tokendef.h

No semantic changes (BNF comment style only, part of parser changes).

---

## source/as_scriptnode.h

No semantic changes (BNF comment style only).

---

## source/as_scriptcode.h / as_scriptcode.cpp

No semantic changes noted.

---

## source/as_map.h / as_array.h

No semantic changes noted (may be whitespace/formatting only).

---

# ADD-ON CHANGES

---

## add_on/datetime/datetime.h

### [YOUR-PATCH] Added time_point constructor
```diff
+	CDateTime(const std::chrono::system_clock::time_point &other) : tp(other) {};
```

## add_on/datetime/datetime.cpp

### [YOUR-PATCH] Removed static from time_point_to_tm
```diff
-static tm time_point_to_tm(...)
+tm time_point_to_tm(...)
```

---

## add_on/scriptany/scriptany.h + scriptany.cpp

### [YOUR-PATCH] REMOVE_MODULE_LINKAGE system
Major addition: intrusive linked list for tracking `CScriptAny` instances that hold module-dependent types. Allows bulk cleanup when a module is discarded.
- `#define REMOVE_MODULE_LINKAGE 1`
- `MovePointer` enum, `StoreMove()` method (takes ownership instead of copy)
- `RemoverModule(asIScriptModule*)` static method
- Linked list management (`g_first`, `g_last`, `m_next`, `m_prev`, atomic `m_isInList`)
- `ShouldBeInList()`, `Depends()`, `UpdateListSafe()` etc.
- `GetObjectAddress()` method
- `mark` field + `setMark()` / `getMark()`
- `UpdateListSafe()` calls in operator=, destructor, FreeObject, StoreMove
- `FreeObject()` now sets typeId to `asTYPEID_VOID` and clears mark

---

## add_on/scriptarray/scriptarray.h + scriptarray.cpp

### [YOUR-PATCH] POD value type optimization
Major change: arrays of POD value types store elements inline instead of as pointers.
- `SArrayBuffer` moved to header, added `userData` field (asQWORD)
- Added `isValueType` bool field, `elementSize` changed from `int` to `int16_t`
- Constructor logic checks `asOBJ_POD` flag to determine inline storage
- `Construct`, `Destruct`, `operator[]`, `EnumReferences`, assignment operator all check `isValueType`
- Removed `ScriptArrayTemplateCallback` — replaced with external `ScriptArrayTemplateCallback2`
- Added various helper functions and friend declarations
- Added `#include "AngelScript/Native/as_arraysupport.h"` (engine-specific)
- Added `TryOpConv`, `Swap`, `setUserData`/`getUserData`, `GetElementSize` methods
- Added `TryNormalConstructor`/`TryListConstructor` methods
- Added `UniqueT` template method
- Added `CScriptArray_Filter*`, `CScriptArray_Unique*` free functions
- Generic registration: moved `opAssign` registration order

---

## add_on/scriptbuilder/scriptbuilder.cpp

### [OLDER-THAN-STOCK] Removed classMetadataMap clearing
```diff
-	classMetadataMap.clear();
```

---

## add_on/scriptdictionary/scriptdictionary.h + scriptdictionary.cpp

### [YOUR-PATCH] Added valueType to cache
```diff
+	asITypeInfo *valueType;
+	cache->valueType = engine->GetTypeInfoByDecl("dictionaryValue");
```

### [YOUR-PATCH] Value initialization
```diff
-	asINT64 value;
+	asINT64 value{};
-	double value;
+	double value{};
```

### [OLDER-THAN-STOCK] dictKey_t → std::string in opForValue1
```diff
-const dictKey_t& opForValue1(...) const;
+const std::string& opForValue1(...) const;
```

### [OLDER-THAN-STOCK] Removed generic registration for foreach
Removed all `ScriptDict*_Generic` wrapper functions for the foreach iterator pattern. The iterator C++ implementation was moved after `RegisterScriptDictionary_Generic`.

### [YOUR-PATCH] Added friend and GetEngine
```diff
+friend void CScriptDictionary_OpConvInner(...);
+	asIScriptEngine * GetEngine() const { return engine; }
```

---

## add_on/scripthelper/scripthelper.h

### [OLDER-THAN-STOCK] Typo regression
```diff
-// the statements can access the entities compiled in the module.
+// the statements can access the entitites compiled in the module.
```
(This looks like your copy has the old typo that was fixed in 2.38.0)

## add_on/scripthelper/scripthelper.cpp

### [OLDER-THAN-STOCK] Removed objprop validation for isCompositeIndirect
### [OLDER-THAN-STOCK] Changed to use GetScriptSectionName() instead of GetDeclaredAt

---

## add_on/scriptsocket/scriptsocket.h + scriptsocket.cpp

### [OLDER-THAN-STOCK] Major simplification
Your copy is from an earlier version of the socket add-on:
- Removed `#ifdef _WIN32` guard (socket always compiled)
- Removed `IsActive()` method and all its usage
- Removed auto-close on error in `Select()`, `Send()`, `Receive()`
- Removed `autowrapper/aswrappedcall.h` include
- Removed generic registration (`RegisterScriptSocket_Generic`)
- Removed `RegisterScriptSocket_Native` (merged into `RegisterScriptSocket`)
- Removed non-Windows `asNOT_SUPPORTED` fallback
- Listen queue 25→5
- Fixed `Close()` bug: `m_socket == -1` → `m_socket != -1` (the stock version has this bug too — your fix is correct, stock 2.38.0 has the wrong check)

---

## add_on/scriptstdstring/scriptstdstring.cpp

### [WARNING-FIX] Suppress -Wtype-limits
```diff
+#pragma GCC diagnostic ignored "-Wtype-limits"
```

### [YOUR-PATCH] Fixed UTF-8 characters in comments
```diff
-//  bool result = std::regex_match(std::wstring(L"abcd�fg"), pattern);
+//  bool result = std::regex_match(std::wstring(L"abcdefg"), pattern);
```

### [OLDER-THAN-STOCK] Removed StringRegexFind_Generic wrapper
### [OLDER-THAN-STOCK] Commented out scan/format generic registration
```diff
-	r = engine->RegisterGlobalFunction("uint scan(...)", ...);
-	r = engine->RegisterGlobalFunction("string format(...)", ...);
+//	r = engine->RegisterGlobalFunction("uint scan(...)", ...);
+//	r = engine->RegisterGlobalFunction("string format(...)", ...);
```

---

## add_on/weakref/weakref.cpp

### [YOUR-PATCH] Null comparison with expired weak refs
```diff
+	if(ref == nullptr && m_weakRefFlag)
+		return m_weakRefFlag->Get();
```
Returns true if comparing to null and the weak ref's target has been destroyed.

---

# SUMMARY BY CATEGORY

## Definitely Your Patches (carry forward)
- Version string "WIP"
- Callback signatures by value (asSFuncPtr, not const ref) — 5 sites
- `\e` escape sequence
- For-each auto handle behavior
- Const-to-non-const allowObjectConstruct guard
- GetInitFunc const
- asCScriptObject destructor ordering
- Forward declarations (asCScriptEngine, asCDataType)
- Bytecode register tracking fix (JMP/JMPP)
- Computed goto cast simplification
- liveObjects assertion
- Changed member access error message (removed format specifier)
- MatchFunctions declaration reorder
- All add_on patches (scriptany module linkage, scriptarray POD, datetime, dictionary, weakref, socket fixes)
- BNF comment modernization

## Older-Than-Stock (pre-2.38.0 base, features missing)
- No GetMessageCallback
- No ParseNamespace
- No isConst in GetProperty
- No GetScriptSectionName deprecation
- No tryCatchInfo stackSize
- No template function serialization
- No RETURN_VALUE_MAX_SIZE / E2K native calling
- No inherited property access validation
- No HUGE_VALF / isinf checks (using HUGE_VAL only)
- No lambda deterministic type check
- No SetReturnObject in generic
- No &out reference matching improvement
- Various other 2.38.0 features not present

## AI Handle-Resolve (DO NOT carry forward unless re-implementing)
- asOBJ_HANDLE_RESOLVE flag + mask update
- resolveId field on asCObjectType
- SetHandleResolveId API
- TXT_DEAD_HANDLE_ACCESS
- Opcodes 201-204 (LoadThisAndChk, ResolveThisAndChk, CmpResolveNull, RefCpyResolve)
- Dispatch table + instruction handlers
- EmitPrepareObject / EmitRefCpy helpers + all call sites
- CmpResolveNull emission block
- Resolve-at-cleanup sites (8 locations)
- extern "C" ResolveHandle
- InstrW_PTR assertion loosening
- as_objecttype.h include in as_context.cpp
- New opcodes in as_restore.cpp translation, as_scriptfunction.cpp switch cases

## Copyright Drift (cosmetic, fix when updating)
Multiple files have copyright years that predate the stock release. These are artifacts of patching forward from older versions.

## Warning Fixes (carry forward)
- `-Wimplicit-fallthrough` suppression + `[[fallthrough]]` in as_callfunc_x64_gcc.cpp
- `-Wextra` suppression in as_typeinfo.cpp
- `-Wtype-limits` suppression in scriptstdstring.cpp
