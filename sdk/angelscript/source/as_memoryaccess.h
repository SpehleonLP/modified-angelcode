/*
   AngelCode Scripting Library
   Copyright (c) 2003-2025 Andreas Jonsson

   This software is provided 'as-is', without any express or implied
   warranty. In no event will the authors be held liable for any
   damages arising from the use of this software.

   Permission is granted to anyone to use this software for any
   purpose, including commercial applications, and to alter it and
   redistribute it freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you
      must not claim that you wrote the original software. If you use
      this software in a product, an acknowledgment in the product
      documentation would be appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and
      must not be misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
      distribution.

   The original version of this library can be located at:
   http://www.angelcode.com/angelscript/

   Andreas Jonsson
   andreas@angelcode.com
*/



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
// The address of some frame slot, but paths disagree on which one
const short asANY_SLOT = 0x7FFE;

// The abstract value of one dword cell: a frame slot, a stack cell or a
// register. Origins are asEMemoryAccess values. asMA_THIS as an origin means
// "the object this function was called on", which is what the call rule rebases.
struct asSAbstractValue
{
	asBYTE origin;  // scope of memory reached through this value
	asBYTE loads;   // origin of a pointer read through this value (RDSPtr)
	asBYTE hold;    // asEReferenceHold
	short  slot;    // frame offset this value is the address of, asNO_SLOT, or asANY_SLOT
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
// The read side of asMemoryAccessContribution, floored at WorldStable for any callee above None (spec 2.2)
asEMemoryAccess asMemoryAccessReadContribution(asEMemoryAccess callee, asBYTE objectOrigin);
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
	// A pointer or 8-byte value spans the base cell and the one below it
	bool              SetVarCells(State &s, int offset, const asSAbstractValue &v, asUINT dwords);
	// A write through `addr` may change the frame slot it points to
	void              ForgetSlot(State &s, const asSAbstractValue &addr, asUINT dwords);
	// The dwords of the variable at `offset`, or 0 when no variable starts there
	asUINT            VarDwords(int offset) const;
	// A callee handed the address `a` may have written the whole variable it points into
	void              ForgetArgument(State &s, const asSAbstractValue &a);

	// 0 for an id outside the engine's function table
	asCScriptFunction *FunctionById(int id) const;
	// Applies one call's effects and stack traffic; `callee` gives the signature
	bool              DoCall(State &s, asCScriptFunction *callee, asUINT kind);
	// The type behaviours that construct: they return an object no one else holds (spec 2.5)
	bool              IsFactory(asCScriptFunction *callee) const;
	void              CalleeAccess(asCScriptFunction *callee, asUINT kind, asEMemoryAccess &read, asEMemoryAccess &write, bool &drops);
	asBYTE            FieldLoads(asBYTE baseOrigin, int typeId, int offset);
	// The scope of the storage a global's address operand names
	asBYTE            GlobalOrigin(void *address);
	// The origin of the pointer stored in that global
	asBYTE            GlobalLoads(void *address);
	// The pointer occupying AS_PTR_SIZE cells k dwords from the top
	bool              PtrAt(State &s, asUINT dwordsFromTop, asSAbstractValue &v);
	// The address `offset` bytes into what `base` points to
	asSAbstractValue  FieldOf(const asSAbstractValue &base, asBYTE loads, int offset);

	void              RecordRead(asBYTE origin);
	void              RecordWrite(asBYTE origin);

	// Releasing `v`, a reference to an object of static type `type` (spec 2.4)
	void              ReleaseValue(const asSAbstractValue &v, asCTypeInfo *type);
	// A release that may be the last one
	void              DestroyUnbalanced(asCTypeInfo *type);
	// A handle assignment into `dest`: releases the old value, AddRefs the new one
	bool              StoreHandle(State &s, const asSAbstractValue &dest, const asSAbstractValue &value, asCTypeInfo *type);
	// What the VM releases or destroys of a system function's arguments after it returns
	bool              CleanNativeArgs(State &s, asCScriptFunction *callee, asUINT firstArg);

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
