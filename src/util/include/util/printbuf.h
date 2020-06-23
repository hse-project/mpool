/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_PRINTBUF_H
#define MPOOL_UTIL_PRINTBUF_H

#include <util/inttypes.h>

/**
 * snprintf_append - append a formatted char string to a buffer.
 * @buf:    char *, pre-allocated buffer to which the formatted string
 *              should be appended.
 * @buf_sz: size_t, allocated size of buf
 * @offset: size_t *, offset at which to append the string. This will be
 *              incremented by the length of the string.
 * @format: standard printf format string
 * @...:    variable argument list to be passed to vnsprintf
 *
 * Standard snprintf has several unfortunate characteristics that make
 * it hard to use for iteratively filling a buffer. In particular, its
 * behavior when a given write would exceed the indicated max write.
 * Instead of returning the number of characters written, ala sprintf,
 * it returns how many characters it could have written without the barrier.
 *
 * snprintf_append provides a convenient way to manage multiple iterative
 * writes to a buffer. Each write is guaranteed not to overflow the buffer
 * and the offset is automatically advanced. A write that would have
 * overflowed the buffer is stopped at the buffer's end.
 *
 * Return: The return code from vsnprintf().
 */
int snprintf_append(char *buf, size_t buf_sz, size_t *offset, const char *format, ...);

/**
 * strlcpy_append() - append %src to (dst + *offsetp)
 *
 * An efficient version of snprintf_append() for simple strings.
 * (e.g., snprintf_append(dst, dstsz, &offset, "%s", src)).
 *
 * Return: The return code from strlcpy().
 */
int strlcpy_append(char *dst, const char *src, size_t dstsz, size_t *offsetp);

#endif /* MPOOL_UTIL_PRINTBUF_H */
