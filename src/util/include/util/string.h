/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_STRING_H
#define MPOOL_UTIL_STRING_H

/*
 * string.h - linux library code not in glibc
 *
 * See also:
 * https://www.sudo.ws/todd/papers/strlcpy.html
 * https://lwn.net/Articles/507319/
 * https://www.sourceware.org/ml/libc-alpha/2000-08/msg00053.html
 * https://linux.die.net/man/3/strlcpy
 *
 * That is, this is not without controversy.  But the code is useful,
 * as most code is, when used with care.
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <util/inttypes.h>

/**
 * strlcpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 *
 * Compatible with *BSD: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 */
size_t strlcpy(char *dest, const char *src, size_t size);

/**
 * strlcat - Concatenate a C-string to another in a fixed size buffer
 * @dest: The string to append to
 * @src: The string to append
 * @size: Total size of destination buffer
 *
 * Compatible with *BSD: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 */
size_t strlcat(char *dest, const char *src, size_t size);

/**
 * strimpull() - Remove the leading and trailing whitespace in a string, pull
 *	the remaining at the beginning of the buffer.
 * @buf: input must be zero terminated string
 *
 * Return: the value of the input parameter "buf".
 *	The buffer "buf" content returned is a zero terminated string.
 *	In the case the input buffer contained only white spaces, then
 *	the string "" is returned.
 */
char *strimpull(char *buf);

#endif /* MPOOL_UTIL_STRING_H */
