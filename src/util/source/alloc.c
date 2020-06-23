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

unsigned long __weak __get_free_page(unsigned int flags)
{
	return (unsigned long)aligned_alloc(4096, 4096);
}

unsigned long __weak get_zeroed_page(unsigned int flags)
{
	void *mem;

	mem = aligned_alloc(4096, 4096);
	if (mem)
		memset(mem, 0, 4096);

	return (unsigned long)mem;
}

void __weak free_page(unsigned long addr)
{
	free((void *)addr);
}
