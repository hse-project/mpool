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
#include "mpft_mdc.h"

#define merr(_errnum)   (_errnum)

#define EBUG            (666)

#define BUF_SIZE        128
#define BUF_CNT         12

u8 opflags = 0; /* flags to be used for mpool_mdc_open() */

static void show_args(int argc, char **argv)
{
	int i;

	if (!co.co_verbose)
		return;

	for (i = 0; i < argc; i++)
		fprintf(stdout, "\t[%d] %s\n", i, argv[i]);
}


#define ERROR_BUFFER_SIZE 256
#define BUFFER_SIZE 64

/**
 *
 * Simple
 *
 */

/**
 * The simple test is meant to only test the basics of creating,
 * opening, closing, and destroying MDCs.
 *
 * Steps:
 * 1. Create an mpool
 * 2. Open the mpool
 * 3. Create an MDC
 * 4. Open the MDC
 * 5. Close the MDC
 * 6. Cleanup
 */

char mdc_correctness_simple_mpool[MPOOL_NAMESZ_MAX];

static struct param_inst mdc_correctness_simple_params[] = {
	PARAM_INST_STRING(mdc_correctness_simple_mpool,
			  sizeof(mdc_correctness_simple_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mdc_correctness_simple_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mdc.correctness.simple [options]\n");

	show_default_params(mdc_correctness_simple_params, 0);
}

mpool_err_t mdc_correctness_simple(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	int    next_arg = 0;
	char   errbuf[ERROR_BUFFER_SIZE];
	u64    oid[2];

	struct mpool       *mp;
	struct mpool_mdc   *mdc;

	struct mdc_capacity  capreq;
	enum mp_media_classp mclassp;

	show_args(argc, argv);
	err = process_params(argc, argv, mdc_correctness_simple_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mdc_correctness_simple_mpool;

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* 2. Open the mpool */
	err = mpool_open(mpool, O_RDWR, &mp, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the mpool: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	mclassp = MP_MED_CAPACITY;

	capreq.mdt_captgt = 1024 * 1024;   /* 1M, arbitrary choice */

	/* 3. Create an MDC */
	err = mpool_mdc_alloc(mp, &oid[0], &oid[1], mclassp, &capreq, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_abort(mp, oid[0], oid[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to abort MDC : %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_alloc(mp, &oid[0], &oid[1], mclassp, &capreq, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_commit(mp, oid[0], oid[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	/* 4. Open the MDC */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 5. Close the MDC */
	err = mpool_mdc_close(mdc);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* Test MDC destroy with two committed mlogs */
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to destroy MDC: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	/* Test MDC destroy with two non-existent mlogs */
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (!err && mpool_errno(err) != ENOENT) {
		original_err = (err ? : merr(EBUG));
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: MDC destroy must fail with ENOENT: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_alloc(mp, &oid[0], &oid[1], mclassp, &capreq, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mlog_abort(mp, oid[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to abort mlog : %s\n", __func__, __LINE__, errbuf);
		mpool_mlog_abort(mp, oid[1]);
		goto destroy_mdc;
	}

	/* Test MDC destroy with one non-existent and one un-committed mlog */
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (!err || mpool_errno(err) != ENOENT) {
		original_err = (err ? : merr(EBUG));
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: MDC destroy must fail with ENOENT: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_alloc(mp, &oid[0], &oid[1], mclassp, &capreq, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_commit(mp, oid[0], oid[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mlog_delete(mp, oid[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to delete mlog : %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* Test MDC destroy with one non-existent and one committed mlog */
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (!err || mpool_errno(err) != ENOENT) {
		original_err = (err ? : merr(EBUG));
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: MDC destroy must fail with ENOENT: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp;
	}

	goto close_mp;

	/* 6. Cleanup */
destroy_mdc:
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to destroy MDC: %s\n", __func__, __LINE__, errbuf);
	}

close_mp:
	err = mpool_close(mp);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mpool: %s\n", __func__, __LINE__, errbuf);
	}

	return original_err;
}

/**
 *
 * Mpool Release
 *
 */

/**
 * 1. Create an mpool
 * 2. Open the mpool
 * 3. Create an MDC
 * 4. Open the MDC
 * 5. Close the MDC
 * 6. Close the mpool
 * 7. Open the mpool
 * 8. Open the MDC
 * 9. Close the MDC
 * 10. Cleanup
 */

char mdc_correctness_mp_release_mpool[MPOOL_NAMESZ_MAX];

static struct param_inst mdc_correctness_mp_release_params[] = {
	PARAM_INST_STRING(mdc_correctness_mp_release_mpool,
			  sizeof(mdc_correctness_mp_release_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mdc_correctness_mp_release_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mdc.correctness.mp_release [options]\n");

	show_default_params(mdc_correctness_mp_release_params, 0);
}

mpool_err_t mdc_correctness_mp_release(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	int    next_arg = 0;
	char   errbuf[ERROR_BUFFER_SIZE];
	u64    oid[2];

	struct mpool       *mp;
	struct mpool_mdc   *mdc[3];

	struct mdc_capacity  capreq;
	enum mp_media_classp mclassp;

	show_args(argc, argv);
	err = process_params(argc, argv, mdc_correctness_mp_release_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mdc_correctness_mp_release_mpool;

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* 2. Open the mpool */
	err = mpool_open(mpool, O_RDWR, &mp, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the mpool: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	mclassp = MP_MED_CAPACITY;

	capreq.mdt_captgt = 1024 * 1024;   /* 1M, arbitrary choice */

	/* 3. Create an MDC */
	err = mpool_mdc_alloc(mp, &oid[0], &oid[1], mclassp, &capreq, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_commit(mp, oid[0], oid[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	/* 4. Open the MDC */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 5. Close the MDC */
	err = mpool_mdc_close(mdc[0]);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 6. Close the mpool */
	err = mpool_close(mp);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mpool: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 7. Open the mpool */
	err = mpool_open(mpool, O_RDWR, &mp, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the mpool: %s\n",
			__func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 8. Open the MDC */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 9. Close the MDC */
	err = mpool_mdc_close(mdc[0]);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}


	/* 10. Cleanup */
	/* Destroy the MDC */
destroy_mdc:
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to destroy MDC: %s\n", __func__, __LINE__, errbuf);
	}

close_mp:
	err = mpool_close(mp);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mpool: %s\n", __func__, __LINE__, errbuf);
	}

	return original_err;
}

/**
 *
 * Multi Readers in the Same Application
 *
 */

/**
 * 1. Create a mpool
 * 2. Open the mpool RDWR
 * 3. Create an MDC
 * 4. Open MDC
 * 5. Write pattern to MDC
 * 6. Close MDC
 * 7. Open MDC (handle: mdc[0])
 * 8. Rewind mdc[0]
 * 9. Read/Verify pattern via mdc[0]
 * 10. Rewind mdc[0]
 * 11. Open the same MDC (handle: mdc[1]
 * 12. Rewind mdc[1]
 * 13. Read/Verify pattern via mdc[1]
 * 14. Cleanup
 */

char mdc_correctness_multi_reader_single_app_mpool[MPOOL_NAMESZ_MAX];

static struct param_inst mdc_correctness_multi_reader_single_app_params[] = {
	PARAM_INST_STRING(mdc_correctness_multi_reader_single_app_mpool,
			  sizeof(mdc_correctness_multi_reader_single_app_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mdc_correctness_multi_reader_single_app_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mdc.correctness.multi_reader_single_app [options]\n");

	show_default_params(mdc_correctness_multi_reader_single_app_params, 0);
}

int verify_buf(char *buf_in, size_t buf_len, char val)
{
	char    buf[buf_len];
	pid_t   pid = getpid();
	u8     *p, *p1;

	memset(buf, val, buf_len);

	if (memcmp(buf, buf_in, buf_len)) {
		p = (u8 *)buf;
		p1 = (u8 *)buf_in;
		fprintf(stdout, "[%d] expect %d got %d\n", pid, (int)*p, (int)*p1);
		return 1;
	}

	return 0;
}

mpool_err_t mdc_correctness_multi_reader_single_app(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	int    next_arg = 0, i, rc;
	char   errbuf[ERROR_BUFFER_SIZE];
	u64    oid[2];
	char   buf[BUF_SIZE], buf_in[BUF_SIZE];
	size_t read_len;

	struct mpool       *mp;
	struct mpool_mdc   *mdc[2];

	struct mdc_capacity  capreq;
	enum mp_media_classp mclassp;

	show_args(argc, argv);
	err = process_params(argc, argv, mdc_correctness_multi_reader_single_app_params,
			     &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mdc_correctness_multi_reader_single_app_mpool;

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* 2. Open the mpool RDWR */
	err = mpool_open(mpool, O_RDWR, &mp, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the mpool: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	mclassp = MP_MED_CAPACITY;

	capreq.mdt_captgt = 1024 * 1024;   /* 1M, arbitrary choice */

	/* 3. Create an MDC */
	err = mpool_mdc_alloc(mp, &oid[0], &oid[1], mclassp, &capreq, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_commit(mp, oid[0], oid[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	/* 4. Open MDC */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 5. Write pattern to MDC */
	for (i = 0; i < BUF_CNT; i++) {
		memset(buf, i, BUF_SIZE);

		err = mpool_mdc_append(mdc[0], buf, BUF_SIZE, true);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to append to MDC: %s\n",
				__func__, __LINE__, errbuf);
			mpool_mdc_close(mdc[0]);
			goto destroy_mdc;
		}
	}

	/* 6. Close MDC */
	err = mpool_mdc_close(mdc[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 7. Open MDC (handle: mdc[0]) */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 8. Rewind mdc[0] */
	err = mpool_mdc_rewind(mdc[0]);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to rewind to MDC: %s\n", __func__, __LINE__, errbuf);
		goto close_mdc0;
	}

	/* 9. Read/Verify pattern via mdc[0] */
	for (i = 0; i < BUF_CNT; i++) {
		memset(buf_in, ~i, sizeof(buf_in));

		err = mpool_mdc_read(mdc[0], buf_in, BUF_SIZE, &read_len);
		if (err) {
			original_err = err;
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to read from MDC: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mdc0;
		}

		if (BUF_SIZE != read_len) {
			original_err = merr(EINVAL);
			fprintf(stderr, "%s.%d: Requested size not read exp %d, got %d\n",
				__func__, __LINE__, (int)BUF_SIZE, (int)read_len);
			goto close_mdc0;
		}

		rc = verify_buf(buf_in, read_len, i);
		if (rc != 0) {
			original_err = merr(EIO);
			fprintf(stderr, "%s.%d: Verify mismatch buf[%d]\n", __func__, __LINE__, i);
			err = merr(EINVAL);
			goto close_mdc0;
		}
	}

	/* 10. Rewind mdc[0] */
	err = mpool_mdc_rewind(mdc[0]);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to rewind to MDC: %s\n", __func__, __LINE__, errbuf);
		goto close_mdc0;
	}

	/* 11. Open the same MDC (handle: mdc[1], like a reopen */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto close_mdc0;
	}

	/* 12. Rewind mdc[1] */
	err = mpool_mdc_rewind(mdc[1]);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to rewind to MDC: %s\n", __func__, __LINE__, errbuf);
		goto close_mdc1;
	}

	/* 13. Read/Verify pattern via mdc[1] */
	for (i = 0; i < BUF_CNT; i++) {
		memset(buf_in, ~i, sizeof(buf_in));

		err = mpool_mdc_read(mdc[1], buf_in, BUF_SIZE, &read_len);
		if (err) {
			original_err = err;
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to read from MDC: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mdc1;
		}

		if (BUF_SIZE != read_len) {
			original_err = merr(EINVAL);
			fprintf(stderr, "%s.%d: Requested size not read exp %d, got %d\n",
				__func__, __LINE__, (int)BUF_SIZE, (int)read_len);
			goto close_mdc1;
		}

		rc = verify_buf(buf_in, read_len, i);
		if (rc != 0) {
			original_err = merr(EIO);
			fprintf(stderr, "%s.%d: Verify mismatch buf[%d]\n", __func__, __LINE__, i);
			err = merr(EINVAL);
			goto close_mdc1;
		}
	}

	/* 14. Cleanup */
close_mdc1:
	mpool_mdc_close(mdc[1]);

close_mdc0:
	mpool_mdc_close(mdc[0]);

	/* Destroy the MDC */
destroy_mdc:
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to destroy MDC: %s\n", __func__, __LINE__, errbuf);
	}

close_mp:
	err = mpool_close(mp);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mpool: %s\n", __func__, __LINE__, errbuf);
	}

	return original_err;
}

/**
 *
 * Reader then Writer
 *
 */

/**
 * 1. Create a mpool
 * 2. Open the mpool RDWR
 * 3. Create an MDC
 * 4. Open MDC
 * 5. Write pattern to MDC
 * 6. Close MDC
 * 7. Open MDC
 * 8. Rewind mdc
 * 9. Read/Verify pattern via mdc
 * 10. Rewind mdc
 * 11. Cleanup
 */

char mdc_correctness_reader_then_writer_mpool[MPOOL_NAMESZ_MAX];

static struct param_inst mdc_correctness_reader_then_writer_params[] = {
	PARAM_INST_STRING(mdc_correctness_reader_then_writer_mpool,
			  sizeof(mdc_correctness_reader_then_writer_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mdc_correctness_reader_then_writer_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mdc.correctness.reader_then_writer [options]\n");

	show_default_params(mdc_correctness_reader_then_writer_params, 0);
}

mpool_err_t mdc_correctness_reader_then_writer(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	int    next_arg = 0, i, rc;
	char   errbuf[ERROR_BUFFER_SIZE];
	u64    oid[2];
	char   buf[BUF_SIZE], buf_in[BUF_SIZE];
	size_t read_len;

	struct mpool       *mp;
	struct mpool_mdc   *mdc;

	struct mdc_capacity  capreq;
	enum mp_media_classp mclassp;

	show_args(argc, argv);
	err = process_params(argc, argv, mdc_correctness_reader_then_writer_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mdc_correctness_reader_then_writer_mpool;

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* 2. Open the mpool RDWR */
	err = mpool_open(mpool, O_RDWR, &mp, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the mpool: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	mclassp = MP_MED_CAPACITY;

	capreq.mdt_captgt = 1024 * 1024;   /* 1M, arbitrary choice */

	/* 3. Create an MDC */
	err = mpool_mdc_alloc(mp, &oid[0], &oid[1], mclassp, &capreq, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_commit(mp, oid[0], oid[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}


	/* 4. Open MDC */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 5. Write pattern to MDC */
	for (i = 0; i < BUF_CNT; i++) {
		memset(buf, i, BUF_SIZE);

		err = mpool_mdc_append(mdc, buf, BUF_SIZE, true);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to append to MDC: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mdc;
		}
	}

	/* 6. Close MDC */
	err = mpool_mdc_close(mdc);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 7. Open MDC (handle: mdc) */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 8. Rewind mdc */
	err = mpool_mdc_rewind(mdc);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to rewind to MDC: %s\n", __func__, __LINE__, errbuf);
		goto close_mdc;
	}

	/* 9. Read/Verify pattern via mdc */
	for (i = 0; i < BUF_CNT; i++) {
		memset(buf_in, ~i, BUF_SIZE);

		err = mpool_mdc_read(mdc, buf_in, BUF_SIZE, &read_len);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to read from MDC: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mdc;
		}

		if (BUF_SIZE != read_len) {
			fprintf(stderr, "%s.%d: Requested size not read exp %d, got %d\n",
				__func__, __LINE__, (int)BUF_SIZE, (int)read_len);
			goto close_mdc;
		}

		rc = verify_buf(buf_in, read_len, i);
		if (rc != 0) {
			fprintf(stderr, "%s.%d: Verify mismatch buf[%d]\n", __func__, __LINE__, i);
			err = merr(EINVAL);
			goto close_mdc;
		}
	}

	/* 10. Rewind mdc */
	err = mpool_mdc_rewind(mdc);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to rewind to MDC: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mdc;
	}

	/* 11. Cleanup */
close_mdc:
	mpool_mdc_close(mdc);

	/* Destroy the MDC */
destroy_mdc:
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to destroy MDC: %s\n", __func__, __LINE__, errbuf);
	}

close_mp:
	err = mpool_close(mp);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mpool: %s\n", __func__, __LINE__, errbuf);
	}

	return original_err;
}

/**
 *
 * Writer then Reader
 *
 */

/**
 * 1. Create a mpool
 * 2. Open the mpool RDWR
 * 3. Create an MDC
 * 4. Open MDC
 * 5. Write pattern to MDC (handle: mdc[0])
 * 6. Close MDC (handle: mdc[0])
 * 7. Open MDC (handle: mdc[1]), should succeed
 * 8. Rewind mdc[1]
 * 9. Read/Verify pattern via mdc[1]
 * 10. Cleanup
 */

char mdc_correctness_writer_then_reader_mpool[MPOOL_NAMESZ_MAX];

static struct param_inst mdc_correctness_writer_then_reader_params[] = {
	PARAM_INST_STRING(mdc_correctness_writer_then_reader_mpool,
			  sizeof(mdc_correctness_writer_then_reader_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mdc_correctness_writer_then_reader_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mdc.correctness.writer_then_reader [options]\n");

	show_default_params(mdc_correctness_writer_then_reader_params, 0);
}

mpool_err_t mdc_correctness_writer_then_reader(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	int    next_arg = 0, i, rc;
	char   errbuf[ERROR_BUFFER_SIZE];
	u64    oid[2];
	char   buf[BUF_SIZE], buf_in[BUF_SIZE];
	size_t read_len;

	struct mpool       *mp;
	struct mpool_mdc   *mdc[2];

	struct mdc_capacity  capreq;
	enum mp_media_classp mclassp;

	show_args(argc, argv);
	err = process_params(argc, argv, mdc_correctness_writer_then_reader_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mdc_correctness_writer_then_reader_mpool;

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* 2. Open the mpool RDWR */
	err = mpool_open(mpool, O_RDWR, &mp, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the mpool: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	mclassp = MP_MED_CAPACITY;

	capreq.mdt_captgt = 1024 * 1024;   /* 1M, arbitrary choice */

	/* 3. Create an MDC */
	err = mpool_mdc_alloc(mp, &oid[0], &oid[1], mclassp, &capreq, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	err = mpool_mdc_commit(mp, oid[0], oid[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mdc: %s\n", __func__, __LINE__, errbuf);
		goto close_mp;
	}

	/* 4. Open MDC */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 5. Write pattern to MDC (handle: mdc[0]) */
	for (i = 0; i < BUF_CNT; i++) {
		memset(buf, i, BUF_SIZE);

		err = mpool_mdc_append(mdc[0], buf, BUF_SIZE, true);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to append to MDC: %s\n",
				__func__, __LINE__, errbuf);
			mpool_mdc_close(mdc[0]);
			goto destroy_mdc;
		}
	}

	/* 6. Close MDC (handle: mdc[0]) */
	err = mpool_mdc_close(mdc[0]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}
	mdc[0] = NULL;

	/* 7. Open MDC (handle: mdc[1]), should succeed */
	err = mpool_mdc_open(mp, oid[0], oid[1], opflags, &mdc[1]);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open MDC: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mdc;
	}

	/* 8. Rewind mdc[1] */
	err = mpool_mdc_rewind(mdc[1]);
	if (err) {
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to rewind to MDC: %s\n", __func__, __LINE__, errbuf);
		goto close_mdc1;
	}

	/* 9. Read/Verify pattern via mdc[1] */
	for (i = 0; i < BUF_CNT; i++) {
		memset(buf_in, ~i, BUF_SIZE);

		err = mpool_mdc_read(mdc[1], buf_in, BUF_SIZE, &read_len);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to read from MDC: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mdc1;
		}

		if (BUF_SIZE != read_len) {
			fprintf(stderr, "%s.%d: Requested size not read exp %d, got %d\n",
				__func__, __LINE__, (int)BUF_SIZE, (int)read_len);
			goto close_mdc1;
		}

		rc = verify_buf(buf_in, read_len, i);
		if (rc != 0) {
			fprintf(stderr, "%s.%d: Verify mismatch buf[%d]\n", __func__, __LINE__, i);
			original_err = merr(EINVAL);
			goto close_mdc1;
		}
	}

	/* 10. Cleanup */
close_mdc1:
	mpool_mdc_close(mdc[1]);

	/* Destroy the MDC */
destroy_mdc:
	err = mpool_mdc_delete(mp, oid[0], oid[1]);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to destroy MDC: %s\n", __func__, __LINE__, errbuf);
	}

close_mp:
	err = mpool_close(mp);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mpool: %s\n", __func__, __LINE__, errbuf);
	}

	return original_err;
}

/**
 *
 * Multi MDC Single App
 *
 */

/**
 * 1. Create a mpool
 * 2. Open the mpool RDWR
 * 3. Create 4 MDCs
 * 4. Open all 4 MDCs in client serialization mode
 * 5. Write different patterns to each MDC
 * 6. Close all MDCs
 * 7. Open all 4 MDCs (handles: mdc[0..3])
 * 8. Rewind MDCs
 * 9. Read/Verify patterns on all MDCs
 * 10. Cleanup
 */

char mdc_correctness_multi_mdc_mpool[MPOOL_NAMESZ_MAX];
u32  mdc_correctness_multi_mdc_mdc_cnt = 4;
struct oid_s {
	u64 oid[2];
};

static struct param_inst mdc_correctness_multi_mdc_params[] = {
	PARAM_INST_STRING(mdc_correctness_multi_mdc_mpool,
			  sizeof(mdc_correctness_multi_mdc_mpool), "mp", "mpool"),
	PARAM_INST_U32(mdc_correctness_multi_mdc_mdc_cnt, "mdc_cnt", "Number of MDCs"),
	PARAM_INST_END
};

static void mdc_correctness_multi_mdc_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mdc.correctness.multi_mdc [options]\n");

	show_default_params(mdc_correctness_multi_mdc_params, 0);
}

mpool_err_t mdc_correctness_multi_mdc(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	int    next_arg = 0, i, j, rc;
	char   errbuf[ERROR_BUFFER_SIZE];
	char   buf[BUF_SIZE], buf_in[BUF_SIZE];
	u32    mdc_cnt;
	size_t read_len;

	struct oid_s       *oid;
	struct mpool       *mp;
	struct mpool_mdc      *mdc[4];

	struct mdc_capacity  capreq;
	enum mp_media_classp mclassp;

	show_args(argc, argv);
	err = process_params(argc, argv, mdc_correctness_multi_mdc_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mdc_correctness_multi_mdc_mpool;
	mdc_cnt = mdc_correctness_multi_mdc_mdc_cnt;

	if (mdc_cnt >= 16) {
		fprintf(stderr, "%s.%d: mdc_cnt exceeds maximum (15)\n", __func__, __LINE__);
		return merr(EINVAL);
	}

	if (mpool[0] == 0) {
		fprintf(stderr,
			"%s.%d: mpool (mp=<mpool>) must be specified\n", __func__, __LINE__);
		return merr(EINVAL);
	}

	oid = calloc(mdc_cnt, sizeof(*oid));
	if (!oid) {
		perror("oid calloc");
		return merr(ENOMEM);
	}

	/* 2. Open the mpool RDWR */
	err = mpool_open(mpool, O_RDWR, &mp, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the mpool: %s\n",
			__func__, __LINE__, errbuf);
		goto close_mp1;
	}

	mclassp = MP_MED_CAPACITY;

	capreq.mdt_captgt = 1024 * 1024;   /* 1M, arbitrary choice */

	/* 3. Create <mdc_cnt> MDCs */
	for (i = 0; i < mdc_cnt; i++) {
		err = mpool_mdc_alloc(mp, &oid[i].oid[0], &oid[i].oid[1], mclassp, &capreq, NULL);
		if (err) {
			original_err = err;
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to alloc mdc: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mp;
		}

		err = mpool_mdc_commit(mp, oid[i].oid[0], oid[i].oid[1]);
		if (err) {
			original_err = err;
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to commit mdc: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mp;
		}
	}

	/* 4. Open all <mdc_cnt> MDCs */
	for (i = 0; i < mdc_cnt; i++) {
		err = mpool_mdc_open(mp, oid[i].oid[0], oid[i].oid[1], MDC_OF_SKIP_SER, &mdc[i]);
		if (err) {
			original_err = err;
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to open MDC %d: %s\n",
				__func__, __LINE__, i, errbuf);
			goto destroy_mdcs;
		}
	}

	/* 5. Write different patterns to each MDC */
	for (i = 0; i < mdc_cnt; i++) {
		int v;

		for (j = 0; j < BUF_CNT; j++) {

			v = (i << 4) | (j & 0xf);

			memset(buf, v, BUF_SIZE);

			err = mpool_mdc_append(mdc[i], buf, BUF_SIZE, true);
			if (err) {
				mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
				fprintf(stderr, "%s.%d: Unable to append to MDC: %s\n",
					__func__, __LINE__, errbuf);
				goto close_mdcs;
			}
		}
	}

	/* 6. Close all MDCs */
	for (i = 0; i < mdc_cnt; i++) {
		err = mpool_mdc_close(mdc[i]);
		if (err) {
			original_err = err;
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to close MDC: %s\n",
				__func__, __LINE__, errbuf);
			goto destroy_mdcs;
		}

		mdc[i] = NULL;
	}

	/* 7. Open all MDCs (handles: mdc[0..<mdc_cnt>]) */
	for (i = 0; i < mdc_cnt; i++) {
		err = mpool_mdc_open(mp, oid[i].oid[0], oid[i].oid[1], opflags, &mdc[i]);
		if (err) {
			original_err = err;
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to open MDC: %s\n",
				__func__, __LINE__, errbuf);
			goto destroy_mdcs;
		}
	}

	/* 8. Rewind MDCs */
	for (i = 0; i < mdc_cnt; i++) {
		err = mpool_mdc_rewind(mdc[i]);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to rewind to MDC: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mdcs;
		}
	}

	/* 9. Read/Verify patterns on all MDCs */
	for (j = 0; j < BUF_CNT; j++) {
		for (i = 0; i < mdc_cnt; i++) {
			int v;

			memset(buf_in, ~i, BUF_SIZE);

			err = mpool_mdc_read(mdc[i], buf_in, BUF_SIZE, &read_len);
			if (err) {
				mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
				fprintf(stderr, "%s.%d: Unable to read from MDC: %s\n",
					__func__, __LINE__, errbuf);
				goto close_mdcs;
			}

			if (BUF_SIZE != read_len) {
				fprintf(stderr, "%s.%d: Requested size not read exp %d, got %d\n",
					__func__, __LINE__, (int)BUF_SIZE, (int)read_len);
				goto close_mdcs;
			}

			v = (i << 4) | (j & 0xf);
			rc = verify_buf(buf_in, read_len, v);
			if (rc != 0) {
				fprintf(stderr, "%s.%d: Verify mismatch buf[%d]\n",
					__func__, __LINE__, i);
				fprintf(stderr, "\tmdc %d, buf %d\n", i, j);
				original_err = merr(EINVAL);
				goto close_mdcs;
			}
		}
	}

	/* 10. Cleanup */
close_mdcs:
	for (i = 0; i < mdc_cnt; i++)
		mpool_mdc_close(mdc[i]);

	/* Destroy the MDCs */
destroy_mdcs:
	for (i = 0; i < mdc_cnt; i++) {
		err = mpool_mdc_delete(mp, oid[i].oid[0], oid[i].oid[1]);
		if (err) {
			if (!original_err)
				original_err = err;
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to destroy MDC: %s\n",
				__func__, __LINE__, errbuf);
		}
	}

close_mp:
	err = mpool_close(mp);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mpool: %s\n", __func__, __LINE__, errbuf);
	}

close_mp1:
	free(oid);

	return original_err;
}

struct test_s mdc_tests[] = {
	{ "simple",  MPFT_TEST_TYPE_CORRECTNESS, mdc_correctness_simple,
		mdc_correctness_simple_help },
	{ "mp_release",  MPFT_TEST_TYPE_CORRECTNESS, mdc_correctness_mp_release,
		mdc_correctness_mp_release_help },
	{ "multi_reader_single_app",  MPFT_TEST_TYPE_CORRECTNESS,
		mdc_correctness_multi_reader_single_app,
		mdc_correctness_multi_reader_single_app_help },
	{ "reader_then_writer",  MPFT_TEST_TYPE_CORRECTNESS, mdc_correctness_reader_then_writer,
		mdc_correctness_reader_then_writer_help },
	{ "writer_then_reader",  MPFT_TEST_TYPE_CORRECTNESS, mdc_correctness_writer_then_reader,
		mdc_correctness_writer_then_reader_help },
	{ "multi_mdc",  MPFT_TEST_TYPE_CORRECTNESS, mdc_correctness_multi_mdc,
		mdc_correctness_multi_mdc_help },
	{ NULL,  MPFT_TEST_TYPE_INVALID, NULL, NULL },
};

void mdc_help(void)
{
	int i = 0;

	fprintf(co.co_fp, "\nmp tests validate the behavior of mpool\n");

	fprintf(co.co_fp, "Available tests include:\n");
	while (mdc_tests[i].test_name) {
		if (mdc_tests[i].test_type != MPFT_TEST_TYPE_ACTOR)
			fprintf(co.co_fp, "\t%s\n", mdc_tests[i].test_name);
		i++;
	}
}

struct group_s mpft_mdc = {
	.group_name = "mdc",
	.group_test = mdc_tests,
	.group_help = mdc_help,
};
