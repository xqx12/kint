#include "inttrap.h"

#define TRAP_MUL_S(n, n2) \
void __trap_mul_s##n(s##n lhs, s##n rhs) \
{ \
	s##n2 tmp = (s##n2)lhs * (s##n2)rhs; \
	if ((s##n)(tmp >> n) != ((s##n)tmp) >> (n - 1)) \
			TRAP(lhs, rhs); \
}

TRAP_MUL_S(32, 64)
TRAP_MUL_S(64, 128)

#undef TRAP_MUL_S

#define TRAP_MUL_U(n) \
void __trap_mul_u##n(u##n lhs, u##n rhs) \
{ \
	if ((rhs != 0) && (lhs > UINT##n##_MAX / rhs)) \
		TRAP(lhs, rhs); \
}

TRAP_MUL_U(32)
TRAP_MUL_U(64)

#undef TRAP_MUL_U
