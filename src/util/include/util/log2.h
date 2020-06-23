/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_LOG2_H
#define MPOOL_UTIL_LOG2_H

#include <stdbool.h>
#include <assert.h>
#include <sys/param.h>

static inline __attribute__((const))
unsigned int
ilog2(unsigned long n)
{
	assert(n > 0);

	return (NBBY * sizeof(n) - 1) - __builtin_clzl(n);
}

#endif /* MPOOL_UTIL_LOG2_H */
