/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_RWSEM_H
#define MPOOL_UTIL_RWSEM_H

/*
 * Summary:
 *
 *   init_rwsem -- intialize to unlocked state
 *
 *   down_read/down_write -- acquire lock for reading/writing
 *
 *   down_read_tryock/down_write_trylock
 *     -- try to acquire lock for reading/writing.
 *        returns !0 on success, 0 on fail.
 *
 *   up_read/up_write -- release lcok for reading/writing
 *
 * NOTES:
 *  - The userspace implementation uses classic read/write locks and
 *    allows (1) one writer with no readers, or (2) no writers with
 *    multiple readers.
 */

#include <stdio.h>
#include <assert.h>
#include <pthread.h>

struct rw_semaphore {
	pthread_rwlock_t rwsemlock;
};

#define __RWSEM_INITIALIZER(name) { .rwsemlock = PTHREAD_RWLOCK_INITIALIZER }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

static inline
void
init_rwsem(struct rw_semaphore *sem)
{
	struct rw_semaphore  tmp;
	pthread_rwlockattr_t attr;
	int rc;

	rc = pthread_rwlockattr_init(&attr);
	if (rc)
		fprintf(stderr, "%s: pthread_rwlockattr_init() failed: %d",
			__func__, rc);

	rc = pthread_rwlockattr_setkind_np(&attr,
					   PTHREAD_RWLOCK_PREFER_WRITER_NP);
	if (rc)
		fprintf(stderr, "%s: pthread_rwlockattr_setkind_np() failed: %d",
			__func__, rc);

	rc = pthread_rwlock_init(&tmp.rwsemlock, &attr);
	if (rc)
		fprintf(stderr, "%s: pthread_rwlock_init() failed: %d",
			__func__, rc);

	rc = pthread_rwlockattr_destroy(&attr);
	if (rc)
		fprintf(stderr, "%s: pthread_rwlockattr_destroy() failed: %d",
			__func__, rc);

	*sem = tmp;
}

static inline
void
init_rwsem_reader(struct rw_semaphore *sem)
{
	int rc;

	rc = pthread_rwlock_init(&sem->rwsemlock, NULL);
	if (rc)
		fprintf(stderr, "%s: pthread_rwlock_init() failed: %d",
			__func__, rc);
}

static __always_inline
void
down_read(struct rw_semaphore *sem)
{
	int rc __maybe_unused;

	rc = pthread_rwlock_rdlock(&sem->rwsemlock);
	assert(rc == 0);
}

static __always_inline
void
down_write(struct rw_semaphore *sem)
{
	int rc __maybe_unused;

	rc = pthread_rwlock_wrlock(&sem->rwsemlock);
	assert(rc == 0);
}

static __always_inline
void
up_read(struct rw_semaphore *sem)
{
	int rc __maybe_unused;

	rc = pthread_rwlock_unlock(&sem->rwsemlock);
	assert(rc == 0);
}

static __always_inline
void
up_write(struct rw_semaphore *sem)
{
	int rc __maybe_unused;

	rc = pthread_rwlock_unlock(&sem->rwsemlock);
	assert(rc == 0);
}

#define down_read_nested(sem, subclass)    down_read(sem)
#define down_write_nested(sem, subclass)   down_write(sem)

#endif
