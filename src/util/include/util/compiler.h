/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_COMPILER_H
#define MPOOL_UTIL_COMPILER_H

/*
 * Assumes gcc
 */

#ifndef likely
#define likely(_expr)           __builtin_expect(!!(_expr), 1)
#endif

#ifndef unlikely
#define unlikely(_expr)         __builtin_expect(!!(_expr), 0)
#endif

#ifndef __always_inline
#define __always_inline         inline __attribute__((always_inline))
#endif

#define __printf(_a, _b)        __attribute__((format(printf, (_a), (_b))))

#define __packed                __attribute__((packed))

#ifndef __aligned
#define __aligned(_size)        __attribute__((aligned((_size))))
#endif

#define __maybe_unused          __attribute__((__unused__))
#define __weak                  __attribute__((__weak__, __noinline__))

/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define barrier() asm volatile("" : : : "memory")

#define smp_wmb()  barrier()

/*
 * There are multiple ways GCC_VERSION could be defined.  This mimics
 * the kernel's definition in include/linux/compiler-gcc.h.
 */
#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)

#if GCC_VERSION < 40700
#define _Static_assert(...)
#endif

#define NELEM(_a)               (sizeof(_a) / sizeof((_a)[0]))

#endif /* MPOOL_UTIL_COMPILER_H */
