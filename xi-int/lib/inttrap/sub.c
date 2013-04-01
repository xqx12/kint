#include "inttrap.h"

#define TRAP_SUB_S(n) \
void __trap_sub_s##n(s##n lhs, s##n rhs) \
{ \
	if ((((lhs - rhs) ^ lhs) & (lhs ^ rhs)) < 0) \
		TRAP(lhs, rhs); \
}

TRAP_SUB_S(32)
TRAP_SUB_S(64)

#undef TRAP_SUB_S

#define TRAP_SUB_U(n) \
void __trap_sub_u##n(u##n lhs, u##n rhs) \
{ \
	if (lhs < rhs) \
		TRAP(lhs, rhs); \
}

TRAP_SUB_U(32)
TRAP_SUB_U(64)

#undef TRAP_SUB_U
