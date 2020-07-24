/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_SAMPLE_COMMON_H
#define MPOOL_SAMPLE_COMMON_H

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <sys/param.h>
#include <sys/mman.h>

#include <mpool/mpool.h>

static inline void eprint(mpool_err_t err, const char *fmt, ...)
{
	char       msg[128];
	char       errbuf[128];
	va_list    ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s%s%s\n", msg, err ? ": " : "",
	       err ? mpool_strerror(err, errbuf, sizeof(errbuf)) : "");
}

static inline
int setup_mpool(const char *mpname, const char *devname, struct mpool **mp, uint8_t alloc_unit)
{
	struct mpool_params    params;
	mpool_err_t            err;

	mpool_params_init(&params);

	params.mp_mblocksz[MP_MED_CAPACITY] = alloc_unit;

	err = mpool_create(mpname, devname, &params, 0, NULL);
	if (err)
		return mpool_errno(err);

	err = mpool_open(mpname, 0, mp, NULL);
	if (err) {
		mpool_destroy(mpname, 0, NULL);
		return mpool_errno(err);
	}

	return 0;
}

static inline int alloc_and_prep_buf(void **buf, size_t buflen)
{
	int     fd, rc = 0;
	ssize_t cc;

	*buf = aligned_alloc(PAGE_SIZE, roundup(buflen, PAGE_SIZE));
	if (!(*buf))
		return ENOMEM;

	fd = open("/dev/random", O_RDONLY);
	if (fd < 0) {
		rc = errno;
		goto exit;
	}

	cc = read(fd, *buf, buflen);
	if (cc < 0)
		rc = errno;

	close(fd);
exit:
	if (rc) {
		free(*buf);
		*buf = NULL;
	}

	return rc;
}

#endif /* MPOOL_SAMPLE_COMMON_H */
