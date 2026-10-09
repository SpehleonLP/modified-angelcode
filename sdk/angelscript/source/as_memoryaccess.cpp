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
	v.originType = 0;
	v.loadsType  = 0;
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

// The address of a frame variable holding `held`
static asSAbstractValue AddressOf(const asSAbstractValue &held)
{
	asSAbstractValue v = Value(asMA_NONE, held.origin, asRH_NONE);
	v.loadsType = held.originType;
	return v;
}

// The pointer read through `addr`, when the scanner does not know the location
static asSAbstractValue PointeeOf(const asSAbstractValue &addr)
{
	asSAbstractValue v = Value(addr.loads, asMA_PROGRAM, asRH_NONE);
	v.originType = addr.loadsType;
	return v;
}

// The least origin covering both. A type-home origin sits above None and below
// Program, and is unordered against every other origin: two different classes,
// or a class and a scope above None, join to the first scope covering both.
static void JoinOrigin(asBYTE a, asCObjectType *aType, asBYTE b, asCObjectType *bType, asBYTE &origin, asCObjectType *&type)
{
	if( a == b && aType == bType )
	{
		origin = a;
		type = aType;
		return;
	}
	if( a == asMA_NONE )
	{
		origin = b;
		type = bType;
		return;
	}
	if( b == asMA_NONE )
	{
		origin = a;
		type = aType;
		return;
	}
	asBYTE ca = a == asORIGIN_TYPE_HOME ? asBYTE(asMA_PROGRAM) : a;
	asBYTE cb = b == asORIGIN_TYPE_HOME ? asBYTE(asMA_PROGRAM) : b;
	origin = ca > cb ? ca : cb;
	type = 0;
}

static asSAbstractValue JoinValue(const asSAbstractValue &a, const asSAbstractValue &b)
{
	asSAbstractValue v;
	JoinOrigin(a.origin, a.originType, b.origin, b.originType, v.origin, v.originType);
	JoinOrigin(a.loads, a.loadsType, b.loads, b.loadsType, v.loads, v.loadsType);
	v.hold   = a.hold > b.hold ? a.hold : b.hold;
	// Paths that disagree on which frame slot a value addresses may still address one
	v.slot   = a.slot == b.slot ? a.slot : asANY_SLOT;
	v.varRef = a.varRef == b.varRef ? a.varRef : asNO_SLOT;
	return v;
}

static bool SameValue(const asSAbstractValue &a, const asSAbstractValue &b)
{
	return a.origin == b.origin && a.loads == b.loads && a.hold == b.hold && a.slot == b.slot && a.varRef == b.varRef &&
	       a.originType == b.originType && a.loadsType == b.loadsType;
}

// Not monotone in s on its own: WorldStable maps to itself, while This on an
// object of lower origin maps below it. Reads take the floor in
// asMemoryAccessReadContribution, writes are never WorldStable, and the origin
// of a returned reference maps WorldStable to None (ReturnedReferenceOrigin),
// so no use ever takes that step.
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
// object's origin from hiding that, and it also makes reads monotone in s,
// which the fixed point needs to end.
asEMemoryAccess asMemoryAccessReadContribution(asEMemoryAccess s, asBYTE objectOrigin)
{
	return Join(asMemoryAccessContribution(s, objectOrigin), s < asMA_WORLD_STABLE ? s : asMA_WORLD_STABLE);
}

// The part of a returned reference's origin that comes from what the callee
// read. A reference into world-stable state counts as None, like This on a
// local object: that state never changes while the VM runs, and a reference
// into it is const by contract. This keeps the origin monotone in s; the call
// itself still records the WorldStable read.
static asBYTE ReturnedReferenceOrigin(asEMemoryAccess s, asBYTE objectOrigin)
{
	if( s <= asMA_WORLD_STABLE )
	{
		return asMA_NONE;
	}
	return asMemoryAccessContribution(s, objectOrigin);
}

asEMemoryAccess asMemoryAccessOfDestruction(asEMemoryAccess s)
{
	return s >= asMA_MODULE ? s : asMA_NONE;
}

asCMemoryAccessScanner::asCMemoryAccessScanner(asCScriptEngine *in_engine, asCModule *in_module, const asSMemoryAccessTable *in_table)
	: engine(in_engine), module(in_module), table(in_table), calleeLog(0), func(0), bc(0), bcLen(0), slotBase(0),
	  read(asMA_NONE), write(asMA_NONE), drops(false)
{
}

void asCMemoryAccessScanner::SetCalleeLog(asCArray<asUINT> *log)
{
	calleeLog = log;
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
	// Only this pass tracks the flag; any other callee that writes may drop a stored handle
	d = w > asMA_WORLD_STABLE;
}

void asCMemoryAccessScanner::DestructionOf(asCTypeInfo *type, asEMemoryAccess &r, asEMemoryAccess &w, bool &d)
{
	r = w = asMA_NONE;
	d = false;
	asCArray<asCTypeInfo*> visited;
	DestructionWalk(type, r, w, d, visited);
}

// Joins what running `f` on a dying object adds: only effects outside it count
static void JoinDestruction(asCMemoryAccessScanner *scanner, asCScriptFunction *f, asEMemoryAccess &r, asEMemoryAccess &w, bool &d)
{
	asEMemoryAccess fr, fw;
	bool fd;
	scanner->AccessOf(f, fr, fw, fd);
	d = d || fd;
	r = Join(r, asMemoryAccessOfDestruction(fr));
	w = Join(w, asMemoryAccessOfDestruction(fw));
}

void asCMemoryAccessScanner::DestructionWalk(asCTypeInfo *type, asEMemoryAccess &r, asEMemoryAccess &w, bool &d, asCArray<asCTypeInfo*> &visited)
{
	if( type == 0 || type == &engine->functionBehaviours || CastToFuncdefType(type) || (type->flags & asOBJ_TEMPLATE_SUBTYPE) )
	{
		// Unknown, or a delegate that may hold the last reference to anything
		r = w = asMA_PROGRAM;
		d = true;
		return;
	}
	if( visited.IndexOf(type) >= 0 )
	{
		return;
	}
	visited.PushLast(type);

	asCObjectType *ot = CastToObjectType(type);
	if( ot == 0 )
	{
		// Enums and primitives: nothing to destroy
		return;
	}

	if( ot->flags & asOBJ_LIST_PATTERN )
	{
		// The buffer holds copies of the elements its list pattern names
		asCObjectType *target = ot->templateSubTypes.GetLength() ? CastToObjectType(ot->templateSubTypes[0].GetTypeInfo()) : 0;
		asCScriptFunction *lf = target && target->beh.listFactory ? FunctionById(target->beh.listFactory) : 0;
		if( lf == 0 )
		{
			r = w = asMA_PROGRAM;
			d = true;
			return;
		}
		for( asSListPatternNode *node = lf->listPattern; node; node = node->next )
		{
			if( node->type != asLPT_TYPE )
			{
				continue;
			}
			const asCDataType &dt = static_cast<asSListPatternDataTypeNode*>(node)->dataType;
			if( dt.GetTokenType() == ttQuestion )
			{
				r = w = asMA_PROGRAM;
				d = true;
				return;
			}
			if( dt.IsObject() || dt.IsObjectHandle() )
			{
				DestructionWalk(dt.GetTypeInfo(), r, w, d, visited);
			}
		}
		return;
	}

	// A handle of static type T may hold any class derived from T, or
	// implementing T. The set is complete only for a type of this module that
	// is not shared: a shared type may gain subclasses in a module built later.
	if( ot->flags & asOBJ_SCRIPT_OBJECT )
	{
		if( ot->IsShared() || module == 0 || ot->module != module )
		{
			r = w = asMA_PROGRAM;
			d = true;
			return;
		}
		const asCArray<asCObjectType*> &classes = module->GetClassTypes();
		// An interface is a script object too, with no body or members of its own
		if( ot->IsInterface() )
		{
			for( asUINT c = 0; c < classes.GetLength(); c++ )
			{
				if( classes[c] && classes[c] != ot && classes[c]->Implements(ot) )
				{
					DestructionWalk(classes[c], r, w, d, visited);
				}
			}
			return;
		}
		if( ot->beh.destruct )
		{
			JoinDestruction(this, FunctionById(ot->beh.destruct), r, w, d);
		}
		if( ot->derivedFrom )
		{
			DestructionWalk(ot->derivedFrom, r, w, d, visited);
		}
		for( asUINT n = 0; n < ot->properties.GetLength(); n++ )
		{
			const asCDataType &dt = ot->properties[n]->type;
			if( dt.IsObject() || dt.IsObjectHandle() )
			{
				DestructionWalk(dt.GetTypeInfo(), r, w, d, visited);
			}
		}
		for( asUINT c = 0; c < classes.GetLength(); c++ )
		{
			if( classes[c] && classes[c] != ot && classes[c]->DerivesFrom(ot) )
			{
				DestructionWalk(classes[c], r, w, d, visited);
			}
		}
		return;
	}

	// A registered type: the host's Release (reference types) or destructor
	// (value types), whose declared scope is the worst case, when it destroys
	int id = (ot->flags & asOBJ_REF) ? ot->beh.release : ot->beh.destruct;
	if( id )
	{
		JoinDestruction(this, FunctionById(id), r, w, d);
	}
	// Every instance of a template shares that declaration, so it cannot cover
	// what destroying the objects one instance holds runs: array<D@> runs ~D
	for( asUINT n = 0; n < ot->templateSubTypes.GetLength(); n++ )
	{
		const asCDataType &dt = ot->templateSubTypes[n];
		if( dt.IsObject() || dt.IsObjectHandle() )
		{
			DestructionWalk(dt.GetTypeInfo(), r, w, d, visited);
		}
	}
}

void asCMemoryAccessScanner::DestroyUnbalanced(asCTypeInfo *type)
{
	asEMemoryAccess r, w;
	bool d;
	DestructionOf(type, r, w, d);
	RecordRead(r);
	RecordWrite(w);
	// A destruction that clears a stored handle can make an earlier balanced
	// release the last one
	if( d )
	{
		drops = true;
	}
}

void asCMemoryAccessScanner::ReleaseValue(const asSAbstractValue &v, asCTypeInfo *type)
{
	if( v.hold == asRH_NONE )
	{
		return;
	}
	// The VM never releases an object of a type without a reference count
	if( type && (type->flags & asOBJ_NOCOUNT) )
	{
		return;
	}
	if( v.hold == asRH_COPIED )
	{
		// Counted only if this function also drops a stored handle, which is
		// known at the end of Scan
		if( copiedReleases.IndexOf(type) < 0 )
		{
			copiedReleases.PushLast(type);
		}
		return;
	}
	DestroyUnbalanced(type);
}

bool asCMemoryAccessScanner::StoreHandle(State &s, const asSAbstractValue &dest, const asSAbstractValue &value, asCTypeInfo *type)
{
	RecordReadOf(dest);
	RecordWriteOf(dest);
	// REFCPY skips the AddRef and Release of these types (asCContext, asBC_REFCPY)
	bool counted = type == 0 || !(type->flags & (asOBJ_NOCOUNT | asOBJ_VALUE));
	if( dest.slot == asANY_SLOT )
	{
		// Some frame slot, but which one is unknown: its old value is unknown too
		DestroyUnbalanced(type);
		ForgetSlot(s, dest, AS_PTR_SIZE);
		return true;
	}
	if( dest.slot != asNO_SLOT )
	{
		// A REFCPY into offset 0 of an inline value-type local (FieldOf) lands on
		// that local's base cell and overwrites it. That is sound only because
		// destruction is counted per type, at the position after the constructor;
		// tracking destruction per instance must revisit this.
		asSAbstractValue *slot = Var(s, dest.slot);
		if( slot == 0 )
		{
			return false;
		}
		if( counted )
		{
			ReleaseValue(*slot, type);
		}
		// A copy that is not counted holds whatever the variable is later freed for
		asSAbstractValue stored = value;
		stored.hold   = counted ? asRH_COPIED : asRH_OWNED;
		stored.slot   = asNO_SLOT;
		stored.varRef = asNO_SLOT;
		return SetVarCells(s, dest.slot, stored, AS_PTR_SIZE);
	}
	// Overwriting a handle stored outside the frame drops whatever it held
	if( counted )
	{
		drops = true;
		DestroyUnbalanced(type);
	}
	return true;
}

bool asCMemoryAccessScanner::CleanNativeArgs(State &s, asCScriptFunction *callee, asUINT k)
{
	// asSSystemFunctionInterface::cleanArgs releases auto handles and destroys
	// objects passed by value, and the native itself releases any other handle
	// it is given, so every handle and object argument is released here
	if( callee->funcType != asFUNC_SYSTEM )
	{
		return true;
	}
	for( asUINT n = 0; n < callee->parameterTypes.GetLength(); n++ )
	{
		const asCDataType &dt = callee->parameterTypes[n];
		if( !dt.IsReference() && (dt.IsObject() || dt.IsObjectHandle()) )
		{
			asSAbstractValue arg;
			if( !PtrAt(s, k, arg) )
			{
				return false;
			}
			ReleaseValue(arg, dt.GetTypeInfo());
		}
		k += dt.GetSizeOnStackDWords();
	}
	return true;
}

// A type-home origin reaching here unresolved would be no scope at all
void asCMemoryAccessScanner::RecordRead(asBYTE origin)
{
	asEMemoryAccess o = origin > asMA_UNSET ? asMA_PROGRAM : asEMemoryAccess(origin);
	read = Join(read, o);
}

void asCMemoryAccessScanner::RecordWrite(asBYTE origin)
{
	// Writing world-stable state contradicts its definition, so widen it
	asEMemoryAccess o = (origin == asMA_WORLD_STABLE || origin > asMA_UNSET) ? asMA_PROGRAM : asEMemoryAccess(origin);
	write = Join(write, o);
}

void asCMemoryAccessScanner::RecordReadOf(const asSAbstractValue &v)
{
	RecordRead(Resolve(v.origin, v.originType));
}

void asCMemoryAccessScanner::RecordWriteOf(const asSAbstractValue &v)
{
	RecordWrite(Resolve(v.origin, v.originType));
}

asCObjectType *asCMemoryAccessScanner::ScriptClass(asCTypeInfo *type) const
{
	asCObjectType *ot = CastToObjectType(type);
	// The engine's own script-object behaviours are declared on a type no script names
	if( ot == 0 || !(ot->flags & asOBJ_SCRIPT_OBJECT) || ot == &engine->scriptTypeBehaviours )
	{
		return 0;
	}
	return ot;
}

asSAbstractValue asCMemoryAccessScanner::TypedPointer(asCTypeInfo *type, asBYTE hold)
{
	asCObjectType *ot = ScriptClass(type);
	if( ot == 0 )
	{
		return Value(asMA_PROGRAM, asMA_PROGRAM, hold);
	}
	asSAbstractValue v = Value(asORIGIN_TYPE_HOME, asMA_PROGRAM, hold);
	v.originType = ot;
	return v;
}

void asCMemoryAccessScanner::SetTypedLoads(asSAbstractValue &v, asCTypeInfo *type)
{
	v.loadsType = ScriptClass(type);
	v.loads = v.loadsType ? asORIGIN_TYPE_HOME : asBYTE(asMA_PROGRAM);
}

// Code can touch a member of a script object only by naming it, and only code
// that sees the declaring class can name it. Another module reaches an object
// of this module's non-shared class only through a shared type, an interface,
// `?` or the host, each already Engine or wider there, and Engine contains
// every module's Module.
asBYTE asCMemoryAccessScanner::HomeOf(asCObjectType *cls) const
{
	if( cls == 0 || ScriptClass(cls) == 0 )
	{
		return asMA_PROGRAM;
	}
	if( !cls->IsShared() && module != 0 && cls->module == module )
	{
		return asMA_MODULE;
	}
	return asMA_ENGINE;
}

asBYTE asCMemoryAccessScanner::ChainHomeOf(asCObjectType *cls) const
{
	asBYTE h = asMA_NONE;
	for( asCObjectType *c = cls; c; c = c->derivedFrom )
	{
		asBYTE ch = HomeOf(c);
		h = ch > h ? ch : h;
		for( asUINT n = 0; n < c->interfaces.GetLength(); n++ )
		{
			ch = HomeOf(c->interfaces[n]);
			h = ch > h ? ch : h;
		}
	}
	return h;
}

asBYTE asCMemoryAccessScanner::WholeHomeOf(asCObjectType *type) const
{
	asCObjectType *ot = ScriptClass(type);
	if( ot == 0 )
	{
		return asMA_PROGRAM;
	}
	asBYTE h = ChainHomeOf(ot);
	if( h != asMA_MODULE )
	{
		return h;
	}
	// The object may be of any class derived from the static type, or
	// implementing it, and such a class may have a shared base or interface.
	// A non-shared type of this module is derived from only in this module.
	const asCArray<asCObjectType*> &classes = module->GetClassTypes();
	for( asUINT c = 0; c < classes.GetLength(); c++ )
	{
		if( classes[c] && classes[c] != ot && (classes[c]->DerivesFrom(ot) || classes[c]->Implements(ot)) )
		{
			asBYTE ch = ChainHomeOf(classes[c]);
			h = ch > h ? ch : h;
		}
	}
	return h;
}

static asCObjectProperty *PropertyAt(asCObjectType *ot, int offset)
{
	for( asUINT n = 0; n < ot->properties.GetLength(); n++ )
	{
		if( ot->properties[n]->byteOffset == offset )
		{
			return ot->properties[n];
		}
	}
	return 0;
}

asBYTE asCMemoryAccessScanner::FieldHome(int typeId, int offset) const
{
	asCObjectType *ot = ScriptClass(engine->GetObjectTypeFromTypeId(typeId));
	asCObjectProperty *prop = ot ? PropertyAt(ot, offset) : 0;
	if( prop == 0 )
	{
		return asMA_PROGRAM;
	}
	// A derived class keeps an inherited property at its base's offset
	asCObjectType *decl = ot;
	while( decl->derivedFrom )
	{
		asCObjectProperty *base = PropertyAt(decl->derivedFrom, offset);
		if( base == 0 || base->name != prop->name )
		{
			break;
		}
		decl = decl->derivedFrom;
	}
	return HomeOf(decl);
}

asBYTE asCMemoryAccessScanner::MethodHome(asCScriptFunction *method, asCObjectType *type) const
{
	asCObjectType *decl = method ? ScriptClass(method->objectType) : 0;
	if( decl )
	{
		return HomeOf(decl);
	}
	// A behaviour the engine implements for every script object, such as the
	// default opAssign, may touch any member of the object
	return WholeHomeOf(type);
}

asBYTE asCMemoryAccessScanner::Resolve(asBYTE origin, asCObjectType *type) const
{
	return origin == asORIGIN_TYPE_HOME ? WholeHomeOf(type) : origin;
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

bool asCMemoryAccessScanner::SetVarCells(State &s, int offset, const asSAbstractValue &v, asUINT dwords)
{
	for( asUINT n = 0; n < dwords; n++ )
	{
		asSAbstractValue *c = Var(s, offset - int(n));
		if( c == 0 )
		{
			return false;
		}
		*c = v;
	}
	return true;
}

void asCMemoryAccessScanner::ForgetSlot(State &s, const asSAbstractValue &addr, asUINT dwords)
{
	if( addr.slot == asNO_SLOT )
	{
		return;
	}
	if( addr.slot == asANY_SLOT )
	{
		// Which slot is unknown, so any of them may have changed, except `this`:
		// the compiler never assigns it, which CatchState relies on as well
		asSAbstractValue self;
		asSAbstractValue *selfCell = func->objectType ? Var(s, 0) : 0;
		if( selfCell )
		{
			self = *selfCell;
		}
		for( asUINT n = 0; n < s.vars.GetLength(); n++ )
		{
			s.vars[n] = Unknown();
		}
		if( selfCell )
		{
			SetVarCells(s, 0, self, AS_PTR_SIZE);
		}
		return;
	}
	// A slot the write covers only partly is out of the frame's range, which
	// the bytecode never produces; the cells that are in range still change.
	// The cell above is cleared too: a write into the high cell of a pointer
	// must not leave the pointer's base cell holding its old value.
	for( int n = -1; n < int(dwords); n++ )
	{
		asSAbstractValue *c = Var(s, addr.slot - n);
		if( c )
		{
			*c = Unknown();
		}
	}
}

asCScriptFunction *asCMemoryAccessScanner::FunctionById(int id) const
{
	if( id < 0 || asUINT(id) >= engine->scriptFunctions.GetLength() )
	{
		return 0;
	}
	return engine->scriptFunctions[asUINT(id)];
}

// Same sizes as asCCompiler::GetVariableOffset. The compiler reuses a slot
// only for an identical type, so every variable at an offset has one size.
asUINT asCMemoryAccessScanner::VarDwords(int offset) const
{
	for( asUINT n = 0; n < func->scriptData->variables.GetLength(); n++ )
	{
		asSScriptVariable *v = func->scriptData->variables[n];
		if( v->stackOffset != offset )
		{
			continue;
		}
		int size;
		if( !v->type.IsReference() && !v->onHeap && v->type.IsObject() )
		{
			size = v->type.GetSizeInMemoryDWords();
		}
		else
		{
			size = v->type.GetSizeOnStackDWords();
		}
		return asUINT(size > AS_PTR_SIZE ? size : AS_PTR_SIZE);
	}
	return 0;
}

void asCMemoryAccessScanner::ForgetArgument(State &s, const asSAbstractValue &a)
{
	if( a.slot == asNO_SLOT )
	{
		return;
	}
	asUINT dwords = a.slot == asANY_SLOT ? 0 : VarDwords(a.slot);
	if( dwords == 0 )
	{
		// An address inside a variable, or of no known variable: the callee
		// may write anywhere in it, and its extent is unknown
		asSAbstractValue any = a;
		any.slot = asANY_SLOT;
		ForgetSlot(s, any, 0);
		return;
	}
	ForgetSlot(s, a, dwords);
}

enum
{
	asCALLKIND_DIRECT,   // CALL, CALLSYS, Thiscall1, ALLOC: the callee is known
	asCALLKIND_DISPATCH, // CALLINTF: the callee is a virtual or interface method
	asCALLKIND_UNKNOWN   // CallPtr, CALLBND: only the signature is known
};

bool asCMemoryAccessScanner::IsFactory(asCScriptFunction *callee) const
{
	asCObjectType *ot = CastToObjectType(callee->returnType.GetTypeInfo());
	if( ot == 0 )
	{
		return false;
	}
	if( ot->beh.listFactory == callee->id || ot->beh.copyfactory == callee->id )
	{
		return true;
	}
	for( asUINT n = 0; n < ot->beh.factories.GetLength(); n++ )
	{
		if( ot->beh.factories[n] == callee->id )
		{
			return true;
		}
	}
	return false;
}

bool asCMemoryAccessScanner::CallTargets(asCScriptFunction *callee, asUINT kind, asCArray<asCScriptFunction*> &targets)
{
	// A virtual entry has no body, and its stored byte is stale inside the
	// fixed point (Unset on a first build), so it is reached through its targets
	if( callee->funcType == asFUNC_VIRTUAL || callee->funcType == asFUNC_INTERFACE )
	{
		kind = asCALLKIND_DISPATCH;
	}
	else if( callee->funcType == asFUNC_FUNCDEF || callee->funcType == asFUNC_IMPORTED || callee->funcType == asFUNC_DELEGATE )
	{
		kind = asCALLKIND_UNKNOWN;
	}
	if( kind == asCALLKIND_UNKNOWN )
	{
		return false;
	}
	if( kind == asCALLKIND_DISPATCH )
	{
		// The targets are found among this module's classes, so a type owned by
		// another module would yield an empty, falsely narrow set
		return module != 0 && callee->objectType != 0 && callee->objectType->module == module &&
		       module->GetDispatchTargets(callee, targets);
	}
	targets.PushLast(callee);
	return true;
}

void asCMemoryAccessScanner::CalleeAccess(asCScriptFunction *callee, asUINT kind, asEMemoryAccess &r, asEMemoryAccess &w, bool &d)
{
	asCArray<asCScriptFunction*> targets;
	if( !CallTargets(callee, kind, targets) )
	{
		r = w = asMA_PROGRAM;
		d = true;
		return;
	}
	r = w = asMA_NONE;
	d = false;
	for( asUINT n = 0; n < targets.GetLength(); n++ )
	{
		asEMemoryAccess tr, tw;
		bool td;
		AccessOf(targets[n], tr, tw, td);
		r = Join(r, tr);
		w = Join(w, tw);
		d = d || td;
	}
}

bool asCMemoryAccessScanner::DoCall(State &s, asCScriptFunction *callee, asUINT kind)
{
	if( callee == 0 || callee->IsVariadic() )
	{
		return false;
	}
	if( calleeLog && table && kind == asCALLKIND_DIRECT )
	{
		asSMapNode<int, asUINT> *cursor = 0;
		if( table->funcIdToIndex->MoveTo(&cursor, callee->id) )
		{
			asUINT i = table->funcIdToIndex->GetValue(cursor);
			if( calleeLog->IndexOf(i) < 0 )
			{
				calleeLog->PushLast(i);
			}
		}
	}
	asUINT thisSize = callee->objectType ? AS_PTR_SIZE : 0;
	asUINT retSize  = callee->DoesReturnOnStack() ? AS_PTR_SIZE : 0;
	asUINT total    = thisSize + retSize + asUINT(callee->GetSpaceNeededForArguments());
	if( s.stack.GetLength() < total )
	{
		return false;
	}

	asBYTE objectOrigin = asMA_THIS; // identity for calls without an object
	asSAbstractValue obj;
	if( thisSize && !PtrAt(s, 0, obj) )
	{
		return false;
	}
	if( thisSize )
	{
		objectOrigin = obj.origin;
	}

	// A returned reference points into memory the callee read, or into the
	// object or an argument it was given. A script callee counts the reference
	// it returns as read (asBC_RET), because taking an address reads nothing.
	asEMemoryAccess cr, cw;
	asBYTE refOrigin;
	bool d;
	if( objectOrigin == asORIGIN_TYPE_HOME )
	{
		// Each body the call may run touches the object at the home of the
		// class that declares it
		asCArray<asCScriptFunction*> targets;
		if( !CallTargets(callee, kind, targets) )
		{
			targets.SetLength(0);
			targets.PushLast(0);
		}
		cr = cw = asMA_NONE;
		refOrigin = asMA_NONE;
		d = false;
		for( asUINT n = 0; n < targets.GetLength(); n++ )
		{
			asEMemoryAccess tr, tw;
			bool td;
			AccessOf(targets[n], tr, tw, td);
			asBYTE home = targets[n] ? MethodHome(targets[n], obj.originType) : asBYTE(asMA_PROGRAM);
			cr = Join(cr, asMemoryAccessReadContribution(tr, home));
			cw = Join(cw, asMemoryAccessContribution(tw, home));
			asBYTE tref = ReturnedReferenceOrigin(tr, home);
			refOrigin = tref > refOrigin ? tref : refOrigin;
			d = d || td;
		}
	}
	else
	{
		asEMemoryAccess r, w;
		CalleeAccess(callee, kind, r, w, d);
		cr = asMemoryAccessReadContribution(r, objectOrigin);
		cw = asMemoryAccessContribution(w, objectOrigin);
		refOrigin = ReturnedReferenceOrigin(r, objectOrigin);
	}
	RecordRead(cr);
	RecordWrite(cw);
	if( d )
	{
		drops = true;
	}
	if( !ChargeArguments(s, callee, thisSize + retSize) )
	{
		return false;
	}

	// The reference may point into a frame variable the caller passed, at an
	// offset the scanner cannot know
	bool refIntoFrame = false;
	if( thisSize )
	{
		asBYTE o = Resolve(obj.origin, obj.originType);
		if( o > refOrigin )
		{
			refOrigin = o;
		}
		refIntoFrame = obj.slot != asNO_SLOT;
	}
	asUINT k = thisSize + retSize;
	for( asUINT n = 0; n < callee->parameterTypes.GetLength(); n++ )
	{
		const asCDataType &dt = callee->parameterTypes[n];
		if( dt.IsReference() || dt.IsObjectHandle() || dt.IsObject() || dt.GetTokenType() == ttQuestion )
		{
			asSAbstractValue arg;
			if( !PtrAt(s, k, arg) )
			{
				return false;
			}
			asBYTE o = Resolve(arg.origin, arg.originType);
			if( o > refOrigin )
			{
				refOrigin = o;
			}
			if( arg.slot != asNO_SLOT )
			{
				refIntoFrame = true;
			}
		}
		k += dt.GetSizeOnStackDWords();
	}
	if( !CleanNativeArgs(s, callee, thisSize + retSize) )
	{
		return false;
	}

	// The callee may write any frame variable whose address it was given:
	// `this` of an inline value, the return location, or an argument
	for( asUINT n = 0; n < total; n++ )
	{
		asSAbstractValue cell = *Cell(s, n);
		ForgetArgument(s, cell);
	}

	if( !Pop(s, total) )
	{
		return false;
	}

	// A returned reference lands in the value register, and a returned handle
	// or object in the object register. Any call may leave the value register
	// changed.
	s.valueReg = Unknown();
	const asCDataType &rt = callee->returnType;
	if( rt.IsReference() )
	{
		s.valueReg = Value(refOrigin, asMA_PROGRAM, asRH_NONE);
		// A handle read through a returned reference to one may be held anywhere
		if( rt.IsObjectHandle() )
		{
			SetTypedLoads(s.valueReg, rt.GetTypeInfo());
		}
		if( refIntoFrame )
		{
			s.valueReg.slot = asANY_SLOT;
		}
	}
	else if( rt.IsObjectHandle() || rt.IsFuncdef() || (rt.IsObject() && !callee->DoesReturnOnStack()) )
	{
		if( kind == asCALLKIND_DIRECT && IsFactory(callee) )
		{
			s.objectReg = Value(asMA_NONE, asMA_PROGRAM, asRH_OWNED);
		}
		else
		{
			// A returned handle may be held anywhere
			s.objectReg = TypedPointer(rt.GetTypeInfo(), asRH_OWNED);
		}
	}
	return true;
}

// Only script classes own the objects in their non-handle reference members.
// A handle member may refer to an object held anywhere, and every other
// pointer field is treated as reaching anything.
void asCMemoryAccessScanner::FieldLoads(const asSAbstractValue &base, int typeId, int offset, asBYTE &loads, asCObjectType *&loadsType)
{
	loads = asMA_PROGRAM;
	loadsType = 0;
	asCObjectType *ot = engine->GetObjectTypeFromTypeId(typeId);
	asCObjectProperty *prop = (ot && (ot->flags & asOBJ_SCRIPT_OBJECT)) ? PropertyAt(ot, offset) : 0;
	if( prop == 0 || !(prop->type.IsObjectHandle() || prop->type.IsReference()) )
	{
		return;
	}
	if( prop->type.IsObjectHandle() || base.origin == asORIGIN_TYPE_HOME )
	{
		// An object reached through a handle, or owned by one that was
		loadsType = ScriptClass(prop->type.GetTypeInfo());
		loads = loadsType ? asORIGIN_TYPE_HOME : asBYTE(asMA_PROGRAM);
		return;
	}
	// A non-handle object member is owned by the object holding it
	if( base.origin == asMA_THIS || base.origin == asMA_OWNED )
	{
		loads = asMA_OWNED;
		return;
	}
	loads = base.origin;
}

asSAbstractValue asCMemoryAccessScanner::FieldAt(const asSAbstractValue &base, int typeId, int offset)
{
	asBYTE loads;
	asCObjectType *loadsType;
	FieldLoads(base, typeId, offset, loads, loadsType);
	asSAbstractValue v = FieldOf(base, loads, offset);
	v.loadsType = loadsType;
	// Code that names the field is charged at the home of its declaring class
	if( base.origin == asORIGIN_TYPE_HOME )
	{
		v.origin = FieldHome(typeId, offset);
		v.originType = 0;
	}
	return v;
}

// Every address operand of a global opcode is a global property's storage, which
// the engine maps back to the property, except the string constants PGA also
// takes (asCCompiler::CompileExpressionValue): those the string factory owns
// and the script can only read, so they reach nothing mutable.
asBYTE asCMemoryAccessScanner::GlobalOrigin(void *address)
{
	asSMapNode<void*, asCGlobalProperty*> *cursor = 0;
	if( !engine->varAddressMap.MoveTo(&cursor, address) )
	{
		return asMA_NONE;
	}
	asCGlobalProperty *prop = engine->varAddressMap.GetValue(cursor);
	// Only a registered property has an application address
	return prop->realAddress ? asMA_ENGINE : asMA_MODULE;
}

asSAbstractValue asCMemoryAccessScanner::GlobalAddress(void *address)
{
	asSAbstractValue v = Value(GlobalOrigin(address), asMA_PROGRAM, asRH_NONE);
	asSMapNode<void*, asCGlobalProperty*> *cursor = 0;
	if( !engine->varAddressMap.MoveTo(&cursor, address) )
	{
		return v;
	}
	asCGlobalProperty *prop = engine->varAddressMap.GetValue(cursor);
	if( prop->type.IsObjectHandle() )
	{
		// The object a handle refers to may be held anywhere
		SetTypedLoads(v, prop->type.GetTypeInfo());
	}
	else if( prop->type.IsObject() )
	{
		// The object a non-handle object global holds belongs to that global
		v.loads = v.origin;
	}
	return v;
}

bool asCMemoryAccessScanner::PtrAt(State &s, asUINT k, asSAbstractValue &v)
{
	asSAbstractValue *c = Cell(s, k);
	if( c == 0 )
	{
		return false;
	}
	v = *c;
	for( asUINT n = 1; n < AS_PTR_SIZE; n++ )
	{
		c = Cell(s, k + n);
		if( c == 0 )
		{
			return false;
		}
		v = JoinValue(v, *c);
	}
	return true;
}

asSAbstractValue asCMemoryAccessScanner::FieldOf(const asSAbstractValue &base, asBYTE loads, int offset)
{
	asSAbstractValue v = Value(base.origin, loads, asRH_NONE);
	// An access through it without the field's class is to the whole object
	v.originType = base.originType;
	// A field of an object stored inline in the frame is itself a frame address,
	// and a write through it must reach the cells it covers
	if( base.slot == asANY_SLOT || (base.slot != asNO_SLOT && (offset & 3)) )
	{
		v.slot = asANY_SLOT;
	}
	else if( base.slot != asNO_SLOT )
	{
		v.slot = short(base.slot - offset / 4);
	}
	return v;
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

// A value type no handle can refer to, so a copy of it is private
static bool IsPlainValueType(const asCDataType &dt)
{
	asCTypeInfo *ti = dt.GetTypeInfo();
	return ti && (ti->flags & asOBJ_VALUE) && !(ti->flags & asOBJ_ASHANDLE);
}

// An application function is opaque, so the caller charges what it may touch
// through its arguments: the memory a reference points into, and the object a
// handle or a reference-type value refers to. Its declared scopes cover only
// what it reaches beyond those. A script callee's parameters are analysed
// inside the callee instead. An &out argument is a caller temporary, and a
// value type passed by value is a private copy.
bool asCMemoryAccessScanner::ChargeArguments(State &s, asCScriptFunction *callee, asUINT k)
{
	// A template instance's factory stub has no module, and carries the
	// declared scopes of the application factory it forwards to
	if( callee->funcType != asFUNC_SYSTEM && !(callee->funcType == asFUNC_SCRIPT && callee->module == 0) )
	{
		return true;
	}
	for( asUINT n = 0; n < callee->parameterTypes.GetLength(); n++ )
	{
		const asCDataType &dt = callee->parameterTypes[n];
		asETypeModifiers inOut = n < callee->inOutFlags.GetLength() ? callee->inOutFlags[n] : asTM_NONE;
		bool charge;
		if( dt.IsReference() )
		{
			charge = inOut != asTM_OUTREF;
		}
		else
		{
			charge = dt.IsObjectHandle() || (dt.IsObject() && !IsPlainValueType(dt));
		}
		if( charge )
		{
			asSAbstractValue arg;
			if( !PtrAt(s, k, arg) )
			{
				return false;
			}
			RecordReadOf(arg);
			// A reference to a handle points at the handle variable, which only a
			// const handle protects; a handle to a const object may still be
			// reseated. Anything else is protected by a const object.
			bool argConst = dt.IsReference() && dt.IsObjectHandle() ? dt.IsReadOnly() : dt.IsObjectConst();
			if( !argConst )
			{
				RecordWriteOf(arg);
			}
			// Through a reference to a handle, or to a value of unknown type, the
			// function also reaches the object the handle refers to
			if( dt.IsReference() && (dt.IsObjectHandle() || dt.GetTokenType() == ttQuestion) )
			{
				asBYTE pointee = Resolve(arg.loads, arg.loadsType);
				// A `?` reference may hold a handle of any type, whatever its static type here
				if( dt.GetTokenType() == ttQuestion )
				{
					pointee = arg.loads == asORIGIN_TYPE_HOME ? asBYTE(asMA_PROGRAM) : arg.loads;
				}
				RecordRead(pointee);
				if( !dt.IsObjectConst() )
				{
					RecordWrite(pointee);
				}
			}
		}
		k += dt.GetSizeOnStackDWords();
	}
	return true;
}

asSAbstractValue asCMemoryAccessScanner::ParamValue(const asCDataType &dt, asETypeModifiers inOut)
{
	if( dt.GetTokenType() == ttQuestion )
	{
		return Value(asMA_PROGRAM, asMA_PROGRAM, asRH_NONE);
	}
	if( dt.IsReference() )
	{
		asSAbstractValue v;
		if( dt.IsObjectHandle() )
		{
			// A reference to a handle variable: the variable is the caller's
			// temporary for &out and may be anywhere otherwise, while the object
			// the handle refers to may be held anywhere
			v = Value(inOut == asTM_OUTREF ? asMA_NONE : asMA_PROGRAM, asMA_PROGRAM, asRH_NONE);
			SetTypedLoads(v, dt.GetTypeInfo());
			return v;
		}
		if( inOut == asTM_OUTREF )
		{
			return Value(asMA_NONE, asMA_PROGRAM, asRH_NONE);
		}
		// &in: a primitive is copied or is the caller's own frame variable, and a
		// non-const value type is copied or is the caller's own temporary
		if( inOut == asTM_INREF && (dt.IsPrimitive() || (!dt.IsReadOnly() && IsPlainValueType(dt))) )
		{
			return Value(asMA_NONE, asMA_PROGRAM, asRH_NONE);
		}
		// &inout, a const &in object, and a reference type that is already a
		// temporary, which is passed without a copy (PrepareArgument) and may be
		// a handle's object: an object of the static type, held anywhere. A
		// primitive &inout may alias anything.
		if( dt.IsObject() )
		{
			return TypedPointer(dt.GetTypeInfo(), asRH_NONE);
		}
		return Value(asMA_PROGRAM, asMA_PROGRAM, asRH_NONE);
	}
	if( dt.IsObjectHandle() )
	{
		// The caller may have handed over the only reference
		return TypedPointer(dt.GetTypeInfo(), asRH_OWNED);
	}
	if( dt.IsObject() )
	{
		// By value the callee owns its argument, but a reference type that is
		// already a temporary is passed without a copy (PrepareTemporaryVariable),
		// so the object may be shared; only a value type is a private copy
		if( IsPlainValueType(dt) )
		{
			return Value(asMA_NONE, asMA_PROGRAM, asRH_OWNED);
		}
		return TypedPointer(dt.GetTypeInfo(), asRH_OWNED);
	}
	return Unknown();
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

	if( func->objectType )
	{
		SetVarCells(s, 0, Value(asMA_THIS, asMA_PROGRAM, asRH_NONE), AS_PTR_SIZE);
	}
	// The caller's temporary for a value type returned by value
	if( func->DoesReturnOnStack() )
	{
		SetVarCells(s, func->objectType ? -AS_PTR_SIZE : 0, Value(asMA_NONE, asMA_PROGRAM, asRH_NONE), AS_PTR_SIZE);
	}
	// Same offsets as asCCompiler::SetupParametersAndReturnVariable
	int offset = -((func->objectType ? AS_PTR_SIZE : 0) + (func->DoesReturnOnStack() ? AS_PTR_SIZE : 0));
	for( asUINT n = 0; n < func->parameterTypes.GetLength(); n++ )
	{
		const asCDataType &dt = func->parameterTypes[n];
		asETypeModifiers inOut = n < func->inOutFlags.GetLength() ? func->inOutFlags[n] : asTM_NONE;
		// A primitive by value is already Unknown, and its slot may be a single dword
		if( dt.IsReference() || dt.IsObject() || dt.GetTokenType() == ttQuestion )
		{
			SetVarCells(s, offset, ParamValue(dt, inOut), AS_PTR_SIZE);
		}
		offset -= dt.GetSizeOnStackDWords();
	}
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

	// An exception or an abort can unwind at any reached position, and
	// asCContext::CleanStackFrame then releases every object variable, the
	// parameters included, as it holds it there. `this` is not a variable. The
	// state after a call covers what the callee may have written before it
	// raised, and the arguments pending in a call were counted at GETOBJ. A
	// value still in the object register when a native aborts is counted
	// through the next STOREOBJ position, where the stored variable is OWNED.
	for( asUINT pos = 0; ok && pos < bcLen; pos++ )
	{
		if( !states[pos].reached )
		{
			continue;
		}
		for( asUINT n = 0; n < f->scriptData->variables.GetLength(); n++ )
		{
			asSScriptVariable *v = f->scriptData->variables[n];
			// The VM skips a variable of no known type: it holds null or a borrowed reference
			if( v->type.IsReference() || v->type.GetTypeInfo() == 0 || !(v->type.IsObject() || v->type.IsObjectHandle()) )
			{
				continue;
			}
			asSAbstractValue *slot = Var(states[pos], v->stackOffset);
			if( slot )
			{
				ReleaseValue(*slot, v->type.GetTypeInfo());
			}
		}
	}
	// A copied release is balanced unless this function drops stored handles itself.
	// It runs last, because releases above may both add copies and set the flag.
	if( ok && drops )
	{
		for( asUINT n = 0; n < copiedReleases.GetLength(); n++ )
		{
			DestroyUnbalanced(copiedReleases[n]);
		}
	}

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
		// Returning a reference hands the caller what it points to. Taking the
		// address of a member or a global reads nothing (LoadThisR, LDG), so
		// without this a caller writing through the reference would see None.
		if( func->returnType.IsReference() )
		{
			RecordReadOf(s.valueReg);
		}
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

	// Addresses of frame slots, and pointers held in them
	case asBC_PSF:
	{
		short at = asBC_SWORDARG0(instr);
		asSAbstractValue *slot = Var(s, at);
		if( slot == 0 )
		{
			return false;
		}
		asSAbstractValue v = AddressOf(*slot);
		v.slot = at;
		Push(s, v, AS_PTR_SIZE);
		break;
	}
	case asBC_PshVPtr:
	{
		asSAbstractValue *slot = Var(s, asBC_SWORDARG0(instr));
		if( slot == 0 )
		{
			return false;
		}
		// A copy borrows the reference the slot keeps
		asSAbstractValue v = *slot;
		v.hold   = asRH_NONE;
		v.varRef = asNO_SLOT;
		Push(s, v, AS_PTR_SIZE);
		break;
	}
	case asBC_RDSPtr:
	{
		asSAbstractValue addr;
		if( !PtrAt(s, 0, addr) )
		{
			return false;
		}
		RecordReadOf(addr);
		asSAbstractValue loaded;
		if( addr.slot == asANY_SLOT )
		{
			loaded = Unknown();
		}
		else if( addr.slot != asNO_SLOT )
		{
			asSAbstractValue *slot = Var(s, addr.slot);
			if( slot == 0 )
			{
				return false;
			}
			loaded = *slot;
			loaded.hold   = asRH_NONE;
			loaded.varRef = asNO_SLOT;
		}
		else
		{
			loaded = PointeeOf(addr);
		}
		if( !SetCells(s, 0, loaded) )
		{
			return false;
		}
		break;
	}
	case asBC_ADDSi:
	{
		asSAbstractValue a;
		if( !PtrAt(s, 0, a) )
		{
			return false;
		}
		short offset = asBC_SWORDARG0(instr);
		if( !SetCells(s, 0, FieldAt(a, int(asBC_DWORDARG(instr)), offset)) )
		{
			return false;
		}
		break;
	}
	case asBC_LoadThisR:
	{
		asSAbstractValue *self = Var(s, 0);
		if( self == 0 )
		{
			return false;
		}
		short offset = asBC_SWORDARG0(instr);
		s.valueReg = FieldAt(*self, int(asBC_DWORDARG(instr)), offset);
		break;
	}
	case asBC_LoadRObjR:
	{
		// rW_W_DW: the type id is in the third dword, after the two words
		asSAbstractValue *base = Var(s, asBC_SWORDARG0(instr));
		if( base == 0 )
		{
			return false;
		}
		short offset = asBC_SWORDARG1(instr);
		s.valueReg = FieldAt(*base, int(asBC_DWORDARG(instr + 1)), offset);
		break;
	}
	case asBC_LoadVObjR:
	{
		// A field of a value object stored inline in the frame
		short at = asBC_SWORDARG0(instr);
		if( Var(s, at) == 0 )
		{
			return false;
		}
		asSAbstractValue addr = Value(asMA_NONE, asMA_PROGRAM, asRH_NONE);
		addr.slot = at;
		short offset = asBC_SWORDARG1(instr);
		s.valueReg = FieldAt(addr, int(asBC_DWORDARG(instr + 1)), offset);
		break;
	}
	case asBC_LDV:
	{
		short at = asBC_SWORDARG0(instr);
		asSAbstractValue *slot = Var(s, at);
		if( slot == 0 )
		{
			return false;
		}
		s.valueReg = AddressOf(*slot);
		s.valueReg.slot = at;
		break;
	}
	case asBC_PopRPtr:
	{
		asSAbstractValue a;
		if( !PtrAt(s, 0, a) )
		{
			return false;
		}
		s.valueReg = a;
		if( !Pop(s, AS_PTR_SIZE) )
		{
			return false;
		}
		break;
	}
	case asBC_PshRPtr:
		Push(s, s.valueReg, AS_PTR_SIZE);
		break;

	// Global variables. The address operand is at instr+1 in every one of them
	case asBC_PGA:
	{
		void *at = (void*)asBC_PTRARG(instr);
		Push(s, GlobalAddress(at), AS_PTR_SIZE);
		break;
	}
	case asBC_PshGPtr:
	{
		void *at = (void*)asBC_PTRARG(instr);
		RecordRead(GlobalOrigin(at));
		Push(s, PointeeOf(GlobalAddress(at)), AS_PTR_SIZE);
		break;
	}
	case asBC_LDG:
	{
		void *at = (void*)asBC_PTRARG(instr);
		s.valueReg = GlobalAddress(at);
		break;
	}
	case asBC_LdGRdR4:
	{
		void *at = (void*)asBC_PTRARG(instr);
		RecordRead(GlobalOrigin(at));
		if( !SetVarCells(s, asBC_SWORDARG0(instr), Unknown(), 1) )
		{
			return false;
		}
		// The VM leaves the global's address in the register
		s.valueReg = GlobalAddress(at);
		break;
	}
	case asBC_PshG4:
		RecordRead(GlobalOrigin((void*)asBC_PTRARG(instr)));
		Push(s, Unknown(), 1);
		break;
	case asBC_CpyGtoV4:
		RecordRead(GlobalOrigin((void*)asBC_PTRARG(instr)));
		if( !SetVarCells(s, asBC_SWORDARG0(instr), Unknown(), 1) )
		{
			return false;
		}
		break;
	case asBC_CpyVtoG4:
		if( Var(s, asBC_SWORDARG0(instr)) == 0 )
		{
			return false;
		}
		RecordWrite(GlobalOrigin((void*)asBC_PTRARG(instr)));
		break;
	case asBC_SetG4:
		RecordWrite(GlobalOrigin((void*)asBC_PTRARG(instr)));
		break;

	// Reads and writes through the value register
	case asBC_RDR1: case asBC_RDR2: case asBC_RDR4: case asBC_RDR8:
		RecordReadOf(s.valueReg);
		if( !SetVarCells(s, asBC_SWORDARG0(instr), Unknown(), op == asBC_RDR8 ? 2 : 1) )
		{
			return false;
		}
		break;
	case asBC_WRTV1: case asBC_WRTV2: case asBC_WRTV4: case asBC_WRTV8:
		RecordWriteOf(s.valueReg);
		ForgetSlot(s, s.valueReg, op == asBC_WRTV8 ? 2 : 1);
		break;
	case asBC_INCi8: case asBC_INCi16: case asBC_INCi: case asBC_INCf: case asBC_INCd: case asBC_INCi64:
	case asBC_DECi8: case asBC_DECi16: case asBC_DECi: case asBC_DECf: case asBC_DECd: case asBC_DECi64:
		RecordReadOf(s.valueReg);
		RecordWriteOf(s.valueReg);
		ForgetSlot(s, s.valueReg, (op == asBC_INCd || op == asBC_DECd || op == asBC_INCi64 || op == asBC_DECi64) ? 2 : 1);
		break;

	// COPY: reads *src, writes *dst, and leaves dst on the stack
	case asBC_COPY:
	{
		asSAbstractValue dst, src;
		if( !PtrAt(s, 0, dst) || !PtrAt(s, AS_PTR_SIZE, src) )
		{
			return false;
		}
		RecordReadOf(src);
		RecordWriteOf(dst);
		ForgetSlot(s, dst, asBC_WORDARG0(instr));
		if( !Pop(s, AS_PTR_SIZE) || !SetCells(s, 0, dst) )
		{
			return false;
		}
		break;
	}
	case asBC_ChkRefS:
	{
		asSAbstractValue a;
		if( !PtrAt(s, 0, a) )
		{
			return false;
		}
		RecordReadOf(a);
		break;
	}

	// Argument placeholders, patched just before a call
	case asBC_VAR:
	{
		asSAbstractValue v = Unknown();
		v.varRef = asBC_SWORDARG0(instr);
		Push(s, v, AS_PTR_SIZE);
		break;
	}
	case asBC_GETREF:
	{
		asSAbstractValue ph;
		if( !PtrAt(s, asBC_WORDARG0(instr), ph) || ph.varRef == asNO_SLOT )
		{
			return false;
		}
		asSAbstractValue *slot = Var(s, ph.varRef);
		if( slot == 0 )
		{
			return false;
		}
		asSAbstractValue v = AddressOf(*slot);
		v.slot = ph.varRef;
		if( !SetCells(s, asBC_WORDARG0(instr), v) )
		{
			return false;
		}
		break;
	}
	case asBC_GETOBJREF:
	{
		asSAbstractValue ph;
		if( !PtrAt(s, asBC_WORDARG0(instr), ph) || ph.varRef == asNO_SLOT )
		{
			return false;
		}
		asSAbstractValue *slot = Var(s, ph.varRef);
		if( slot == 0 )
		{
			return false;
		}
		asSAbstractValue v = *slot;
		v.hold   = asRH_NONE;
		v.varRef = asNO_SLOT;
		if( !SetCells(s, asBC_WORDARG0(instr), v) )
		{
			return false;
		}
		break;
	}
	case asBC_GETOBJ:
	{
		// Moves the reference into the argument. The callee destroys it, or the
		// exception clean-up does if the call never starts, so it counts here.
		asSAbstractValue ph;
		if( !PtrAt(s, asBC_WORDARG0(instr), ph) || ph.varRef == asNO_SLOT )
		{
			return false;
		}
		asSAbstractValue *slot = Var(s, ph.varRef);
		if( slot == 0 )
		{
			return false;
		}
		ReleaseValue(*slot, VarType(ph.varRef));
		asSAbstractValue moved = *slot;
		moved.varRef = asNO_SLOT;
		if( !SetVarCells(s, ph.varRef, Null(), AS_PTR_SIZE) || !SetCells(s, asBC_WORDARG0(instr), moved) )
		{
			return false;
		}
		break;
	}

	// Registers holding objects
	case asBC_LOADOBJ:
	{
		asSAbstractValue *slot = Var(s, asBC_SWORDARG0(instr));
		if( slot == 0 )
		{
			return false;
		}
		s.objectReg = *slot;
		if( !SetVarCells(s, asBC_SWORDARG0(instr), Null(), AS_PTR_SIZE) )
		{
			return false;
		}
		break;
	}
	case asBC_STOREOBJ:
		if( !SetVarCells(s, asBC_SWORDARG0(instr), s.objectReg, AS_PTR_SIZE) )
		{
			return false;
		}
		s.objectReg = Null();
		break;
	case asBC_ClrVPtr:
		if( !SetVarCells(s, asBC_SWORDARG0(instr), Null(), AS_PTR_SIZE) )
		{
			return false;
		}
		break;

	// Releases: only one that may be the last can destroy
	case asBC_FREE:
	{
		asSAbstractValue *slot = Var(s, asBC_SWORDARG0(instr));
		if( slot == 0 )
		{
			return false;
		}
		ReleaseValue(*slot, (asCTypeInfo*)asBC_PTRARG(instr));
		if( !SetVarCells(s, asBC_SWORDARG0(instr), Null(), AS_PTR_SIZE) )
		{
			return false;
		}
		break;
	}
	case asBC_REFCPY:
	{
		// Pops the destination address and leaves the source handle on top
		asSAbstractValue dest, value;
		if( !PtrAt(s, 0, dest) || !PtrAt(s, AS_PTR_SIZE, value) || !Pop(s, AS_PTR_SIZE) )
		{
			return false;
		}
		if( !StoreHandle(s, dest, value, (asCTypeInfo*)asBC_PTRARG(instr)) )
		{
			return false;
		}
		break;
	}
	case asBC_RefCpyV:
	{
		// PSF var; REFCPY
		asSAbstractValue value;
		if( !PtrAt(s, 0, value) )
		{
			return false;
		}
		asSAbstractValue dest = Value(asMA_NONE, asMA_PROGRAM, asRH_NONE);
		dest.slot = asBC_SWORDARG0(instr);
		if( !StoreHandle(s, dest, value, (asCTypeInfo*)asBC_PTRARG(instr)) )
		{
			return false;
		}
		break;
	}

	// Initialisation-list buffers: fresh memory only this frame holds
	case asBC_AllocMem:
		if( !SetVarCells(s, asBC_SWORDARG0(instr), Value(asMA_NONE, asMA_PROGRAM, asRH_OWNED), AS_PTR_SIZE) )
		{
			return false;
		}
		break;
	case asBC_SetListSize: case asBC_SetListType:
	{
		asSAbstractValue *slot = Var(s, asBC_SWORDARG0(instr));
		if( slot == 0 )
		{
			return false;
		}
		RecordWriteOf(*slot);
		ForgetSlot(s, FieldOf(*slot, asMA_PROGRAM, int(asBC_DWORDARG(instr))), 1);
		break;
	}
	case asBC_PshListElmnt:
	{
		asSAbstractValue *slot = Var(s, asBC_SWORDARG0(instr));
		if( slot == 0 )
		{
			return false;
		}
		Push(s, FieldOf(*slot, asMA_PROGRAM, int(asBC_DWORDARG(instr))), AS_PTR_SIZE);
		break;
	}

	// Calls: each adds its callee's scopes, rebased onto the object it is made on
	case asBC_CALL: case asBC_CALLSYS:
		if( !DoCall(s, FunctionById(asBC_INTARG(instr)), asCALLKIND_DIRECT) )
		{
			return false;
		}
		break;
	case asBC_CALLINTF:
		if( !DoCall(s, FunctionById(asBC_INTARG(instr)), asCALLKIND_DISPATCH) )
		{
			return false;
		}
		break;
	case asBC_Thiscall1:
	{
		// T &obj::f(int): pops `this` and one int; the reference lands in the value register
		asCScriptFunction *callee = FunctionById(asBC_INTARG(instr));
		if( callee == 0 || callee->objectType == 0 || callee->DoesReturnOnStack() || callee->GetSpaceNeededForArguments() != 1 )
		{
			return false;
		}
		if( !DoCall(s, callee, asCALLKIND_DIRECT) )
		{
			return false;
		}
		break;
	}
	case asBC_CallPtr:
	{
		// The signature comes from the funcdef type of the variable holding the pointer
		asCFuncdefType *fd = CastToFuncdefType(VarType(asBC_SWORDARG0(instr)));
		if( fd == 0 || !DoCall(s, fd->funcdef, asCALLKIND_UNKNOWN) )
		{
			return false;
		}
		break;
	}
	case asBC_CALLBND:
	{
		int importIdx = asBC_INTARG(instr) & ~FUNC_IMPORTED;
		if( importIdx < 0 || asUINT(importIdx) >= engine->importedFunctions.GetLength() || engine->importedFunctions[importIdx] == 0 )
		{
			return false;
		}
		if( !DoCall(s, engine->importedFunctions[importIdx]->importedFunctionSignature, asCALLKIND_UNKNOWN) )
		{
			return false;
		}
		break;
	}
	case asBC_ALLOC:
	{
		// Constructs a fresh object at *dest: the constructor runs on an object only this frame holds
		int ctorId = asBC_INTARG(instr + AS_PTR_SIZE);
		asCScriptFunction *ctor = 0;
		if( ctorId )
		{
			ctor = FunctionById(ctorId);
			if( ctor == 0 || ctor->IsVariadic() )
			{
				return false;
			}
		}
		// A template's hidden type argument is one of the constructor's parameters
		asUINT args = ctor ? asUINT(ctor->GetSpaceNeededForArguments()) : 0;
		asSAbstractValue destination;
		if( !PtrAt(s, args, destination) )
		{
			return false;
		}
		if( ctor )
		{
			asEMemoryAccess r, w;
			bool d;
			CalleeAccess(ctor, asCALLKIND_DIRECT, r, w, d);
			RecordRead(asMemoryAccessReadContribution(r, asMA_NONE));
			RecordWrite(asMemoryAccessContribution(w, asMA_NONE));
			if( d )
			{
				drops = true;
			}
			if( !ChargeArguments(s, ctor, 0) || !CleanNativeArgs(s, ctor, 0) )
			{
				return false;
			}
			for( asUINT n = 0; n < args; n++ )
			{
				asSAbstractValue cell = *Cell(s, n);
				ForgetArgument(s, cell);
			}
		}
		RecordWriteOf(destination);
		if( !Pop(s, args + AS_PTR_SIZE) )
		{
			return false;
		}
		if( destination.slot == asANY_SLOT )
		{
			ForgetSlot(s, destination, AS_PTR_SIZE);
		}
		else if( destination.slot != asNO_SLOT )
		{
			if( !SetVarCells(s, destination.slot, Value(asMA_NONE, asMA_PROGRAM, asRH_OWNED), AS_PTR_SIZE) )
			{
				return false;
			}
		}
		s.valueReg = Unknown();
		break;
	}
	case asBC_Cast:
	{
		// Reads the handle at the address; the result is a new reference to the same object
		asSAbstractValue addr;
		if( !PtrAt(s, 0, addr) )
		{
			return false;
		}
		RecordReadOf(addr);
		// The object keeps its origin. A type-home origin keeps the static type
		// it had before the cast: its home covers every class derived from it
		// or implementing it, and the object is one of those.
		asSAbstractValue cast = PointeeOf(addr);
		if( addr.slot == asANY_SLOT )
		{
			cast = Unknown();
		}
		else if( addr.slot != asNO_SLOT )
		{
			asSAbstractValue *slot = Var(s, addr.slot);
			if( slot == 0 )
			{
				return false;
			}
			cast = Value(slot->origin, asMA_PROGRAM, asRH_NONE);
			cast.originType = slot->originType;
		}
		if( !Pop(s, AS_PTR_SIZE) )
		{
			return false;
		}
		// A failed cast leaves the register null, which this value covers
		cast.hold = asRH_COPIED;
		s.objectReg = cast;
		break;
	}

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
