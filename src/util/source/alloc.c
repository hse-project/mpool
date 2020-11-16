// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <util/compiler.h>
#include <util/alloc.h>

#include <string.h>

#ifndef __USE_ISOC11
void *aligned_alloc(size_t align, size_t size)
{
	void   *mem = NULL;

	if (posix_memalign(&mem, align, size))
		return NULL;

	return mem;
}
#endif

void * __weak
hse_page_alloc(void)
{
	return aligned_alloc(4096, 4096);
}

void * __weak
hse_page_zalloc(void)
{
	void *mem;

	mem = aligned_alloc(4096, 4096);
	if (mem)
		memset(mem, 0, 4096);

	return mem;
}

void __weak
hse_page_free(void *mem)
{
	free(mem);
}
