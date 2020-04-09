/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MLOG_THREAD_H
#define MPOOL_MLOG_THREAD_H

#include <util/atomic.h>

#include <mpool/mpool.h>

enum thread_state { NOT_STARTED, STARTED };
extern volatile enum thread_state mpft_thread_state;


typedef void *(thread_func_t)(void *arg);

struct mpft_thread_args {
	int                instance;
	pthread_mutex_t   *start_mutex;
	pthread_cond_t    *start_line;
	atomic_t          *start_cnt;
	void              *arg;
};

struct mpft_thread_resp {
	int    instance;
	mpool_err_t err;
	void  *resp;
};

mpool_err_t
mpft_thread(
	int            thread_cnt,
	thread_func_t  func,
	struct mpft_thread_args *targs,
	struct mpft_thread_resp *tresp);

void
mpft_thread_wait_for_start(
	struct mpft_thread_args *targs);

#endif /* MPOOL_MLOG_THREAD_H */

