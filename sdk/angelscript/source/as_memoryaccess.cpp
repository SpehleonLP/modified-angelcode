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
// as_memoryaccess.cpp
//
// Infers the scope of memory a script function may read and write, by
// abstract interpretation of its bytecode
//

#include "as_config.h"

#ifndef AS_NO_COMPILER

#include "as_memoryaccess.h"
#include "as_scriptengine.h"
#include "as_module.h"
#include "as_scriptfunction.h"
#include "as_objecttype.h"
#include "as_property.h"

BEGIN_AS_NAMESPACE

static asEMemoryAccess Join(asEMemoryAccess a, asEMemoryAccess b)
{
	return a > b ? a : b;
}

static asSAbstractValue Value(asBYTE origin, asBYTE loads, asBYTE hold)
{
	asSAbstractValue v;
	v.origin = origin;
	v.loads  = loads;
	v.hold   = hold;
	v.slot   = asNO_SLOT;
	v.varRef = asNO_SLOT;
	return v;
}

// Anything the scanner cannot vouch for: a computed value, or a pointer it lost
static asSAbstractValue Unknown()
{
	return Value(asMA_PROGRAM, asMA_PROGRAM, asRH_OWNED);
}

static asSAbstractValue Null()
{
	return Value(asMA_NONE, asMA_PROGRAM, asRH_NONE);
}

static asSAbstractValue JoinValue(const asSAbstractValue &a, const asSAbstractValue &b)
{
	asSAbstractValue v;
	v.origin = a.origin > b.origin ? a.origin : b.origin;
	v.loads  = a.loads > b.loads ? a.loads : b.loads;
	v.hold   = a.hold > b.hold ? a.hold : b.hold;
	v.slot   = a.slot == b.slot ? a.slot : asNO_SLOT;
	v.varRef = a.varRef == b.varRef ? a.varRef : asNO_SLOT;
	return v;
}

static bool SameValue(const asSAbstractValue &a, const asSAbstractValue &b)
{
	return a.origin == b.origin && a.loads == b.loads && a.hold == b.hold && a.slot == b.slot && a.varRef == b.varRef;
}

// Not monotone in s on its own: WorldStable maps to itself, while This on an
// object of lower origin maps below it. Reads take the floor in
// asMemoryAccessReadContribution, and write scopes are never WorldStable, so
// neither use ever takes that step.
asEMemoryAccess asMemoryAccessContribution(asEMemoryAccess s, asBYTE objectOrigin)
{
	if( s <= asMA_WORLD_STABLE )
	{
		return s;
	}
	// A callee's This/Owned effects land on the object it was called on
	asEMemoryAccess own = s < asMA_OWNED ? s : asMA_OWNED;
	asEMemoryAccess r = objectOrigin == asMA_THIS ? own : asEMemoryAccess(objectOrigin);
	// The ladder puts This inside Module, so a Module callee still touches its object
	if( s >= asMA_MODULE )
	{
		r = Join(r, s);
	}
	return r;
}

// Every scope above WorldStable contains it, so a callee that touches only a
// local object may still read world-stable state. The floor keeps the
// object's origin from hiding that, and it also makes reads monotone in s
// (spec 2.2).
asEMemoryAccess asMemoryAccessReadContribution(asEMemoryAccess s, asBYTE objectOrigin)
{
	return Join(asMemoryAccessContribution(s, objectOrigin), s < asMA_WORLD_STABLE ? s : asMA_WORLD_STABLE);
}

asEMemoryAccess asMemoryAccessOfDestruction(asEMemoryAccess s)
{
	return s >= asMA_MODULE ? s : asMA_NONE;
}

asCMemoryAccessScanner::asCMemoryAccessScanner(asCScriptEngine *in_engine, asCModule *in_module, const asSMemoryAccessTable *in_table)
	: engine(in_engine), module(in_module), table(in_table), func(0), bc(0), bcLen(0), slotBase(0),
	  read(asMA_NONE), write(asMA_NONE), drops(false)
{
}

void asCMemoryAccessScanner::AccessOf(asCScriptFunction *f, asEMemoryAccess &r, asEMemoryAccess &w, bool &d) const
{
	if( f == 0 )
	{
		r = w = asMA_PROGRAM;
		d = true;
		return;
	}
	if( table )
	{
		asSMapNode<int, asUINT> *cursor = 0;
		if( table->funcIdToIndex->MoveTo(&cursor, f->id) )
		{
			asUINT i = table->funcIdToIndex->GetValue(cursor);
			r = asMemoryAccessRead(table->access[i]);
			w = asMemoryAccessWrite(table->access[i]);
			d = table->drops[i];
			return;
		}
	}
	r = asMemoryAccessRead(f->memoryAccess);
	w = asMemoryAccessWrite(f->memoryAccess);
	// Only this pass tracks the flag; any other callee that writes may drop a handle (spec 2.4)
	d = w > asMA_WORLD_STABLE;
}

void asCMemoryAccessScanner::DestructionOf(asCTypeInfo *type, asEMemoryAccess &r, asEMemoryAccess &w, bool &d)
{
	asCArray<asCTypeInfo*> visited;
	DestructionWalk(type, r, w, d, visited);
}

void asCMemoryAccessScanner::DestructionWalk(asCTypeInfo *type, asEMemoryAccess &r, asEMemoryAccess &w, bool &d, asCArray<asCTypeInfo*> &visited)
{
	UNUSED_VAR(type);
	UNUSED_VAR(visited);
	// Task 7 walks the type's destructor and members; until then any destruction may reach anything
	r = w = asMA_PROGRAM;
	d = true;
}

void asCMemoryAccessScanner::RecordRead(asBYTE origin)
{
	read = Join(read, asEMemoryAccess(origin));
}

void asCMemoryAccessScanner::RecordWrite(asBYTE origin)
{
	// Writing world-stable state contradicts its definition, so widen it
	asEMemoryAccess o = origin == asMA_WORLD_STABLE ? asMA_PROGRAM : asEMemoryAccess(origin);
	write = Join(write, o);
}

asSAbstractValue *asCMemoryAccessScanner::Var(State &s, int offset)
{
	int i = offset + slotBase;
	if( i < 0 || asUINT(i) >= s.vars.GetLength() )
	{
		return 0;
	}
	return &s.vars[asUINT(i)];
}

asCTypeInfo *asCMemoryAccessScanner::VarType(int offset) const
{
	int i = offset + slotBase;
	if( i < 0 || asUINT(i) >= varTypes.GetLength() )
	{
		return 0;
	}
	return varTypes[asUINT(i)];
}

void asCMemoryAccessScanner::Push(State &s, const asSAbstractValue &v, asUINT dwords)
{
	for( asUINT n = 0; n < dwords; n++ )
	{
		s.stack.PushLast(v);
	}
}

bool asCMemoryAccessScanner::Pop(State &s, asUINT dwords)
{
	if( s.stack.GetLength() < dwords )
	{
		return false;
	}
	s.stack.SetLength(s.stack.GetLength() - dwords);
	return true;
}

asSAbstractValue *asCMemoryAccessScanner::Cell(State &s, asUINT k)
{
	if( k >= s.stack.GetLength() )
	{
		return 0;
	}
	return &s.stack[s.stack.GetLength() - 1 - k];
}

// A pointer occupies AS_PTR_SIZE cells starting k dwords from the top
bool asCMemoryAccessScanner::SetCells(State &s, asUINT k, const asSAbstractValue &v)
{
	for( asUINT n = 0; n < AS_PTR_SIZE; n++ )
	{
		asSAbstractValue *c = Cell(s, k + n);
		if( c == 0 )
		{
			return false;
		}
		*c = v;
	}
	return true;
}

bool asCMemoryAccessScanner::Merge(State &into, const State &from, bool &mismatch)
{
	mismatch = false;
	if( !into.reached )
	{
		into = from;
		return true;
	}
	if( into.stack.GetLength() != from.stack.GetLength() )
	{
		// Valid bytecode always agrees here; disagreeing means the model lost track
		mismatch = true;
		return false;
	}
	bool changed = false;
	asSAbstractValue j;
	for( asUINT n = 0; n < into.vars.GetLength(); n++ )
	{
		j = JoinValue(into.vars[n], from.vars[n]);
		if( !SameValue(j, into.vars[n]) )
		{
			into.vars[n] = j;
			changed = true;
		}
	}
	for( asUINT n = 0; n < into.stack.GetLength(); n++ )
	{
		j = JoinValue(into.stack[n], from.stack[n]);
		if( !SameValue(j, into.stack[n]) )
		{
			into.stack[n] = j;
			changed = true;
		}
	}
	j = JoinValue(into.valueReg, from.valueReg);
	if( !SameValue(j, into.valueReg) )
	{
		into.valueReg = j;
		changed = true;
	}
	j = JoinValue(into.objectReg, from.objectReg);
	if( !SameValue(j, into.objectReg) )
	{
		into.objectReg = j;
		changed = true;
	}
	return changed;
}

void asCMemoryAccessScanner::InitialState(State &s)
{
	s.reached = true;
	s.vars.SetLength(varTypes.GetLength());
	for( asUINT n = 0; n < s.vars.GetLength(); n++ )
	{
		s.vars[n] = Unknown();
	}
	s.stack.SetLength(0);
	s.valueReg  = Unknown();
	s.objectReg = Null();

	// The VM clears heap-object locals on entry
	for( asUINT n = 0; n < func->scriptData->variables.GetLength(); n++ )
	{
		asSScriptVariable *v = func->scriptData->variables[n];
		if( v->stackOffset > 0 && v->onHeap )
		{
			asSAbstractValue *slot = Var(s, v->stackOffset);
			if( slot )
			{
				*slot = Null();
			}
		}
	}
	// Task 4 adds `this`, the return pointer and the parameters here
}

void asCMemoryAccessScanner::CatchState(State &s, asUINT stackSize)
{
	// An exception can leave any slot in any state the try block reached
	InitialState(s);
	for( asUINT n = 0; n < s.vars.GetLength(); n++ )
	{
		s.vars[n] = Unknown();
	}
	asSAbstractValue *self = func->objectType ? Var(s, 0) : 0;
	if( self )
	{
		*self = Value(asMA_THIS, asMA_PROGRAM, asRH_NONE);
	}
	s.stack.SetLength(0);
	Push(s, Unknown(), stackSize);
}

// asCArray frees raw memory and destroys only its first GetLength() elements,
// so shrinking an array of States with SetLength would leak each one's buffers
void asCMemoryAccessScanner::ReleaseStates()
{
	states.SetLength(states.GetCapacity());
	states.Allocate(0, false);
}

asSMemoryScanResult asCMemoryAccessScanner::Scan(asCScriptFunction *f)
{
	asSMemoryScanResult res;
	res.read = res.write = asMA_PROGRAM;
	res.dropsStoredHandle = true;
	if( f == 0 || f->scriptData == 0 )
	{
		return res;
	}

	func  = f;
	bc    = f->scriptData->byteCode.AddressOf();
	bcLen = f->scriptData->byteCode.GetLength();
	read  = write = asMA_NONE;
	drops = false;
	copiedReleases.SetLength(0);

	// Frame offsets run from the deepest parameter (negative) to the last local
	int argSpace = f->GetSpaceNeededForArguments() + (f->objectType ? AS_PTR_SIZE : 0) + (f->DoesReturnOnStack() ? AS_PTR_SIZE : 0);
	slotBase = argSpace + AS_PTR_SIZE;
	varTypes.SetLength(asUINT(slotBase + int(f->scriptData->variableSpace) + 2 * AS_PTR_SIZE + 1));
	for( asUINT n = 0; n < varTypes.GetLength(); n++ )
	{
		varTypes[n] = 0;
	}
	for( asUINT n = 0; n < f->scriptData->variables.GetLength(); n++ )
	{
		asSScriptVariable *v = f->scriptData->variables[n];
		int i = v->stackOffset + slotBase;
		if( i >= 0 && asUINT(i) < varTypes.GetLength() )
		{
			varTypes[asUINT(i)] = v->type.GetTypeInfo();
		}
	}

	ReleaseStates();
	states.SetLength(bcLen);
	for( asUINT n = 0; n < bcLen; n++ )
	{
		states[n].reached = false;
	}

	asCArray<asUINT> work;
	asCArray<asUINT> successors;
	bool ok = true;

	if( bcLen == 0 )
	{
		return res;
	}
	bool mismatch = false;
	State entry;
	InitialState(entry);
	Merge(states[0], entry, mismatch);
	work.PushLast(0);
	for( asUINT n = 0; n < f->scriptData->tryCatchInfo.GetLength(); n++ )
	{
		asUINT at = f->scriptData->tryCatchInfo[n].catchPos;
		if( at >= bcLen )
		{
			ok = false;
			break;
		}
		State c;
		CatchState(c, f->scriptData->tryCatchInfo[n].stackSize);
		if( Merge(states[at], c, mismatch) )
		{
			work.PushLast(at);
		}
		if( mismatch )
		{
			ok = false;
			break;
		}
	}

	while( ok && work.GetLength() )
	{
		asUINT pos = work.PopLast();
		State s = states[pos];
		successors.SetLength(0);
		if( !Step(pos, s, successors) )
		{
			ok = false;
			break;
		}
		for( asUINT n = 0; n < successors.GetLength(); n++ )
		{
			asUINT to = successors[n];
			if( to >= bcLen )
			{
				ok = false;
				break;
			}
			if( Merge(states[to], s, mismatch) )
			{
				work.PushLast(to);
			}
			if( mismatch )
			{
				ok = false;
				break;
			}
		}
	}

	// Task 7 adds exception clean-up and the copied-release rule here

	ReleaseStates();
	if( !ok )
	{
		return res;
	}
	res.read = read;
	res.write = write;
	res.dropsStoredHandle = drops;
	return res;
}

bool asCMemoryAccessScanner::Step(asUINT pos, State &s, asCArray<asUINT> &successors)
{
	asDWORD *instr = &bc[pos];
	asEBCInstr op = asEBCInstr(*(asBYTE*)instr);
	asUINT size = asBCTypeSize[asBCInfo[op].type];
	if( size == 0 )
	{
		return false;
	}
	asUINT next = pos + size;
	bool fallsThrough = true;

	switch( op )
	{
	// Control flow
	case asBC_RET:
		fallsThrough = false;
		break;
	case asBC_JMP:
		successors.PushLast(asUINT(int(next) + asBC_INTARG(instr)));
		fallsThrough = false;
		break;
	case asBC_JZ: case asBC_JNZ: case asBC_JS: case asBC_JNS: case asBC_JP: case asBC_JNP:
	case asBC_JLowZ: case asBC_JLowNZ:
		successors.PushLast(asUINT(int(next) + asBC_INTARG(instr)));
		break;
	case asBC_JMPP:
		// The jump table is the run of JMPs that follows
		for( asUINT t = next; t < bcLen && *(asBYTE*)&bc[t] == asBC_JMP; t += asBCTypeSize[asBCInfo[asBC_JMP].type] )
		{
			successors.PushLast(t);
		}
		fallsThrough = false;
		break;

	// Writes only the frame or the value register
	case asBC_NOT: case asBC_NEGi: case asBC_NEGf: case asBC_NEGd: case asBC_NEGi64:
	case asBC_IncVi: case asBC_DecVi: case asBC_BNOT: case asBC_BNOT64:
	case asBC_iTOf: case asBC_fTOi: case asBC_uTOf: case asBC_fTOu:
	case asBC_sbTOi: case asBC_swTOi: case asBC_ubTOi: case asBC_uwTOi:
	case asBC_iTOb: case asBC_iTOw: case asBC_dTOi64: case asBC_dTOu64: case asBC_i64TOd: case asBC_u64TOd:
	case asBC_SetV1: case asBC_SetV2: case asBC_SetV4: case asBC_SetV8:
	case asBC_dTOi: case asBC_dTOu: case asBC_dTOf: case asBC_iTOd: case asBC_uTOd: case asBC_fTOd:
	case asBC_i64TOi: case asBC_uTOi64: case asBC_iTOi64: case asBC_fTOi64: case asBC_fTOu64: case asBC_i64TOf: case asBC_u64TOf:
	case asBC_ADDIi: case asBC_SUBIi: case asBC_MULIi: case asBC_ADDIf: case asBC_SUBIf: case asBC_MULIf:
	case asBC_BAND: case asBC_BOR: case asBC_BXOR: case asBC_BSLL: case asBC_BSRL: case asBC_BSRA:
	case asBC_ADDi: case asBC_SUBi: case asBC_MULi: case asBC_DIVi: case asBC_MODi:
	case asBC_ADDf: case asBC_SUBf: case asBC_MULf: case asBC_DIVf: case asBC_MODf:
	case asBC_ADDd: case asBC_SUBd: case asBC_MULd: case asBC_DIVd: case asBC_MODd:
	case asBC_ADDi64: case asBC_SUBi64: case asBC_MULi64: case asBC_DIVi64: case asBC_MODi64:
	case asBC_BAND64: case asBC_BOR64: case asBC_BXOR64: case asBC_BSLL64: case asBC_BSRL64: case asBC_BSRA64:
	case asBC_DIVu: case asBC_MODu: case asBC_DIVu64: case asBC_MODu64:
	case asBC_POWi: case asBC_POWu: case asBC_POWf: case asBC_POWd: case asBC_POWdi: case asBC_POWi64: case asBC_POWu64:
	{
		asSAbstractValue *v = Var(s, asBC_SWORDARG0(instr));
		if( v == 0 )
		{
			return false;
		}
		*v = Unknown();
		break;
	}
	case asBC_CMPd: case asBC_CMPu: case asBC_CMPf: case asBC_CMPi: case asBC_CMPi64: case asBC_CMPu64:
	case asBC_CMPIi: case asBC_CMPIf: case asBC_CMPIu:
	case asBC_TZ: case asBC_TNZ: case asBC_TS: case asBC_TNS: case asBC_TP: case asBC_TNP:
	case asBC_ClrHi: case asBC_CmpPtr:
		s.valueReg = Unknown();
		break;

	// Moves copy the abstract value, so a pointer moved by a 4-byte op on a
	// 32-bit build keeps its origin
	case asBC_CpyVtoV4: case asBC_CpyVtoV8:
	{
		asSAbstractValue *to = Var(s, asBC_SWORDARG0(instr));
		asSAbstractValue *from = Var(s, asBC_SWORDARG1(instr));
		if( to == 0 || from == 0 )
		{
			return false;
		}
		*to = *from;
		break;
	}
	case asBC_CpyVtoR4: case asBC_CpyVtoR8:
	{
		asSAbstractValue *from = Var(s, asBC_SWORDARG0(instr));
		if( from == 0 )
		{
			return false;
		}
		// A 4-byte write leaves the high half of the old register on 64-bit hosts
		s.valueReg = (op == asBC_CpyVtoR8 || AS_PTR_SIZE == 1) ? *from : Unknown();
		break;
	}
	case asBC_CpyRtoV4: case asBC_CpyRtoV8:
	{
		asSAbstractValue *to = Var(s, asBC_SWORDARG0(instr));
		if( to == 0 )
		{
			return false;
		}
		*to = (op == asBC_CpyRtoV8 || AS_PTR_SIZE == 1) ? s.valueReg : Unknown();
		break;
	}

	// Stack traffic of values and non-data pointers
	case asBC_PshC4: case asBC_TYPEID:
		Push(s, Unknown(), 1);
		break;
	case asBC_PshC8:
		Push(s, Unknown(), 2);
		break;
	case asBC_PshV4: case asBC_PshV8:
	{
		asSAbstractValue *from = Var(s, asBC_SWORDARG0(instr));
		if( from == 0 )
		{
			return false;
		}
		Push(s, *from, op == asBC_PshV8 ? 2 : 1);
		break;
	}
	case asBC_PopPtr:
		if( !Pop(s, AS_PTR_SIZE) )
		{
			return false;
		}
		break;
	case asBC_PshNull:
		Push(s, Null(), AS_PTR_SIZE);
		break;
	case asBC_OBJTYPE: case asBC_FuncPtr:
		// Engine metadata and code pointers: immutable while the VM runs
		Push(s, Value(asMA_NONE, asMA_PROGRAM, asRH_NONE), AS_PTR_SIZE);
		break;
	case asBC_SwapPtr:
	{
		if( s.stack.GetLength() < 2 * AS_PTR_SIZE )
		{
			return false;
		}
		asSAbstractValue a = *Cell(s, 0), b = *Cell(s, AS_PTR_SIZE);
		SetCells(s, 0, b);
		SetCells(s, AS_PTR_SIZE, a);
		break;
	}

	// No effect on memory
	case asBC_JitEntry: case asBC_CHKREF: case asBC_ChkNullS: case asBC_ChkNullV:
		break;
	// The line callback is host code outside the analysis, like the GC
	case asBC_SUSPEND:
		break;

	default:
		// Fail closed: an opcode this scanner does not model makes the function Program
		return false;
	}

	if( fallsThrough )
	{
		successors.PushLast(next);
	}
	return true;
}

END_AS_NAMESPACE

#endif // AS_NO_COMPILER
