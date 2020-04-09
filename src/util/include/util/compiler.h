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

#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif

#define container_of(ptr, type, member)                         \
({                                                              \
	__typeof(((type *)0)->member) * _p = (ptr);		\
	(type *)((char *)_p - offsetof(type, member));          \
})


#ifndef __always_inline
#define __always_inline         inline __attribute__((always_inline))
#endif

#define __printf(_a, _b)        __attribute__((format(printf, (_a), (_b))))

#define __packed                __attribute__((packed))
#define __aligned(_size)        __attribute__((aligned((_size))))

#define __maybe_unused          __attribute__((__unused__))
#define __weak                  __attribute__((__weak__, __noinline__))

#define __read_mostly           __attribute__((__section__(".read_mostly")))

/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define barrier() asm volatile("" : : : "memory")

#define smp_mb()   asm volatile("lock; addl $0,-4(%%rsp)" : : : "memory", "cc")
#define smp_rmb()  smp_mb() /* TODO: lfence? */
#define smp_wmb()  barrier()
#define smp_read_barrier_depends()   do { } while (0)

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

#if GCC_VERSION < 40500
#define __builtin_ia32_pause()  asm volatile("pause" : : : "memory")
#endif


#ifndef _BullseyeCoverage
#define _BullseyeCoverage 0
#endif

#if _BullseyeCoverage
#define BullseyeCoverageSaveOff _Pragma("BullseyeCoverage save off")
#define BullseyeCoverageRestore _Pragma("BullseyeCoverage restore")

#ifndef _Static_assert
#define _Static_assert(...)
#endif

#else

#define BullseyeCoverageSaveOff
#define BullseyeCoverageRestore
#endif

#define NELEM(_a)               (sizeof(_a) / sizeof((_a)[0]))

#ifndef SMP_CACHE_BYTES
#define SMP_CACHE_BYTES         64
#endif

#endif /* MPOOL_UTIL_COMPILER_H */
