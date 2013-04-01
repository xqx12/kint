#include "inttrap.h"

#define TRAP_IDX_S(n) \
void __trap_idx_s##n(s##n rhs) \
{ \
	if (rhs < 0) \
		TRAP(rhs); \
}

TRAP_IDX_S(8)
TRAP_IDX_S(16)
TRAP_IDX_S(32)
TRAP_IDX_S(64)

#undef TRAP_IDX_S
