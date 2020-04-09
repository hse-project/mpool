/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_PAGE_H
#define MPOOL_UTIL_PAGE_H

#include <unistd.h>
#include <sys/mman.h>

/* verified at run time during platform init */

#define PAGE_SHIFT		12
#define PAGE_SIZE		(1UL << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

/* From include/uapi/linux/kernel.h */
#ifdef __ALIGN_KERNEL
#undef __ALIGN_KERNEL
#endif
#define __ALIGN_KERNEL(x, a)          __ALIGN_KERNEL_MASK(x, (typeof(x))(a)-1)

#define __ALIGN_KERNEL_MASK(x, mask)  (((x) + (mask)) & ~(mask))

/* From include/linux/kernel.h */
#define ALIGN(x, a)             __ALIGN_KERNEL((x), (a))
#define __ALIGN_MASK(x, mask)   __ALIGN_KERNEL_MASK((x), (mask))
#define PTR_ALIGN(p, a)         ((typeof(p))ALIGN((unsigned long)(p), (a)))

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

/* test whether an address (unsigned long or pointer) is aligned to PAGE_SIZE */
#define PAGE_ALIGNED(addr)      IS_ALIGNED((unsigned long)addr, PAGE_SIZE)

/* From include/linux/kernel.h
 * Note: 'a' must be a power 2.
 */
#define IS_ALIGNED(x, a)    (((x) & ((typeof(x))(a) - 1)) == 0)

/* from include/linux/kernel.h */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif /* MPOOL_UTIL_PAGE_H */
