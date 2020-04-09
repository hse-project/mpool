/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_INTTYPES_H
#define MPOOL_UTIL_INTTYPES_H

#include <util/base.h>

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <linux/types.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef unsigned long long llu;
typedef long long          lld;
typedef unsigned long      pgoff_t;

/* copied from include/linux/kernel.h */
#ifndef U8_MAX
# define U8_MAX          ((u8)~0U)
# define S8_MAX          ((s8)(U8_MAX>>1))
# define S8_MIN          ((s8)(-S8_MAX - 1))
#endif

#ifndef U16_MAX
# define U16_MAX         ((u16)~0U)
# define S16_MAX         ((s16)(U16_MAX>>1))
# define S16_MIN         ((s16)(-S16_MAX - 1))
#endif

#ifndef U32_MAX
# define U32_MAX         ((u32)~0U)
# define S32_MAX         ((s32)(U32_MAX>>1))
# define S32_MIN         ((s32)(-S32_MAX - 1))
#endif

#ifndef U64_MAX
# define U64_MAX         ((u64)~0ULL)
# define S64_MAX         ((s64)(U64_MAX>>1))
# define S64_MIN         ((s64)(-S64_MAX - 1))
#endif

#endif
