#pragma once

#include_next <asm/bug.h>

#ifndef HAVE_ARCH_BUG_ON
#define HAVE_ARCH_BUG_ON
#endif

#ifndef HAVE_ARCH_WARN_ON
#define HAVE_ARCH_WARN_ON
#endif

#ifdef BUG_ON
#undef BUG_ON
#endif

#define BUG_ON(condition) 0

#ifdef WARN_ON
#undef WARN_ON
#endif

#define WARN_ON(condition) 0
