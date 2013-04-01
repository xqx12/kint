#include "inttrap.h"

#define TRAP_REM_S(n) \
void __trap_rem_s##n(s##n lhs, s##n rhs) \
{ \
	if (rhs == 0) \
		TRAP(lhs, rhs); \
}

TRAP_REM_S(32)
TRAP_REM_S(64)

#undef TRAP_REM_S

#define TRAP_REM_U(n) \
void __trap_rem_u##n(u##n lhs, u##n rhs) \
{ \
	if (rhs == 0) \
		TRAP(lhs, rhs); \
}

TRAP_REM_U(32)
TRAP_REM_U(64)

#undef TRAP_REM_U
