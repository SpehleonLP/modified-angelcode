//
// as_memoryaccess.h
//
// Infers the scope of memory a script function may read and write
//

#ifndef AS_MEMORYACCESS_H
#define AS_MEMORYACCESS_H

#include "as_config.h"

#ifndef AS_NO_COMPILER

#include "as_array.h"
#include "as_map.h"

BEGIN_AS_NAMESPACE

class asCScriptEngine;
class asCModule;
class asCScriptFunction;
class asCTypeInfo;

// How a frame slot or register holds the reference it points to. It decides
// whether releasing that reference can destroy the object (spec 2.4).
// Ordered so that the join of two holds is their max.
enum asEReferenceHold
{
	asRH_NONE   = 0, // null, or borrowed: never released
	asRH_COPIED = 1, // an AddRef'd copy of a reference some location still holds
	asRH_OWNED  = 2  // the only reference this frame knows of, or unknown
};

const short asNO_SLOT = 0x7FFF;

// The abstract value of one dword cell: a frame slot, a stack cell or a
// register. Origins are asEMemoryAccess values. asMA_THIS as an origin means
// "the object this function was called on", which is what the call rule rebases.
struct asSAbstractValue
{
	asBYTE origin;  // scope of memory reached through this value
	asBYTE loads;   // origin of a pointer read through this value (RDSPtr)
	asBYTE hold;    // asEReferenceHold
	short  slot;    // frame offset this value is the address of, or asNO_SLOT
	short  varRef;  // frame offset an asBC_VAR placeholder stands for, or asNO_SLOT
};

// The in-progress results of one module pass, indexed like m_scriptFunctions
struct asSMemoryAccessTable
{
	const asCMap<int, asUINT> *funcIdToIndex;
	asCArray<asBYTE>           access;
	asCArray<bool>             drops;
};

struct asSMemoryScanResult
{
	asEMemoryAccess read;
	asEMemoryAccess write;
	bool            dropsStoredHandle;
};

// What calling a function with scope `callee` on an object reached by `objectOrigin` adds to the caller (spec 2.2)
asEMemoryAccess asMemoryAccessContribution(asEMemoryAccess callee, asBYTE objectOrigin);
// What destroying an object adds, given its destructor's scope: the dying object's own memory is private (spec 2.4)
asEMemoryAccess asMemoryAccessOfDestruction(asEMemoryAccess s);

class asCMemoryAccessScanner
{
public:
	// table may be 0: every callee is then read from its stored byte
	asCMemoryAccessScanner(asCScriptEngine *engine, asCModule *module, const asSMemoryAccessTable *table);

	asSMemoryScanResult Scan(asCScriptFunction *func);

	// The current scopes of a callee, and whether calling it may drop a stored handle
	void AccessOf(asCScriptFunction *func, asEMemoryAccess &read, asEMemoryAccess &write, bool &drops) const;

	// The scope destroying an object of static type `type` may reach (spec 2.4)
	// `drops` reports whether the destruction may itself drop a stored handle
	void DestructionOf(asCTypeInfo *type, asEMemoryAccess &read, asEMemoryAccess &write, bool &drops);

protected:
	struct State
	{
		bool                       reached;
		asCArray<asSAbstractValue> vars;
		asCArray<asSAbstractValue> stack;
		asSAbstractValue           valueReg;
		asSAbstractValue           objectReg;
	};

	void              InitialState(State &s);
	void              CatchState(State &s, asUINT stackSize);
	bool              Step(asUINT pos, State &s, asCArray<asUINT> &successors);
	// Returns whether `into` changed; sets `mismatch` when the stack depths disagree
	bool              Merge(State &into, const State &from, bool &mismatch);
	void              ReleaseStates();

	asSAbstractValue *Var(State &s, int offset);
	asCTypeInfo      *VarType(int offset) const;
	void              Push(State &s, const asSAbstractValue &v, asUINT dwords);
	bool              Pop(State &s, asUINT dwords);
	asSAbstractValue *Cell(State &s, asUINT dwordsFromTop);
	bool              SetCells(State &s, asUINT dwordsFromTop, const asSAbstractValue &v);

	void              RecordRead(asBYTE origin);
	void              RecordWrite(asBYTE origin);

	void DestructionWalk(asCTypeInfo *type, asEMemoryAccess &read, asEMemoryAccess &write, bool &drops, asCArray<asCTypeInfo*> &visited);

	asCScriptEngine            *engine;
	asCModule                  *module;
	const asSMemoryAccessTable *table;

	// Per scan
	asCScriptFunction          *func;
	asDWORD                    *bc;
	asUINT                      bcLen;
	int                         slotBase;
	asCArray<asCTypeInfo*>      varTypes;
	asEMemoryAccess             read;
	asEMemoryAccess             write;
	bool                        drops;
	asCArray<asCTypeInfo*>      copiedReleases;
	asCArray<State>             states;
};

END_AS_NAMESPACE

#endif // AS_NO_COMPILER

#endif
