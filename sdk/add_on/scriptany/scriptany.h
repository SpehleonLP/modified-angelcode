#ifndef SCRIPTANY_H
#define SCRIPTANY_H

#ifndef ANGELSCRIPT_H
// Avoid having to inform include path if header is already include before
#include <angelscript.h>
#endif

#define REMOVE_MODULE_LINKAGE 1

#if REMOVE_MODULE_LINKAGE
#include <atomic>
#include <mutex>
#endif


BEGIN_AS_NAMESPACE

class CScriptAny
{
public:
	enum MovePointer
	{
		isReference,
		isHandle,
		matchTypeId,
	};
	
	// Constructors
	CScriptAny(asIScriptEngine *engine);
	CScriptAny(void *ref, int refTypeId, asIScriptEngine *engine);

#if REMOVE_MODULE_LINKAGE
	static void RemoverModule(asIScriptModule* _module);
	bool Depends(asIScriptModule * _module) const;
#endif
	// Memory management
	int AddRef() const;
	int Release() const;

//take ownership of script object instead of copying it.
	void StoreMove(void *ref, int refTypeId, MovePointer);
	
	// Copy the stored value from another any object
	CScriptAny &operator=(const CScriptAny&);
	int CopyFrom(const CScriptAny *other);

	// Store the value, either as variable type, integer number, or real number
	void Store(void *ref, int refTypeId);
	void Store(asINT64 &value);
	void Store(double &value);

	// Retrieve the stored value, either as variable type, integer number, or real number
	bool Retrieve(void *ref, int refTypeId) const;
	bool Retrieve(asINT64 &value) const;
	bool Retrieve(double &value) const;

	// Get the type id of the stored value
	int  GetTypeId() const;

	// GC methods
	int  GetRefCount();
	void SetFlag();
	bool GetFlag();
	void EnumReferences(asIScriptEngine *engine);
	void ReleaseAllHandles(asIScriptEngine *engine);

	void * GetObjectAddress();

//used to determine if it has changed.
//cleared by FreeObject()
	void setMark() { mark = true; }
	bool getMark() const { return mark; }

protected:
	virtual ~CScriptAny();
	void FreeObject();

	mutable int refCount;
	mutable bool gcFlag;
	bool mark{};
	
#if REMOVE_MODULE_LINKAGE
	//mutable/volatile is not the same as atomic. the check bool, lock mutex, check again will only work with atomics not the other two.
	std::atomic<bool> m_isInList{};
#endif

	asIScriptEngine *engine;

	// The structure for holding the values
    struct valueStruct
    {
        union
        {
            asINT64 valueInt;
            double  valueFlt;
            void   *valueObj;
        };
        int   typeId;
    };

	valueStruct value;
	
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
};

void RegisterScriptAny(asIScriptEngine *engine);
void RegisterScriptAny_Native(asIScriptEngine *engine);
void RegisterScriptAny_Generic(asIScriptEngine *engine);

END_AS_NAMESPACE

#endif
