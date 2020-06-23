/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_ALLOC_H
#define MPOOL_UTIL_ALLOC_H

#ifndef _ISOC11_SOURCE
#define _ISOC11_SOURCE
#endif

#include <stdlib.h>

#define GFP_KERNEL                          0x00000004

#define kmalloc(_size, _flags)              malloc((_size))
#define kcalloc(_n, _size, _flags)          calloc((_n), (_size))
#define kzalloc(_size, _flags)              calloc(1, (_size))
#define kfree(_ptr)                         free((void *)(_ptr))

#ifndef __USE_ISOC11
void *aligned_alloc(size_t align, size_t size);
#endif

unsigned long __get_free_page(unsigned int flags);
unsigned long get_zeroed_page(unsigned int flags);
void free_page(unsigned long addr);

#endif /* MPOOL_UTIL_ALLOC_H */
