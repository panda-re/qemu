#ifndef __PANDA_CHEADERS_H__
#define __PANDA_CHEADERS_H__

// ugh these are here so that g++ can actually handle gnarly qemu code

/*
	pthread is insane and, if built by a C++ compiler, will define a class.
	given we're going to "fix" C code which legally uses the class reserved
	word as an identifier, anything we include which tries to use the keyword
	"correctly" (be C and C++ compatible) will break. Given the use case for
	this header, anyone including this is also transitively including pthread.h
	anyways, so we'll just include it first so it isn't mangled.

	Any other similarly behaving library will require the same treatment.
	We can only pray we don't also end up with order-dependent includes...
*/
#include <pthread.h>

#ifdef __cplusplus
#include <type_traits>
#pragma push_macro("new")
#pragma push_macro("typeof")
#pragma push_macro("export")
#define new pandanew
#define typeof(x) std::remove_const<std::remove_reference<decltype(x)>::type>::type
#define export pandaexport

#pragma push_macro("typename")
#define typename typename__

#pragma push_macro("typeof_strip_qual")
#define typeof_strip_qual(expr) std::remove_cv<typeof(expr)>::type

#pragma push_macro("class")
#define class _csymbol_named_class

#pragma push_macro("_Static_assert")
#define _Static_assert static_assert

#pragma push_macro("__builtin_types_compatible_p")
#define __builtin_types_compatible_p(T1, T2) (std::is_same<std::remove_cv<T1>::type, std::remove_cv<T2>::type>::value ? 1 : 0)

extern "C" {
#endif

#ifdef CONFIG_SOFTMMU
#include "config-host.h"
// #include "config-target.h"
#include "cpu.h"
#endif


#include "panda/common.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "disas/disas.h"

// Don't forget to undefine it so people can actually use C++ stuff...
#ifdef __cplusplus
}
#pragma pop_macro("new")
#pragma pop_macro("typename")
#pragma pop_macro("typeof")
#pragma pop_macro("export")
#pragma pop_macro("typeof_strip_qual")
#pragma pop_macro("class")
#pragma pop_macro("_Static_assert")
#pragma pop_macro("__builtin_types_compatible_p")
#endif

#endif
