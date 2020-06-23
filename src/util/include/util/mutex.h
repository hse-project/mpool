/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_MUTEX_H
#define MPOOL_UTIL_MUTEX_H

/*
 * This file provides the following APIs:
 *
 *    struct mutex;
 *
 *    DEFINE_MUTEX(name);
 *    void mutex_init(struct mutex *mutex);
 *    void mutex_destroy(struct mutex *mutex);
 *
 *    void mutex_lock(struct mutex *mutex);
 *    void mutex_unlock(struct mutex *mutex);
 *
 *    int mutex_trylock(struct mutex *mutex);
 *
 *
 * From linux/mutex.h:
 *
 *   Simple, straightforward mutexes with strict semantics:
 *
 *   - only one task can hold the mutex at a time
 *   - only the owner can unlock the mutex
 *   - multiple unlocks are not permitted
 *   - recursive locking is not permitted
 *   - a mutex object must be initialized via the API
 *   - a mutex object must not be initialized via memset or copying
 *   - task may not exit with mutex held
 *   - memory areas where held locks reside must not be freed
 *   - held mutexes must not be reinitialized
 *   - mutexes may not be used in hardware or software interrupt
 *     contexts such as tasklets and timers
 */

/* From http://lkml.iu.edu/hypermail/linux/kernel/0103.1/0030.html:
 *
 * On Linux, the default mutexes implement a strict fairness policy; when
 * a mutex is unlocked, ownership is transferred to one of the threads
 * waiting for it, according to priority---even if some currently running
 * thread is prime and ready to seize the lock, it must wait its turn.
 * This behavior can readily lead to strict alternation under SMP,
 * because as thread is busy inside the mutex, the other thread can execute
 * on another processor and independently reach the pthread_mutex_lock()
 * statement, at which point it is guaranteed that it is eligible to get
 * that mutex as soon as it is unlocked.
 *
 * As you can see, the normal mutex now is PTHREAD_MUTEX_TIMED_NP, in
 * order to support the pthread_mutex_timedlock operation (which only
 * works with this mutex type). This mutex also has the fair scheduling
 * behavior that is so detrimental in some SMP scenarios. It's
 * essentially your deluxe model with all the fixin's.
 *
 * The PTHRED_MUTEX_ADAPTIVE_NP is a new mutex that is intended for high
 * throughput at the sacrifice of fairness and even CPU cycles. This
 * mutex does not transfer ownership to a waiting thread, but rather
 * allows for competition. Also, over an SMP kernel, the lock operation
 * uses spinning to retry the lock to avoid the cost of immediate
 * descheduling.
 */

#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include <util/compiler.h>

struct mutex {
	pthread_mutex_t pth_mutex;
};

#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname = { .pth_mutex = PTHREAD_MUTEX_INITIALIZER }

static inline
void
mutex_init(struct mutex *mutex)
{
	DEFINE_MUTEX(tmp);
	*mutex = tmp;
}

static __always_inline
void
mutex_destroy(struct mutex *mutex)
{
	int rc;

	rc = pthread_mutex_destroy(&mutex->pth_mutex);

	if (unlikely(rc))
		abort();
}

static __always_inline
void
mutex_lock(struct mutex *mutex)
{
	int rc;

	rc = pthread_mutex_lock(&mutex->pth_mutex);

	if (unlikely(rc))
		abort();
}

static __always_inline
void
mutex_unlock(struct mutex *mutex)
{
	int rc;

	rc = pthread_mutex_unlock(&mutex->pth_mutex);

	if (unlikely(rc))
		abort();
}

#endif /* MPOOL_UTIL_MUTEX_H */
