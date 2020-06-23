// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <util/platform.h>
#include <util/compiler.h>
#include <util/string.h>
#include <util/printbuf.h>

#include <stdarg.h>

static int
vsnprintf_append(char *buf, size_t buf_sz, size_t *offset, const char *format, va_list args)
{
	int     cc;

	cc = vsnprintf(buf + *offset, buf_sz - *offset, format, args);

	if (cc < 0 || cc > (buf_sz - *offset))
		*offset = buf_sz;
	else
		*offset += cc;

	return cc;
}

int snprintf_append(char *buf, size_t buf_sz, size_t *offset, const char *format, ...)
{
	int         ret;
	va_list     args;

	va_start(args, format);
	ret = vsnprintf_append(buf, buf_sz, offset, format, args);
	va_end(args);

	return ret;
}


#ifndef MPOOL_BUILD_LIBMPOOL

int strlcpy_append(char *dst, const char *src, size_t dstsz, size_t *offset)
{
	int     cc;

	cc = strlcpy(dst + *offset, src, dstsz - *offset);

	if (cc > (dstsz - *offset))
		*offset = dstsz;
	else
		*offset += cc;

	return cc;
}

#endif
