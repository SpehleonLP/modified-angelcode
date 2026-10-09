#pragma once
#include <angelscript.h>

// True when the function's bytecode calls a registered function. A fold leaves none.
inline bool CallsSystemFunction(asIScriptFunction *fn)
{
	asUINT length = 0;
	asDWORD *bc = fn->GetByteCode(&length);
	for( asUINT at = 0; at < length; )
	{
		asBYTE const op = *reinterpret_cast<asBYTE*>(&bc[at]);
		if( op == asBC_CALLSYS || op == asBC_Thiscall1 )
		{
			return true;
		}
		at += asBCTypeSize[asBCInfo[op].type];
	}
	return false;
}
