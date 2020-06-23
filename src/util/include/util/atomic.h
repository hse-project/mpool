/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_ATOMIC_H
#define MPOOL_UTIL_ATOMIC_H

/*
 * This file provides user-space implementations of the Linux kernel atomic operations:
 *
 * Notes:
 *  - This file implements a limited subset of kernel atomic operations.
 *  - The atomics implemented in this file use GCC's atomic bultins and are probably not as
 *    performant as the kernel atomics. If performance becomes an issue, look at the open source
 *    libatomic_ops library.
 */

#include <util/compiler.h>

typedef struct {
	int counter;
} atomic_t;

#define ATOMIC_INIT(i)	 { (i) }

/*----------------------------------------------------------------
 * 32-bit atomics
 */

/*
 * Atomically reads the value of @v.
 * Doesn't imply a read memory barrier.
 */
static inline int atomic_read(const atomic_t *v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_RELAXED);
}

/*
 * Atomically sets the value of @v to @i.
 * Doesn't imply a read memory barrier.
 */
static inline void atomic_set(atomic_t *v, int i)
{
	__atomic_store_n(&v->counter, i, __ATOMIC_RELAXED);
}


/* Atomically increments @v by 1. */
static inline void atomic_inc(atomic_t *v)
{
	(void)__atomic_fetch_add(&v->counter, 1, __ATOMIC_RELAXED);
}

/* Atomically decrements @v by 1. */
static inline void atomic_dec(atomic_t *v)
{
	(void)__atomic_fetch_sub(&v->counter, 1, __ATOMIC_RELAXED);
}

static inline int atomic_inc_return(atomic_t *v)
{
	return __atomic_add_fetch(&v->counter, 1, __ATOMIC_RELAXED);
}

/*
 * Atomically sets v to newv if it was equal to oldv and returns the old value.
 */
static inline int atomic_cmpxchg(atomic_t *v, int oldv, int newv)
{
	int retv = oldv;

	__atomic_compare_exchange_n(&v->counter, &retv, newv, 0,
				    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);

	return retv;
}

/* The read must complete (in program order) before any subsequent
 * load or store is performed.
 */
static inline int atomic_read_acq(const atomic_t *v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_ACQUIRE);
}

#endif /* MPOOL_UTIL_ATOMIC_H */
