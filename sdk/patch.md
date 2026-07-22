# AngelScript Custom Patches

**Base:** Stock AngelScript release (tested against 2.38.0)
**Purpose:** Apply these patches after extracting a new AngelScript release.
**Source root:** `sdk/angelscript/` for engine, `sdk/add_on/` for add-ons.

All engine-source changes are warning fixes only. The real patches are in the add-ons.

---

## Engine Warning Fixes

These suppress GCC warnings on the stock source. No behavioral changes.

### source/as_callfunc_x64_gcc.cpp

**1. Add pragma at very top of file (before the copyright comment):**
```cpp
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
```

**2. Add `[[fallthrough]];` in two switch cases:**

After the `#endif` following `ICC_VIRTUAL_THISCALL_OBJLAST` / `param_post = 2;` (before `case ICC_THISCALL:`):
```cpp
		[[fallthrough]];
```

After the `#endif` following `ICC_VIRTUAL_THISCALL_OBJLAST_RETURNINMEM` / `param_post = 2;` (before `case ICC_THISCALL_RETURNINMEM:`):
```cpp
		[[fallthrough]];
```

### source/as_typeinfo.cpp

**Add pragma at very top of file (before the copyright comment):**
```cpp
#pragma GCC diagnostic ignored "-Wextra"
```

### add_on/scriptstdstring/scriptstdstring.cpp

**Add pragma at very top of file (before `#include "scriptstdstring.h"`):**
```cpp
#pragma GCC diagnostic ignored "-Wtype-limits"
```

---

## add_on/datetime

### datetime.h

**Add time_point constructor after the 3-arg constructor:**
```cpp
	CDateTime(const std::chrono::system_clock::time_point &other) : tp(other) {};
```

### datetime.cpp

**Remove `static` from `time_point_to_tm`:**
```diff
-static tm time_point_to_tm(const std::chrono::time_point<std::chrono::system_clock> &tp)
+tm time_point_to_tm(const std::chrono::time_point<std::chrono::system_clock> &tp)
```

---

## add_on/scriptany

### scriptany.h

**Add includes and define after the angelscript include guard block:**
```cpp
#define REMOVE_MODULE_LINKAGE 1

#if REMOVE_MODULE_LINKAGE
#include <atomic>
#include <mutex>
#endif
```

**Add `MovePointer` enum and `RemoverModule` at the top of the `CScriptAny` class (before `// Constructors`):**
```cpp
public:
	enum MovePointer
	{
		isReference,
		isHandle,
		matchTypeId,
	};
```

**Add after constructors (before `// Memory management`):**
```cpp
#if REMOVE_MODULE_LINKAGE
	static void RemoverModule(asIScriptModule* _module);
	bool Depends(asIScriptModule * _module) const;
#endif
```

**Add `StoreMove` before `operator=`:**
```cpp
//take ownership of script object instead of copying it.
	void StoreMove(void *ref, int refTypeId, MovePointer);
```

**Add after `ReleaseAllHandles`:**
```cpp
	void * GetObjectAddress();

//used to determine if it has changed.
//cleared by FreeObject()
	void setMark() { mark = true; }
	bool getMark() const { return mark; }
```

**Add fields after `gcFlag`:**
```cpp
	bool mark{};

#if REMOVE_MODULE_LINKAGE
	//mutable/volatile is not the same as atomic. the check bool, lock mutex, check again will only work with atomics not the other two.
	std::atomic<bool> m_isInList{};
#endif
```

**Add after `valueStruct value;`:**
```cpp
#if REMOVE_MODULE_LINKAGE
	bool ShouldBeInList() const;
	inline void UpdateListUnsafe() { ShouldBeInList()? AddToListUnsafe() : RemoveFromListUnsafe();  }
	inline void UpdateListSafe() { ShouldBeInList()? AddToListSafe() : RemoveFromListSafe();  }

//maintain a linked list of anys so we can delete everything _module linked
//probably a way to do this using the existing garbage collector instead.
//there are cases that isn't valid tho
	void AddToListUnsafe();
	void RemoveFromListUnsafe();

	void AddToListSafe();
	void RemoveFromListSafe();

	static std::recursive_mutex g_mutex;
	static CScriptAny * g_first;
	static CScriptAny * g_last;

	CScriptAny * m_next{};
	CScriptAny * m_prev{};
#endif
```

### scriptany.cpp

**Add `UpdateListSafe()` call in `operator=` (after the value copy block, before `return *this`):**
```cpp
#if REMOVE_MODULE_LINKAGE
	UpdateListSafe();
#endif
```

**Add in destructor (after `FreeObject()`):**
```cpp
#if REMOVE_MODULE_LINKAGE
	UpdateListSafe();
#endif
```

**Add `StoreMove` method after `Store(void*, int)` method:**
```cpp
void CScriptAny::StoreMove(void *ref, int refTypeId, MovePointer status)
{
	// This method is not expected to be used for primitive types, except for bool, int64, or double
	assert( refTypeId > asTYPEID_DOUBLE || refTypeId == asTYPEID_VOID || refTypeId == asTYPEID_BOOL || refTypeId == asTYPEID_INT64 || refTypeId == asTYPEID_DOUBLE );

	// Hold on to the object type reference so it isn't destroyed too early
	if( (refTypeId & asTYPEID_MASK_OBJECT) )
	{
		asITypeInfo *ti = engine->GetTypeInfoById(refTypeId);
		if( ti )
			ti->AddRef();
	}

	FreeObject();

	if(status == matchTypeId)
		status = (refTypeId & asTYPEID_OBJHANDLE)? isHandle : isReference;

	value.typeId = refTypeId;
	if( value.typeId & asTYPEID_OBJHANDLE || value.typeId & asTYPEID_SCRIPTOBJECT )
	{
		value.valueObj = status == isHandle? *(void**)ref : ref;
	}
	else if( value.typeId & asTYPEID_APPOBJECT )
	{
		auto typeInfo = engine->GetTypeInfoById(refTypeId);

//delegate or reftype
		if(!(typeInfo->GetFlags() & asOBJ_VALUE))
		{
			value.valueObj = status == isHandle? *(void**)ref : ref;
		}
		else
		{
			// Create a copy of the object
			value.valueObj = engine->CreateScriptObjectCopy(ref, engine->GetTypeInfoById(value.typeId));
		}
	}
	else
	{
		// Primitives can be copied directly
		value.valueInt = 0;

		// Copy the primitive value
		// We receive a pointer to the value.
		int size = engine->GetSizeOfPrimitiveType(value.typeId);
		memcpy(&value.valueInt, ref, size);
	}

#if REMOVE_MODULE_LINKAGE
	UpdateListSafe();
#endif
}
```

**In `FreeObject()`, add after existing cleanup and before closing brace:**
```cpp
#if REMOVE_MODULE_LINKAGE
	UpdateListSafe();
#endif
	// For primitives, there's nothing to do

//mark as asTYPEID_VOID
	value.typeId = asTYPEID_VOID;
//clear mark so we know something changed
	mark = false;
```

**Add `GetObjectAddress` method:**
```cpp
void * CScriptAny::GetObjectAddress()
{
	if((value.typeId & asTYPEID_MASK_SEQNBR) == value.typeId)
		return &value.valueInt;

	if(value.typeId & asTYPEID_OBJHANDLE)
		return &value.valueObj;

	return value.valueObj;
}
```

**Add before `END_AS_NAMESPACE`:**
```cpp
#if REMOVE_MODULE_LINKAGE

void CScriptAny::RemoverModule(asIScriptModule* _module)
{
	std::lock_guard<std::recursive_mutex> lock(g_mutex);

	for(auto itr = g_first, next = g_first; itr; itr = next)
	{
		next = itr->m_next;

		if(itr->Depends(_module))
			itr->FreeObject();
	}
}

std::recursive_mutex CScriptAny::g_mutex;
CScriptAny * CScriptAny::g_first{};
CScriptAny * CScriptAny::g_last{};

void CScriptAny::AddToListSafe()
{
	if(m_isInList.load(std::memory_order_acquire) == false)
	{
		std::lock_guard<std::recursive_mutex> lock(g_mutex);
		return AddToListUnsafe();
	}
}

void CScriptAny::AddToListUnsafe()
{
	if(m_isInList.load(std::memory_order_acquire) == false)
	{
		if(!g_first)	g_first = this;
		if(g_last)      g_last->m_next = this;

		m_prev = g_last;
		m_next = nullptr;
		g_last = this;

		m_isInList.store(true, std::memory_order_release);
	}
}

void CScriptAny::RemoveFromListSafe()
{
	if(m_isInList.load(std::memory_order_acquire))
	{
		std::lock_guard<std::recursive_mutex> lock(g_mutex);
		return RemoveFromListUnsafe();
	}
}

void CScriptAny::RemoveFromListUnsafe()
{
	if(m_isInList.load(std::memory_order_acquire))
	{
		if(m_prev)			 m_prev->m_next = m_next;
		if(m_next)			 m_next->m_prev = m_prev;
		if(g_first == this)  g_first = m_next;
		if(g_last == this)   g_last  = m_prev;
		m_prev = nullptr;
		m_next = nullptr;

		m_isInList.store(false, std::memory_order_release);
	}
}

bool CScriptAny::ShouldBeInList() const
{
	if((value.typeId & asTYPEID_MASK_OBJECT) == false
	|| engine == nullptr)
		return false;

//i think templates count as appobjects even if they're _module dependent?
	auto typeInfo = engine->GetTypeInfoById(value.typeId);

	return typeInfo? typeInfo->GetModule() != nullptr : false;
}

bool CScriptAny::Depends(asIScriptModule * _module) const
{
	if((value.typeId & asTYPEID_MASK_OBJECT) == false
	|| engine == nullptr)
		return false;

	return _module == engine->GetTypeInfoById(value.typeId)->GetModule();
}

#endif
```

---

## add_on/scriptarray

### scriptarray.h

**Replace forward declaration of `SArrayBuffer` with full struct + helpers. Replace:**
```cpp
struct SArrayBuffer;
```
**With:**
```cpp
struct SArrayBuffer
{
	asQWORD userData;
	asDWORD maxElements;
	asDWORD numElements;
//todo: garuntee this is 16 byte aligned
	asBYTE  data[1];
};
```

**Add before `class CScriptArray`:**
```cpp
class asCScriptFunction;
class CScriptArray;
struct ArrayHelper;

asUINT CScriptArray_FilterObj(void * obj, asUINT startAt, asUINT count, CScriptArray * array);
asUINT CScriptArray_FilterObjInternal(void * obj, asUINT startAt, asUINT count, CScriptArray * array);
asUINT CScriptArray_Filter(asIScriptFunction * obj, asUINT startAt, asUINT count, CScriptArray * array);
asUINT CScriptArray_Unique(asUINT startAt, asUINT count, CScriptArray * array);
asUINT CScriptArray_UniqueRef(asUINT startAt, asUINT count, CScriptArray * array);
asUINT CScriptArray_UniqueInternal(asUINT startAt, asUINT count, CScriptArray * array, bool);
```

**Add public methods after `ReleaseAllHandles`:**
```cpp
	bool TryOpConv(void *value, int typeId);
	void Swap(CScriptArray* it);
	void setUserData(uint64_t user_data) { buffer->userData = user_data; }
	uint64_t getUserData() const { return buffer->userData; }
	auto GetElementSize() const { return elementSize; }
```

**Add friend declarations and change field types in protected section:**
```cpp
protected:
friend asUINT CScriptArray_FilterObjInternal(void * obj, asUINT startAt, asUINT count, CScriptArray * array);
friend asUINT CScriptArray_Filter(asIScriptFunction * obj, asUINT startAt, asUINT count, CScriptArray * array);
friend asUINT CScriptArray_UniqueInternal(asUINT startAt, asUINT count, CScriptArray * array, bool);
friend struct ArrayHelper;
	mutable int     refCount;
	mutable bool    gcFlag;
	bool			isValueType;
	int16_t		    elementSize;
	asITypeInfo    *objType;
	SArrayBuffer   *buffer;
	int             subTypeId;
```
(Note: `elementSize` changed from `int` to `int16_t`, `isValueType` added, field order changed.)

**Add protected methods after `Equals`:**
```cpp
	bool TryNormalConstructor(asCScriptFunction * function, void *value, asITypeInfo * typeInfo, bool isHandle);
	bool TryListConstructor(asCScriptFunction * function, void *value, asITypeInfo * typeInfo, bool isHandle);

	template<typename T>
	asUINT UniqueT(asUINT start, asUINT end);
```

### scriptarray.cpp

**Add include after `#include "scriptarray.h"`:**
```cpp
#include "AngelScript/Native/as_arraysupport.h"
```

**Remove the `SArrayBuffer` struct definition** (now in header).

**Remove the entire `ScriptArrayTemplateCallback` function** (~120 lines). Replace its usage with an external `ScriptArrayTemplateCallback2`:

In `RegisterScriptArray_Native`, change:
```diff
-	r = engine->RegisterObjectBehaviour("array<T>", asBEHAVE_TEMPLATE_CALLBACK, "bool f(int&in, bool&out)", asFUNCTION(ScriptArrayTemplateCallback), asCALL_CDECL);
+	bool ScriptArrayTemplateCallback2(asITypeInfo *ti, bool &dontGarbageCollect);
+	r = engine->RegisterObjectBehaviour("array<T>", asBEHAVE_TEMPLATE_CALLBACK, "bool f(int&in, bool&out)", asFUNCTION(ScriptArrayTemplateCallback2), asCALL_CDECL);
```

**In all constructors, change element size determination to handle POD value types inline:**

Where the code determines `elementSize`:
```diff
 	if( subTypeId & asTYPEID_MASK_OBJECT )
+	{
 		elementSize = sizeof(asPWORD);
+		isValueType = (ti->GetSubType(0)->GetFlags() & asOBJ_POD) != 0;
+		if(isValueType) elementSize = ti->GetSubType()->GetSize();
+	}
 	else
+	{
 		elementSize = engine->GetSizeOfPrimitiveType(subTypeId);
+		isValueType = true;
+	}
```
(This pattern appears in 4 constructors. Also copy constructor needs `isValueType = other.isValueType;`.)

**Initialize userData in CreateBuffer:**
```diff
 		(*buf)->numElements = numElements;
 		(*buf)->maxElements = numElements;
+		(*buf)->userData = 0;
```

**Guard Construct/Destruct/At/assignment/EnumReferences with `isValueType`:**

In `Construct`, `Destruct`, `operator[]` (At), and the copy loop in the assignment operator, add `!isValueType &&` before `(subTypeId & asTYPEID_MASK_OBJECT)` checks. In `EnumReferences`, add early return:
```cpp
	if(isValueType) return;
```

**In generic registration, move `opAssign` registration before the `opFor*` methods** (order change only).

**In generic `ScriptArrayTemplateCallback_Generic`, change to use `ScriptArrayTemplateCallback2`.**

---

## add_on/scriptdictionary

### scriptdictionary.h

**Add friend and accessor:**
```cpp
friend void CScriptDictionary_OpConvInner(void * ref, int typeId, CScriptDictionary* This);
```
```cpp
	asIScriptEngine * GetEngine() const { return engine; }
```

**Add free function declaration before `CScriptDictValue`:**
```cpp
void CScriptDictionary_OpConvInner(void * ref, int typeId, CScriptDictionary* This);
```

### scriptdictionary.cpp

**Add `valueType` to `SDictionaryCache`:**
```cpp
	asITypeInfo *valueType;
```
And in `Setup`:
```cpp
	cache->valueType = engine->GetTypeInfoByDecl("dictionaryValue");
```

**Value-initialize locals:**
```diff
-	asINT64 value;
+	asINT64 value{};
-	double value;
+	double value{};
```

---

## add_on/weakref

### weakref.cpp

**In `CScriptWeakRef::Equals`, add null check at the top:**
```cpp
	if(ref == nullptr && m_weakRefFlag)
	{
		return m_weakRefFlag->Get();
	}
```
(Returns true when comparing to null if the weak ref's target has been destroyed.)

---

## add_on/scriptsocket

### scriptsocket.h

**Remove `#ifdef _WIN32` / `#endif` guards around the class.** The class should always be available.

**Remove `IsActive()` method declaration.**

### scriptsocket.cpp

**Remove `#include "../autowrapper/aswrappedcall.h"`.**

**Remove outer `#ifdef _WIN32` guard** (keep the inner one for WSA includes).

**Change listen queue size:**
```diff
-	listen(m_socket, 25);
+	listen(m_socket, 5);
```

**In `Select()`, remove the `if (!IsActive()) return -1;` check at the top, and remove the error-close block after `select()`.**

**In `Close()`, fix the bug:**
```diff
-	if (m_socket == -1)
+	if (m_socket != -1)
```

**In `Send()`, remove the error-close block** (just `return -1` on `SOCKET_ERROR`).

**In `Receive()`, remove the graceful-close handling** (`r == 0` closing the socket) and the error-close in the else branch.

**Remove `IsActive()` implementation entirely.**

**Remove `RegisterScriptSocket_Generic` function entirely.**

**Remove `RegisterScriptSocket_Native` — rename it to `RegisterScriptSocket` directly** (remove the dispatch function that chose between native and generic).

**Remove the `#else` / `asNOT_SUPPORTED` fallback at the bottom.**

---

## Notes for Future Updates

- The `ScriptArrayTemplateCallback2` function and `as_arraysupport.h` are defined in the Kreatures engine source (`Engine/src/AngelScript/Native/`), not in the AS SDK. The add-on patches assume these exist.
- The `CScriptDictionary_OpConvInner` function is also defined engine-side.
- If a new AS version changes the `SArrayBuffer` struct, the header patch will need updating.
- The `REMOVE_MODULE_LINKAGE` system in scriptany is needed because the engine hot-reloads script modules while `CScriptAny` instances persist in native code.
