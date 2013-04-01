#pragma once

#include <stdint.h>
#include <limits.h>

typedef int8_t      s8;
typedef int16_t     s16;
typedef int32_t     s32;
typedef int64_t     s64;
typedef __int128_t  s128;

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;
typedef __uint128_t u128;

extern void __trap_anchor(const char *, ...);

#define TRAP(...) __trap_anchor(__func__, __VA_ARGS__)
