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

#ifndef __USE_ISOC11
void *aligned_alloc(size_t align, size_t size);
#endif

void *hse_page_alloc(void);
void *hse_page_zalloc(void);
void hse_page_free(void *mem);

#endif /* MPOOL_UTIL_ALLOC_H */
