/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_PARSER_H
#define MPOOL_UTIL_PARSER_H

#include <util/inttypes.h>

typedef struct {
	const char *from;
	const char *to;
} substring_t;

struct match_token {
	s32         token;
	const char *pattern;
};

typedef struct match_token match_table_t[];

/**
 * match_token(): - Find a token and optional arg in a string
 * @str:   string to examine for token/argument pairs
 * @table: array of struct match_token's enumerating the allowed option tokens
 * @arg:   pointer to &substring_t element
 *
 * The array @table must be terminated with a struct match_token whose
 * @pattern is set to NULL. This function is nearly identical to that from
 * the Linux kernel except that only one substring_t is matched. The Linux
 * kernel's version can match more than one but that facility is not used by
 * the rest of the kernel and only works at all in a very limited fashion.
 */
int match_token(const char *str, const match_table_t table, substring_t *arg);

#endif /* MPOOL_UTIL_PARSER_H */
