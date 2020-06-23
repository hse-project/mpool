/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_PARSE_NUM_H
#define MPOOL_UTIL_PARSE_NUM_H

#include <util/inttypes.h>

#include <mpool/mpool.h>

mpool_err_t
parse_u64_range(const char *str, char **endptr, u64 min_accept, u64 max_accept, u64 *result);

mpool_err_t
parse_s64_range(const char *str, char **endptr, s64 min_accept, s64 max_accept, s64 *result);

mpool_err_t parse_size_range(const char *str, u64 min_accept, u64 max_accept, u64 *result);

static inline mpool_err_t parse_size(const char *str, u64 *result)
{
	return parse_size_range(str, 0, 0, result);
}

#define __parse_signed_func(FNAME, TYP, TMIN, TMAX)			\
	static inline mpool_err_t FNAME(const char *str, TYP * result)	\
	{								\
		mpool_err_t err;						\
		s64 tmp;						\
		err = parse_s64_range(str, (char **)0, TMIN, TMAX, &tmp); \
		if (!err)						\
			*result = (TYP)tmp;				\
		return err;						\
	}

#define __parse_unsigned_func(FNAME, TYP, TMIN, TMAX)			\
	static inline mpool_err_t FNAME(const char *str, TYP * result)	\
	{								\
		mpool_err_t err;						\
		u64 tmp;						\
		err = parse_u64_range(str, (char **)0, TMIN, TMAX, &tmp); \
		if (!err)						\
			*result = (TYP)tmp;				\
		return err;						\
	}

/* Declarations (for readability) */
static inline mpool_err_t parse_u8(const char *str, u8 *result);
static inline mpool_err_t parse_u16(const char *str, u16 *result);
static inline mpool_err_t parse_u32(const char *str, u32 *result);
static inline mpool_err_t parse_u64(const char *str, u64 *result);
static inline mpool_err_t parse_s64(const char *str, s64 *result);

/* definitions */
__parse_unsigned_func(parse_u8,   u8,  (u8)0,   U8_MAX)

__parse_unsigned_func(parse_u16,  u16, (u16)0,  U16_MAX)

__parse_unsigned_func(parse_u32,  u32, (u32)0,  U32_MAX)

__parse_unsigned_func(parse_u64,  u64, (u64)0,  U64_MAX)

__parse_signed_func(parse_s64,    s64, S64_MIN, S64_MAX)

#endif /* MPOOL_UTIL_PARSE_NUM_H */
