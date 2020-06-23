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

/* From include/linux/kernel.h
 * Note: 'a' must be a power 2.
 */
#define IS_ALIGNED(x, a)    (((x) & ((typeof(x))(a) - 1)) == 0)

/* test whether an address (unsigned long or pointer) is aligned to PAGE_SIZE */
#define PAGE_ALIGNED(addr)      IS_ALIGNED((unsigned long)addr, PAGE_SIZE)

#endif /* MPOOL_UTIL_PAGE_H */
