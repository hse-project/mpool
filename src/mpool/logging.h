/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_LOGGING_H
#define MPOOL_UTIL_LOGGING_H

#include <stdint.h>
#include <syslog.h>

#include "mpool_err.h"

#define MPOOL_MARK "[MPOOL] "

#define MPOOL_EMERG   LOG_EMERG,   MPOOL_MARK
#define MPOOL_ALERT   LOG_ALERT,   MPOOL_MARK
#define MPOOL_CRIT    LOG_CRIT,    MPOOL_MARK
#define MPOOL_ERR     LOG_ERR,     MPOOL_MARK
#define MPOOL_WARNING LOG_WARNING, MPOOL_MARK
#define MPOOL_NOTICE  LOG_NOTICE,  MPOOL_MARK
#define MPOOL_INFO    LOG_INFO,    MPOOL_MARK
#define MPOOL_DEBUG   LOG_DEBUG,   MPOOL_MARK


#define mpool_log_pri(_pri, _fmt, _err, ...)				\
	mpool_log(__FILE__, __LINE__, (_pri), (_err), _fmt, ## __VA_ARGS__)


#define mp_pr_crit(_fmt, _err, ...)				\
	mpool_log_pri(LOG_CRIT, (_fmt), (_err), ## __VA_ARGS__)

#define mp_pr_err(_fmt, _err, ...)				\
	mpool_log_pri(LOG_ERR, (_fmt), (_err), ## __VA_ARGS__)

#define mp_pr_warn(_fmt, ...)					\
	mpool_log_pri(LOG_WARNING, (_fmt), 0, ## __VA_ARGS__)

#define mp_pr_notice(_fmt, ...)				\
	mpool_log_pri(LOG_NOTICE, (_fmt), 0, ## __VA_ARGS__)

#define mp_pr_info(_fmt, ...)				\
	mpool_log_pri(LOG_INFO, (_fmt), 0, ## __VA_ARGS__)

#define mp_pr_debug(_fmt, _err, ...)					\
	mpool_log_pri(LOG_DEBUG, (_fmt), (_err), ## __VA_ARGS__)


/* The following "mpool_" macros are deprecated, do not use in new code.
 */
#define mse_log(log_fmt, ...)				\
	mpool_log_pri(log_fmt, 0, ## __VA_ARGS__)

#define mpool_elog(_log_fmt, _err, ...)			\
	mpool_log_pri(_log_fmt, (_err), ## __VA_ARGS__)


void
mpool_log(
	const char *file,
	int         line,
	int         pri,
	merr_t      err,
	const char *fmt,
	...) __printf(5, 6);

#endif
