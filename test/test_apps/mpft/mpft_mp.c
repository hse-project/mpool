// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include <util/platform.h>
#include <util/parse_num.h>
#include <util/param.h>
#include <mpool/mpool.h>

#include "mpft.h"
#include "mpft_thread.h"

#define merr(_errnum)   (_errnum)

#define EBUG            (666)

static void show_args(int argc, char **argv)
{
	int i;

	for (i = 0; i < argc; i++)
		fprintf(stdout, "\t[%d] %s\n", i, argv[i]);
}

/**
 * Create an mpool, open it, close it, destroy it. See that it is gone.
 */

char mp_correctness_simple_mpool[MPOOL_NAMESZ_MAX];

static struct param_inst mp_correctness_simple_params[] = {
	PARAM_INST_STRING(mp_correctness_simple_mpool,
			  sizeof(mp_correctness_simple_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mp_correctness_simple_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mp.correctness.simple [options]\n");

	show_default_params(mp_correctness_simple_params, 0);
}

#define ERROR_BUFFER_SIZE 256
#define BUFFER_SIZE 64

mpool_err_t mp_correctness_simple(int argc, char **argv)
{
	mpool_err_t err = 0, d_err;
	char  *mpool;
	int    next_arg = 0;
	char   errbuf[ERROR_BUFFER_SIZE];
	u64    oid1, oid2 = 0;

	struct mpool_mdc   *root_mdc;
	struct mpool       *mp;

	show_args(argc, argv);
	err = process_params(argc, argv, mp_correctness_simple_params, &next_arg, 0);
	if (err != 0) {
		printf("%s.%d process_params returned an error\n", __func__, __LINE__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mp_correctness_simple_mpool;

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	err = mpool_open(mpool, O_RDWR, &mp, NULL);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the mpool: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	err = mpool_mdc_get_root(mp, &oid1, &oid2);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to get root MDC OIDs: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_open(mp, oid1, oid2, 0, &root_mdc);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the root mdc: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_close(root_mdc);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close the root mdc: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_open(mp, oid1, oid2, 0, &root_mdc);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the root mdc: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_close(root_mdc);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close the root mdc: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp;
	}

close_mp:
	d_err = mpool_close(mp);
	if (d_err) {
		mpool_strinfo(d_err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mpool: %s\n",
			__func__, __LINE__, errbuf);
	}

	return err;
}

/**
 * Create an mpool, open it once O_RDONLY, then try to open it again
 * See that it fails.
 */

char mp_correctness_rdonly_open_mpool[MPOOL_NAMESZ_MAX];

static struct param_inst mp_correctness_rdonly_open_params[] = {
	PARAM_INST_STRING(mp_correctness_rdonly_open_mpool,
			  sizeof(mp_correctness_rdonly_open_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mp_correctness_rdonly_open_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mp.correctness.rdonly_open [options]\n");

	show_default_params(mp_correctness_rdonly_open_params, 0);
}

mpool_err_t mp_correctness_rdonly_open(int argc, char **argv)
{
	mpool_err_t err = 0, d_err;
	char  *mpool;
	int    next_arg = 0;
	char  *test = argv[0];
	char   errbuf[ERROR_BUFFER_SIZE];

	struct mpool       *mp[2];

	show_args(argc, argv);
	err = process_params(argc, argv, mp_correctness_rdonly_open_params, &next_arg, 0);
	if (err != 0) {
		printf("%s.%d: process_params returned an error\n", __func__, __LINE__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mp_correctness_rdonly_open_mpool;

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d:: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* Open mpool in O_RDONLY mode */
	err = mpool_open(mpool, O_RDONLY, &mp[0], NULL);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Exclusive open of mpool failed: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	/* Now try to open it again, should fail. */
	err = mpool_open(mpool, O_EXCL | O_RDONLY, &mp[1], NULL);
	if (err) {
		err = 0;
	} else {
		err = merr(EBUG);
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Multi-open of a mp must fail: %s\n",
				__func__, __LINE__, errbuf);
		fprintf(stderr, "\tTEST FAILURE: %s\n", test);
	}

	d_err = mpool_close(mp[0]);
	if (d_err) {
		mpool_strinfo(d_err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Close of mpool failed: %s\n",
			__func__, __LINE__, errbuf);
	}

	return err;
}

/**
 * Create an mpool, open it once O_RDWR, then try to open it again RDWR or RDONLY
 * See that it fails on both.
 */

char mp_correctness_rdwr_open_mpool[MPOOL_NAMESZ_MAX];

static struct param_inst mp_correctness_rdwr_open_params[] = {
	PARAM_INST_STRING(mp_correctness_rdwr_open_mpool,
			  sizeof(mp_correctness_rdwr_open_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mp_correctness_rdwr_open_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mp.correctness.rdwr_open [options]\n");

	show_default_params(mp_correctness_rdwr_open_params, 0);
}

mpool_err_t mp_correctness_rdwr_open(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	char  *test = argv[0];
	int    next_arg = 0;
	char   errbuf[ERROR_BUFFER_SIZE];

	struct mpool       *mp[2];

	show_args(argc, argv);
	err = process_params(argc, argv, mp_correctness_rdwr_open_params, &next_arg, 0);
	if (err != 0) {
		fprintf(stderr, "%s.%d: process_params returned an error\n",
			__func__, __LINE__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mp_correctness_rdwr_open_mpool;

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* Open it O_RDWR */
	err = mpool_open(mpool, O_RDWR, &mp[0], NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: RDWR open of mpool failed: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	/* Now try to open it O_RDONLY, should fail. */
	err = mpool_open(mpool, O_EXCL | O_RDONLY, &mp[1], NULL);
	if (!err) {
		original_err = err = merr(EBUG);
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Multi-open of a mp must fail: %s\n",
			__func__, __LINE__, errbuf);
		fprintf(stderr, "\tTEST FAILURE: %s\n", test);
		goto close_mp;
	}

	/* Now try to open it O_RDWR, should fail */
	err = mpool_open(mpool, O_EXCL | O_RDWR, &mp[1], NULL);
	if (!err) {
		original_err = err = merr(EBUG);
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Multi-open of a mp must fail: %s\n",
				__func__, __LINE__, errbuf);
		fprintf(stderr, "\tTEST FAILURE: %s\n", test);
		goto close_mp;
	}

close_mp:
	err = mpool_close(mp[0]);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Close of mpool failed: %s\n",
			__func__, __LINE__, errbuf);
	}

	return original_err;
}

struct test_s mp_tests[] = {
	{ "simple",  MPFT_TEST_TYPE_CORRECTNESS, mp_correctness_simple,
		mp_correctness_simple_help },
	{ "rdonly_open",  MPFT_TEST_TYPE_CORRECTNESS, mp_correctness_rdonly_open,
		mp_correctness_rdonly_open_help },
	{ "rdwr_open",  MPFT_TEST_TYPE_CORRECTNESS, mp_correctness_rdwr_open,
		mp_correctness_rdwr_open_help },
	{ NULL,  MPFT_TEST_TYPE_INVALID, NULL, NULL },
};

void mp_help(void)
{
	fprintf(co.co_fp, "\nmp tests validate the behavior of mpools\n");
}

struct group_s mpft_mp = {
	.group_name = "mp",
	.group_test = mp_tests,
	.group_help = mp_help,
};
