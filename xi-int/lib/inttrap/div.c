#include "inttrap.h"

#define TRAP_DIV_S(n) \
void __trap_div_s##n(s##n lhs, s##n rhs) \
{ \
	if ((rhs == 0) || ((lhs == INT##n##_MIN) && (rhs == -1))) \
		TRAP(lhs, rhs); \
}

TRAP_DIV_S(32)
TRAP_DIV_S(64)

#undef TRAP_DIV_S

#define TRAP_DIV_U(n) \
void __trap_div_u##n(u##n lhs, u##n rhs) \
{ \
	if (rhs == 0) \
		TRAP(lhs, rhs); \
}

TRAP_DIV_U(32)
TRAP_DIV_U(64)

#undef TRAP_DIV_U
