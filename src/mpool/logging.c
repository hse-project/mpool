// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>

#include <mpool/mpool.h>

void mpool_log(const char *file, int line, int pri, mpool_err_t err, const char *fmt, ...)
{
	const char         *dir;
	char                msg[256];
	int                 cnt = 0;
	va_list             ap;

	for (dir = file + strlen(file); dir > file; --dir) {
		if (*dir == '/' &&  ++cnt == 3) {
			++dir;
			break;
		}
	}

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	pri |= LOG_USER;

	if (err) {
		char errbuf[128];

		mpool_strinfo(err, errbuf, sizeof(errbuf));

		syslog(pri, "%s:%d: %s: %s\n", dir, line, msg, errbuf);
	} else {
		syslog(pri, "%s:%d: %s\n", dir, line, msg);
	}
}
