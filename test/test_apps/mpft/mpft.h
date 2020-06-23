/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPFT_H
#define MPOOL_MPFT_H

#include <mpool/mpool.h>

typedef mpool_err_t (test_func_t)(int argc, char **argv);
typedef void   (help_func_t)(void);

enum mpft_test_type {
	MPFT_TEST_TYPE_INVALID,
	MPFT_TEST_TYPE_PERF,
	MPFT_TEST_TYPE_CORRECTNESS,
	MPFT_TEST_TYPE_STRESS,
	MPFT_TEST_TYPE_COMPOUND,
	MPFT_TEST_TYPE_ACTOR,
};

struct test_s {
	char               *test_name;
	enum mpft_test_type test_type;
	test_func_t        *test_func;
	help_func_t        *test_help;
};

struct group_s {
	char           *group_name;
	struct test_s  *group_test;
	help_func_t    *group_help;
};

extern uint8_t *pattern;
extern uint32_t pattern_len;

int pattern_base(char *base);

void pattern_fill(char *buf, uint32_t buf_sz);

int pattern_compare(char *buf, uint32_t buf_sz);

mpool_err_t mpft_launch_actor(char *actor, ...);

#endif /* MPOOL_MPFT_H */
