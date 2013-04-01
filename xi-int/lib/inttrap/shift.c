#include "inttrap.h"

#define TRAP_SHL_U(n) \
void __trap_shl_u##n(u##n lhs, u##n rhs) \
{ \
	if (rhs >= n) \
		TRAP(lhs, rhs); \
}

TRAP_SHL_U(32)
TRAP_SHL_U(64)

#undef TRAP_SHL_U

#define TRAP_SHR_U(n) \
void __trap_shr_u##n(u##n lhs, u##n rhs) \
{ \
	if (rhs >= n) \
		TRAP(lhs, rhs); \
}

TRAP_SHR_U(32)
TRAP_SHR_U(64)

#undef TRAP_SHR_U
