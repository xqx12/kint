#include "inttrap.h"

#define TRAP_ADD_S(n) \
void __trap_add_s##n(s##n lhs, s##n rhs) \
{ \
	if ((((lhs + rhs) ^ lhs) & ((lhs + rhs) ^ rhs)) < 0) \
		TRAP(lhs, rhs); \
}

TRAP_ADD_S(32)
TRAP_ADD_S(64)

#undef TRAP_ADD_S

#define TRAP_ADD_U(n) \
void __trap_add_u##n(u##n lhs, u##n rhs) \
{ \
	if (UINT##n##_MAX - lhs < rhs) \
		TRAP(lhs, rhs); \
}

TRAP_ADD_U(32)
TRAP_ADD_U(64)

#undef TRAP_ADD_U
