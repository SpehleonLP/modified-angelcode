/*
   AngelCode Scripting Library
   Copyright (c) 2003-2013 Andreas Jonsson

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
// as_atomic.h
//
// The asCAtomic class provides methods for performing threadsafe
// operations on a single dword, e.g. reference counting and 
// bitfields.
//



#ifndef AS_ATOMIC_H
#define AS_ATOMIC_H

#include "as_config.h"

#include <atomic>

BEGIN_AS_NAMESPACE

class asCAtomic
{
public:
	asCAtomic();

	// An acquire load: callers test get() == 1 to decide that they hold the
	// last reference and then run the destructor or delete the object, which
	// must observe every write the other holders made before their release.
	asDWORD get() const;

	// A relaxed store: it only initializes a counter that no other thread can
	// see yet; publishing the owning object provides the ordering.
	void    set(asDWORD val);

	// Increase and return new value
	asDWORD atomicInc();

	// Decrease and return new value
	asDWORD atomicDec();

protected:
	std::atomic<asDWORD> value;
};

// Counters are embedded in objects whose layout the engine and JIT derive
// offsets from, so the atomic must not widen the field.
static_assert(sizeof(asCAtomic) == 4, "asCAtomic must stay one dword");

// The external (application) and internal (engine) reference counts of an
// object that is deleted only once both are zero. They share one atomic word
// so that each decrement observes both counts at once: exactly one decrement,
// the one that takes the combined word to zero, reports the last reference.
// With two separate counters, two threads that each zero one counter can
// both see the other at zero and delete twice, and a thread that zeroes its
// counter and then reads the other can read an object already freed.
class asCDualRefCount
{
public:
	asCDualRefCount();

	// Acquire loads, for the same reason as asCAtomic::get()
	asDWORD getExternal() const;
	asDWORD getInternal() const;

	// Relaxed store; only initializes counts no other thread can see yet
	void    set(asDWORD external, asDWORD internal);

	// Increase and return the new value of that count
	asDWORD incExternal();
	asDWORD incInternal();

	// Decrease and return the new value of that count. isLastRef is set only
	// for the single decrement that leaves both counts at zero.
	asDWORD decExternal(bool &isLastRef);
	asDWORD decInternal(bool &isLastRef);

protected:
	// External count in the high dword, internal count in the low dword
	std::atomic<asQWORD> value;
};

END_AS_NAMESPACE

#endif
