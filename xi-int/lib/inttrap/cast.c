#include "inttrap.h"

#define TRAP_CAST_SS(nl, nr) \
void __trap_icast_s##nl##_s##nr(s##nr rhs) { \
	if (rhs < INT##nl##_MIN || rhs > INT##nl##_MAX) \
		TRAP(rhs); \
} \
void __trap_ecast_s##nl##_s##nr(s##nr rhs) { \
	if (rhs < INT##nl##_MIN || rhs > INT##nl##_MAX) \
		TRAP(rhs); \
}

TRAP_CAST_SS(8,  16)
TRAP_CAST_SS(8,  32)
TRAP_CAST_SS(8,  64)
TRAP_CAST_SS(16, 32)
TRAP_CAST_SS(16, 64)
TRAP_CAST_SS(32, 64)

#undef TRAP_CAST_SS

#define TRAP_CAST_SU(nl, nr) \
void __trap_icast_s##nl##_u##nr(u##nr rhs) { \
	if (rhs > INT##nl##_MAX) \
		TRAP(rhs); \
} \
void __trap_ecast_s##nl##_u##nr(u##nr rhs) { \
	if (rhs > INT##nl##_MAX) \
		TRAP(rhs); \
}

TRAP_CAST_SU(8,  8)
TRAP_CAST_SU(8,  16)
TRAP_CAST_SU(8,  32)
TRAP_CAST_SU(8,  64)
TRAP_CAST_SU(16, 16)
TRAP_CAST_SU(16, 32)
TRAP_CAST_SU(16, 64)
TRAP_CAST_SU(32, 32)
TRAP_CAST_SU(32, 64)
TRAP_CAST_SU(64, 64)

#undef TRAP_CAST_SU

#define TRAP_CAST_UU(nl, nr) \
void __trap_icast_u##nl##_u##nr(u##nr rhs) { \
	if (rhs > UINT##nl##_MAX) \
		TRAP(rhs); \
} \
void __trap_ecast_u##nl##_u##nr(u##nr rhs) { \
	if (rhs > UINT##nl##_MAX) \
		TRAP(rhs); \
}

TRAP_CAST_UU(8,  16)
TRAP_CAST_UU(8,  32)
TRAP_CAST_UU(8,  64)
TRAP_CAST_UU(16, 32)
TRAP_CAST_UU(16, 64)
TRAP_CAST_UU(32, 64)

#undef TRAP_CAST_UU

#define TRAP_CAST_US(nl, nr) \
void __trap_icast_u##nl##_s##nr(s##nr rhs) { \
	if (rhs < 0 || rhs > UINT##nl##_MAX) \
		TRAP(rhs); \
} \
void __trap_ecast_u##nl##_s##nr(s##nr rhs) { \
	if (rhs < 0 || rhs > UINT##nl##_MAX) \
		TRAP(rhs); \
}

TRAP_CAST_US(8,  8)
TRAP_CAST_US(8,  16)
TRAP_CAST_US(8,  32)
TRAP_CAST_US(8,  64)
TRAP_CAST_US(16, 16)
TRAP_CAST_US(16, 32)
TRAP_CAST_US(16, 64)
TRAP_CAST_US(32, 32)
TRAP_CAST_US(32, 64)
TRAP_CAST_US(64, 64)

#undef TRAP_CASTUS
