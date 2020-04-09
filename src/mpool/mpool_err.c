// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <util/page.h>
#include <util/string.h>
#include <util/minmax.h>

#include "mpool_err.h"

/* Layout of merr_t:
 *
 *   Field   #bits  Description
 *   ------  -----  ----------
 *   63..48   16    signed offset of (_merr_file - merr_base) / MERR_ALIGN
 *   47..32   16    line number
 *   31..31    1    reserved bits
 *   30..0    31    positive errno value
 */
#define MERR_FILE_SHIFT     (48)
#define MERR_LINE_SHIFT     (32)
#define MERR_RSVD_SHIFT     (31)

#define MERR_FILE_MASK      (0xffff000000000000ul)
#define MERR_LINE_MASK      (0x0000ffff00000000ul)
#define MERR_RSVD_MASK      (0x0000000080000000ul)
#define MERR_ERRNO_MASK     (0x000000007ffffffful)

/* MERR_BASE_SZ    Size of struct merr_base[] buffer for kernel file names
 */
#define MERR_BASE_SZ        (MERR_ALIGN * 64 * 2)

char mpool_merr_base[MERR_BASE_SZ] _merr_attributes = "mpool_merr_baseu";
char mpool_merr_bug0[] _merr_attributes = "mpool_merr_bug0u";
char mpool_merr_bug1[] _merr_attributes = "mpool_merr_bug1u";
char mpool_merr_bug2[] _merr_attributes = "mpool_merr_bug2u";
char mpool_merr_bug3[] _merr_attributes = "mpool_merr_bug3u";

extern uint8_t __start_mpool_merr;
extern uint8_t __stop_mpool_merr;

/**
 * mpool_merr_lineno() - Return the line number from given merr_t
 */
static int
mpool_merr_lineno(merr_t err)
{
	return (err & MERR_LINE_MASK) >> MERR_LINE_SHIFT;
}

merr_t
mpool_merr_pack(int errnum, const char *file, int line)
{
	merr_t  err = 0;
	s64     off;

	if (errnum == 0)
		return 0;

	if (errnum < 0)
		errnum = -errnum;

	if (file < (char *)&__start_mpool_merr ||
	    file >= (char *)&__stop_mpool_merr)
		file = mpool_merr_bug0;

	if (!file || !IS_ALIGNED((ulong)file, MERR_ALIGN))
		file = mpool_merr_bug1;

	off = (file - mpool_merr_base) / MERR_ALIGN;

	if (((s64)((u64)off << MERR_FILE_SHIFT) >> MERR_FILE_SHIFT) == off)
		err = (u64)off << MERR_FILE_SHIFT;

	err |= (line & MERR_LINE_MASK) << MERR_LINE_SHIFT;
	err |= errnum & MERR_ERRNO_MASK;

	return err;
}

static const char *
mpool_merr_file(merr_t err)
{
	const char *file;
	size_t      len;
	int         slash;
	s32         off;

	if (err == 0 || err == -1)
		return NULL;

	off = (s64)(err & MERR_FILE_MASK) >> MERR_FILE_SHIFT;

	file = mpool_merr_base + (off * MERR_ALIGN);

	if (file < (char *)&__start_mpool_merr ||
	    file >= (char *)&__stop_mpool_merr)
		return mpool_merr_bug3;

	len = strnlen(file, PATH_MAX);
	file += len;

	for (slash = 0; len-- > 0; --file) {
		if (*file && !isprint(*file))
			return mpool_merr_bug2;

		if (file[-1] == '/' && ++slash >= 2)
			break;
	}

	return file;
}

char *
mpool_strerror(
	merr_t  err,
	char   *buf,
	size_t  bufsz)
{
	int errnum = mpool_errno(err);

	if (errnum == EBUG)
		strlcpy(buf, "mpool software bug", bufsz);
	else
		strerror_r(errnum, buf, bufsz);

	return buf;
}

char *
mpool_strinfo(
	merr_t  err,
	char   *buf,
	size_t  bufsz)
{
	int n;

	if (!err) {
		strlcpy(buf, "Success", bufsz);
		return buf;
	}

	n = snprintf(buf, bufsz, "%s:%d: ",
		     mpool_merr_file(err) ?: "?",
		     mpool_merr_lineno(err));

	if (n >= 0 && n < bufsz)
		mpool_strerror(err, buf + n, bufsz - n);

	return buf;
}

/**
 * merr_errno() - Return the errno from given merr_t
 */
int
mpool_errno(merr_t merr)
{
	return merr & MERR_ERRNO_MASK;
}
