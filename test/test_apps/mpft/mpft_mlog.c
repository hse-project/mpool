// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/**
 * This file implements tests that are to be run in the mpft (MPool
 * Functional Test) framework.
 *
 * Available tests:
 * * perf_seq_writes - test performance of mlog writes
 *   - required parameters:
 *     - mpool (mp)
 *     - dataset (ds)
 *   - options:
 *     - record size (rs), default: 32B
 *     - total size (ts), default: all available space in mpool
 *     - thread count (threads), default: 1
 *     - sync, default: false
 *     - verify, default: false
 *     - pattern, default: 0x0123456789abcdef
 *
 *     Description: In the specified mpool and dataset, create an MDC
 *       and then write records of size <rs> until <ts> bytes have been
 *       written.
 *
 *       If total size (ts) is not given on the command line, the amount
 *       of space available in the mpool is determined and used for total space.
 *
 *       The number of writes is determined by dividing total space by the
 *       record size. The writes will be evenly distributed across the
 *       specified number of threads.
 *
 *       If verify is set to true, then, after the performance measurement,
 *       the data written to the mlog will be read back and compared to the
 *       expected pattern. A custom pattern can be specified by the
 *       <pattern> parameter:
 *
 *       e.g: #./mpft mlog.perf.seq_writes mp=mp1 ds=ds1 ts=1M
 *                  rs=1K pattern=0123456789abcdef verify=true
 *
 *       This command line will result in 1024 records of 1024B being
 *       written. The writes will have the pattern '0x0123456789abcdef'
 *       and the contents will be verified at the end of the run.
 *
 * * perf_seq_reads
 *   - parameters and options are the same as for perf_seq_writes
 *
 *     Description: perf_seq_reads follows the same steps as perf_seq_writes,
 *       but adds a loop reading back all of the records.
 */

#include <stdio.h>
#include <pthread.h>

#include <util/platform.h>
#include <util/parse_num.h>
#include <util/param.h>
#include <mpool/mpool.h>

#include "mpft.h"
#include "mpft_thread.h"

#define EBUG            (666)

#define merr(_errnum)   (_errnum)

struct oid_pair {
	u64 oid[2];
};

#define MIN_SECTOR_SIZE 512
#define SECTOR_OVERHEAD 26
#define RECORD_OVERHEAD 7
#define LOG_OVERHEAD 2
#define USABLE_SECT_SIZE (MIN_SECTOR_SIZE - SECTOR_OVERHEAD - RECORD_OVERHEAD)

#define MAX_PATTERN_SIZE 256

static enum mp_media_classp mclassp_str2enum(char *mclassp_str)
{
	if (!strcmp(mclassp_str, "STAGING"))
		return MP_MED_STAGING;
	else if (!strcmp(mclassp_str, "CAPACITY"))
		return MP_MED_CAPACITY;

	return MP_MED_INVALID;
}

static u32 calc_record_count(u64 total_size, u32 record_size)
{
	u32 sector_cnt = total_size / MIN_SECTOR_SIZE;
	u32 sector_overhead = sector_cnt * SECTOR_OVERHEAD;
	u32 real_record_size;
	u32 record_cnt;
	u32 record_overhead;

	if (record_size < USABLE_SECT_SIZE)
		/* worst case a record can span two sectors */
		record_overhead = 2 * RECORD_OVERHEAD;
	else if (record_size > USABLE_SECT_SIZE)
		/* 2 here implies 1 leading + 1 trailing record desc. */
		record_overhead = ((record_size / USABLE_SECT_SIZE) + 2) * RECORD_OVERHEAD;
	else
		record_overhead = RECORD_OVERHEAD;

	real_record_size = record_size + record_overhead;
	record_cnt = (total_size - sector_overhead - LOG_OVERHEAD) / real_record_size;

	return record_cnt;
}

static size_t perf_seq_writes_record_size = 32;    /* Bytes */
static size_t perf_seq_writes_total_size;         /* Bytes, 0 = all available */
static size_t perf_seq_writes_thread_cnt = 1;
static char   perf_seq_writes_mpool[MPOOL_NAME_LEN_MAX];
static char   perf_seq_writes_dataset[MPOOL_NAME_LEN_MAX];
static bool   perf_seq_writes_sync;
static bool   perf_seq_writes_read;
static bool   perf_seq_writes_verify;
static bool   perf_seq_writes_skipser;
static char   perf_seq_writes_pattern[MAX_PATTERN_SIZE];
static unsigned int mlog_mclassp = MP_MED_CAPACITY;
static char   mlog_mclassp_str[MPOOL_NAME_LEN_MAX] = "CAPACITY";

static struct param_inst perf_seq_writes_params[] = {
	PARAM_INST_STRING(mlog_mclassp_str, sizeof(mlog_mclassp_str), "mc", "media class"),
	PARAM_INST_U32_SIZE(perf_seq_writes_record_size, "rs", "record size"),
	PARAM_INST_U32_SIZE(perf_seq_writes_total_size, "ts", "total size"),
	PARAM_INST_U32(perf_seq_writes_thread_cnt, "threads", "number of threads"),
	PARAM_INST_STRING(perf_seq_writes_mpool, sizeof(perf_seq_writes_mpool), "mp", "mpool"),
	PARAM_INST_STRING(perf_seq_writes_dataset,
			  sizeof(perf_seq_writes_dataset), "ds", "dataset"),
	PARAM_INST_BOOL(perf_seq_writes_sync, "sync", "all sync writes"),
	PARAM_INST_BOOL(perf_seq_writes_verify, "verify", "verify writes"),
	PARAM_INST_BOOL(perf_seq_writes_skipser, "skipser",
			"Client guarantees serialization, skip it"),
	PARAM_INST_STRING(perf_seq_writes_pattern,
			  sizeof(perf_seq_writes_pattern), "pattern", "pattern to write"),
	PARAM_INST_END
};

static void perf_seq_writes_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mlog.perf.seq_writes [options]\n");
	fprintf(co.co_fp, "e.g.: mpft mlog.perf.seq_writes rs=16\n");
	fprintf(co.co_fp, "\nmlog.perf.seq_writes will measure the performance "
		"in MB/s of writes of a given size (rs) to an mlog\n");

	show_default_params(perf_seq_writes_params, 0);
}

struct ml_writer_args {
	struct mpool      *ds;
	u32                rs;  /* write size in bytes */
	u32                wc;  /* write count */
	struct oid_pair    oid;
};

struct ml_writer_resp {
	mpool_err_t err;
	u32    usec;
	u64    bytes_written;
};

static void *ml_writer(void *arg)
{
	mpool_err_t err;
	int    i;
	char  *buf;
	u32    usec;
	char   err_str[256];
	long   written = 0;
	u8     flags = 0;

	struct mpft_thread_args *targs = (struct mpft_thread_args *)arg;
	struct ml_writer_args  *args = (struct ml_writer_args *)targs->arg;
	struct ml_writer_resp  *resp;
	struct mpool_mdc       *mdc;
	struct timeval          start_tv, stop_tv;
	int                     id = targs->instance;
	size_t                  used;
	u64                     oid1 = args->oid.oid[0];
	u64                     oid2 = args->oid.oid[1];
	u32                     write_cnt = args->wc;
	u32                     write_sz = args->rs;

	resp = calloc(1, sizeof(*resp));
	if (!resp) {
		err = merr(ENOMEM);
		fprintf(stderr, "[%d]%s: Unable to allocate response struct:%s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	if (perf_seq_writes_skipser)
		flags |= MDC_OF_SKIP_SER;

	err = mpool_mdc_open(args->ds, oid1, oid2, flags, &mdc);
	if (err) {
		fprintf(stderr, "[%d]%s: Unable to open mdc: %s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		resp->err = err;
		return resp;
	}

	if (co.co_verbose) {
		err = mpool_mdc_usage(mdc, &used);
		if (err) {
			fprintf(stderr, "[%d]%s: Unable to get mdc usage: %s\n", id,
				__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
			resp->err = err;
			goto close_mdc;
		}
		fprintf(stdout, "[%d] starting usage %ld\n", id, used);
	}

	buf = calloc(1, write_sz);
	if (!buf) {
		err = resp->err = merr(ENOMEM);
		fprintf(stderr, "[%d]%s: Unable to allocate buf: %s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		goto close_mdc;
	}
	pattern_fill(buf, write_sz);

	mpft_thread_wait_for_start(targs);

	/* start timer */
	gettimeofday(&start_tv, NULL);

	for (i = 0; i < write_cnt - 1; i++) {
		err = mpool_mdc_append(mdc, buf, write_sz, perf_seq_writes_sync);
		if (err) {
			fprintf(stderr, "[%d]%s: error on async append #%d bytes "
				"written %ld: %s\n", id, __func__, i, written,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			resp->err = err;
			goto free_buf;
		}
		written += write_sz;
	}

	err = mpool_mdc_append(mdc, buf, write_sz, true); /*  sync */
	if (err) {
		fprintf(stderr, "[%d]%s: error on append: %s\n", id, __func__,
			mpool_strinfo(err, err_str, sizeof(err_str)));
		resp->err = err;
		goto free_buf;
	}

	err = mpool_mdc_usage(mdc, &used);
	if (err) {
		fprintf(stderr, "[%d]%s: Unable to get mdc usage: %s\n", id, __func__,
			mpool_strinfo(err, err_str, sizeof(err_str)));
		resp->err = err;
		goto free_buf;
	}
	if (co.co_verbose)
		fprintf(stdout, "[%d] final usage %ld\n", id, used);

	/* end timer */
	gettimeofday(&stop_tv, NULL);

	if (stop_tv.tv_usec < start_tv.tv_usec) {
		stop_tv.tv_sec--;
		stop_tv.tv_usec += 1000000;
	}
	usec = (stop_tv.tv_sec - start_tv.tv_sec) * 1000000 + (stop_tv.tv_usec - start_tv.tv_usec);

	resp->usec = usec;
	resp->bytes_written = used;

free_buf:
	free(buf);
close_mdc:
	(void)mpool_mdc_close(mdc);
	return resp;
}

struct ml_reader_args {
	struct mpool      *ds;
	u32                rs;  /* read size in bytes */
	u32                rc;  /* read count */
	struct oid_pair    oid;
};

struct ml_reader_resp {
	mpool_err_t err;
	u32    usec;
	u64    read;
};

static void *ml_reader(void *arg)
{
	mpool_err_t err;
	int    i;
	char  *buf;
	u32    usec;
	char   err_str[256];
	size_t bytes_read = 0;
	u8     flags = 0;

	struct mpft_thread_args *targs = (struct mpft_thread_args *)arg;
	struct ml_reader_args  *args = (struct ml_reader_args *)targs->arg;
	struct ml_reader_resp  *resp;
	struct mpool_mdc       *mdc;
	struct timeval          start_tv, stop_tv;
	int                     id = targs->instance;
	size_t                  used;
	u64                     oid1 = args->oid.oid[0];
	u64                     oid2 = args->oid.oid[1];
	u32                     read_cnt = args->rc;

	resp = calloc(1, sizeof(*resp));
	if (!resp) {
		err = merr(ENOMEM);
		fprintf(stderr, "[%d]%s: Unable to allocate response struct:%s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	if (perf_seq_writes_skipser)
		flags |= MDC_OF_SKIP_SER;

	err = mpool_mdc_open(args->ds, oid1, oid2, flags, &mdc);
	if (err) {
		fprintf(stderr, "[%d]%s: Unable to open mdc: %s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		resp->err = err;
		return resp;
	}

	err = mpool_mdc_rewind(mdc);
	if (err) {
		fprintf(stderr, "[%d]%s: Unable to rewind\n", id, __func__);
		resp->err = err;
		return resp;
	}

	err = mpool_mdc_usage(mdc, &used);
	if (err) {
		fprintf(stderr, "[%d]%s: Unable to get mdc usage: %s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		resp->err = err;
		return resp;
	}
	if (co.co_verbose)
		fprintf(stdout, "[%d] starting usage %ld\n", id, used);

	buf = calloc(1, args->rs);
	if (!buf) {
		err = resp->err = merr(ENOMEM);
		fprintf(stderr, "[%d]%s: Unable to allocate buf: %s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	mpft_thread_wait_for_start(targs);

	/* start timer */
	gettimeofday(&start_tv, NULL);

	for (i = 0; i < read_cnt; i++) {
		err = mpool_mdc_read(mdc, buf, args->rs, &bytes_read);
		if (err) {
			fprintf(stderr, "[%d]%s: error on read:%s\n", i, __func__,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			resp->err = err;
			return resp;
		}
	}

	/* end timer */
	gettimeofday(&stop_tv, NULL);

	if (stop_tv.tv_usec < start_tv.tv_usec) {
		stop_tv.tv_sec--;
		stop_tv.tv_usec += 1000000;
	}
	usec = (stop_tv.tv_sec - start_tv.tv_sec) * 1000000 + (stop_tv.tv_usec - start_tv.tv_usec);

	resp->usec = usec;
	resp->read = used;

	mpool_mdc_close(mdc);
	free(buf);

	return resp;
}

struct ml_verify_args {
	struct mpool      *ds;
	u32                rs;  /* read size in bytes */
	u32                rc;  /* read count */
	struct oid_pair    oid;
};

struct ml_verify_resp {
	mpool_err_t err;
	u32    usec;
	u64    verified;
};

static void *ml_verify(void *arg)
{
	mpool_err_t err;
	int    i;
	char  *buf;
	u32    usec;
	char   err_str[256];
	size_t bytes_read = 0;
	int    ret;
	u8     flags = 0;

	struct mpft_thread_args *targs = (struct mpft_thread_args *)arg;
	struct ml_verify_args *args = (struct ml_verify_args *)targs->arg;
	struct ml_verify_resp  *resp;
	struct mpool_mdc       *mdc;
	struct timeval          start_tv, stop_tv;
	int                     id = targs->instance;
	size_t                  used;
	u64                     oid1 = args->oid.oid[0];
	u64                     oid2 = args->oid.oid[1];
	u32                     read_cnt = args->rc;

	resp = calloc(1, sizeof(*resp));
	if (!resp) {
		err = merr(ENOMEM);
		fprintf(stderr, "[%d]%s: Unable to allocate response struct:%s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	if (perf_seq_writes_skipser)
		flags |= MDC_OF_SKIP_SER;

	err = mpool_mdc_open(args->ds, oid1, oid2, flags, &mdc);
	if (err) {
		fprintf(stderr, "[%d]%s: Unable to open mdc: %s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		resp->err = err;
		return resp;
	}

	err = mpool_mdc_rewind(mdc);
	if (err) {
		fprintf(stderr, "[%d]%s: Unable to rewind\n", id, __func__);
		resp->err = err;
		return resp;
	}

	err = mpool_mdc_usage(mdc, &used);
	if (err) {
		fprintf(stderr, "[%d]%s: Unable to get mdc usage: %s\n", id, __func__,
			mpool_strinfo(err, err_str, sizeof(err_str)));
		resp->err = err;
		return resp;
	}
	if (co.co_verbose)
		fprintf(stdout, "[%d] starting usage %ld\n", id, used);

	buf = calloc(1, args->rs);
	if (!buf) {
		err = resp->err = merr(ENOMEM);
		fprintf(stderr, "[%d]%s: Unable to allocate buf: %s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	mpft_thread_wait_for_start(targs);

	/* start timer */
	gettimeofday(&start_tv, NULL);

	for (i = 0; i < read_cnt; i++) {
		err = mpool_mdc_read(mdc, buf, args->rs, &bytes_read);
		if (err) {
			fprintf(stderr, "[%d]%s: error on read:%s\n", i, __func__,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			resp->err = err;
			return resp;
		}
		ret = pattern_compare(buf, args->rs);
		if (ret) {
			fprintf(stderr, "[%d]%s: miscompare!\n", i, __func__);
			resp->err = merr(EIO);
			return resp;
		}
	}

	/* end timer */
	gettimeofday(&stop_tv, NULL);

	if (stop_tv.tv_usec < start_tv.tv_usec) {
		stop_tv.tv_sec--;
		stop_tv.tv_usec += 1000000;
	}
	usec = (stop_tv.tv_sec - start_tv.tv_sec) * 1000000 + (stop_tv.tv_usec - start_tv.tv_usec);

	resp->usec = usec;
	resp->verified = used;

	mpool_mdc_close(mdc);
	free(buf);

	return resp;
}

static mpool_err_t perf_seq_writes(int argc, char **argv)
{
	mpool_err_t err = 0;
	int    next_arg = 0;
	char  *mp;
	char  *ds;
	u32    tc;
	int    i;
	int    err_cnt;
	char   err_str[256];
	u32    usec;
	u32    write_cnt;
	u64    per_thread_size;
	u64    bytes_written;
	u64    bytes_read;
	u64    bytes_verified;
	double perf;
	char  *test_name = argv[0];
	int    ret;

	struct ml_writer_resp    *wr_resp;
	struct ml_writer_args    *wr_arg;
	struct ml_reader_resp    *rd_resp;
	struct ml_reader_args    *rd_arg;
	struct ml_verify_resp    *v_resp;
	struct ml_verify_args    *v_arg;
	struct mpft_thread_args   *targ;
	struct mpft_thread_resp   *tresp;
	struct mpool              *mp_ds;
	struct oid_pair           *oid;
	struct mdc_capacity        capreq;

	err = process_params(argc, argv, perf_seq_writes_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", test_name);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mp = perf_seq_writes_mpool;
	ds = perf_seq_writes_dataset;
	mlog_mclassp = mclassp_str2enum(mlog_mclassp_str);

	if ((mp[0] == 0) || (ds[0] == 0)) {
		fprintf(stderr,
			"%s: mpool (mp=<mpool>) and dataset (ds=<dataset>) must be specified\n",
			test_name);
		return merr(EINVAL);
	}
	tc = perf_seq_writes_thread_cnt;

	ret = pattern_base(perf_seq_writes_pattern);
	if (ret == -1)
		return merr(EINVAL);

	err = mpool_open(mp, O_RDWR, &mp_ds, NULL);
	if (err) {
		fprintf(stderr, "%s: cannot open dataset %s\n", test_name, mp);
		return err;
	}

	wr_arg = calloc(tc, sizeof(*wr_arg));
	targ = calloc(tc, sizeof(*targ));
	if (!wr_arg || !targ) {
		fprintf(stderr, "%s: Unable to allocate memory for arguments\n", test_name);
		err = merr(ENOMEM);
		goto free_wr_arg;
	}

	tresp = calloc(tc, sizeof(*tresp));
	if (!tresp) {
		fprintf(stderr, "%s: Unable to allocate memory for response pointers\n", test_name);
		err = merr(ENOMEM);
		goto free_targ;
	}

	if (perf_seq_writes_total_size == 0) {
		struct mp_usage    usage;

		err = mpool_usage_get(mp_ds, &usage);
		if (err) {
			fprintf(stderr, "%s: Error getting usage. %s\n", test_name,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			goto free_tresp;
		}
		perf_seq_writes_total_size = usage.mpu_fusable / 2;
		perf_seq_writes_total_size /= 4; /* has to fit in a zone? */

		fprintf(stdout, "total_size (ts) not specified, using %ld bytes\n",
			(long)perf_seq_writes_total_size);
	}
	per_thread_size = perf_seq_writes_total_size / tc;
	capreq.mdt_captgt = per_thread_size;

	write_cnt = calc_record_count(per_thread_size, perf_seq_writes_record_size);
	if (write_cnt == 0) {
		fprintf(stderr, "%s: No room to write even one record\n", test_name);
		err = merr(EINVAL);
		goto free_tresp;
	}

	oid = calloc(tc, sizeof(*oid));
	if (!oid) {
		fprintf(stderr, "%s:Unable to alloc space for oid array\n", test_name);
		err = merr(ENOMEM);
		goto free_tresp;
	}

	for (i = 0; i < tc; i++) {

		/* Create an mdc */
		err = mpool_mdc_alloc(mp_ds, &oid[i].oid[0], &oid[i].oid[1],
				      mlog_mclassp, &capreq, NULL);
		if (err) {
			fprintf(stderr, "[%d]%s: Unable to alloc mdc: %s\n", i, test_name,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			goto free_oid;
		}

		err = mpool_mdc_commit(mp_ds, oid[i].oid[0], oid[i].oid[1]);
		if (err) {
			fprintf(stderr, "[%d]%s: Unable to commit mdc: %s\n", i, test_name,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			goto free_oid;
		}

		wr_arg[i].ds = mp_ds;
		wr_arg[i].rs = perf_seq_writes_record_size;
		wr_arg[i].wc = write_cnt;
		wr_arg[i].oid.oid[0] = oid[i].oid[0];
		wr_arg[i].oid.oid[1] = oid[i].oid[1];

		targ[i].arg = &wr_arg[i];
	}

	err = mpft_thread(tc, ml_writer, targ, tresp);
	if (err != 0) {
		fprintf(stderr, "%s: Error from mpft_thread", test_name);
		goto free_oid;
	}

	usec = 0;
	bytes_written = 0;
	err_cnt = 0;

	for (i = 0; i < tc; i++) {
		wr_resp = tresp[i].resp;
		if (wr_resp->err) {
			err_cnt++;
		} else {
			usec = MAX(usec, wr_resp->usec);
			bytes_written += wr_resp->bytes_written;
		}
		free(wr_resp);
	}

	if (err_cnt) {
		fprintf(stderr, "%s: thread reported error, exiting\n", test_name);
		_exit(-1);
	}
	perf = bytes_written / usec;
	printf("%s: %d threads wrote %ld bytes in %d usecs or %4.2f MB/s\n",
		test_name, tc, (long)bytes_written, usec, perf);

	/* Read */
	if (perf_seq_writes_read) {

		memset(targ, 0, tc * sizeof(*targ));
		memset(tresp, 0, tc * sizeof(*tresp));

		rd_arg = calloc(tc, sizeof(*rd_arg));
		if (!rd_arg) {
			fprintf(stderr, "%s: Unable to allocate memory for read arguments\n",
				__func__);
			return merr(ENOMEM);
		}

		for (i = 0; i < tc; i++) {

			rd_arg[i].ds = mp_ds;
			rd_arg[i].rs = perf_seq_writes_record_size;
			rd_arg[i].rc = write_cnt;
			rd_arg[i].oid.oid[0] = oid[i].oid[0];
			rd_arg[i].oid.oid[1] = oid[i].oid[1];

			targ[i].arg = &rd_arg[i];
		}

		err = mpft_thread(tc, ml_reader, targ, tresp);
		if (err != 0) {
			fprintf(stderr, "%s: Error from mpft_thread", __func__);
			return err;
		}

		usec = 0;
		bytes_read = 0;
		err_cnt = 0;

		for (i = 0; i < tc; i++) {
			rd_resp = tresp[i].resp;
			if (rd_resp->err) {
				err_cnt++;
			} else {
				usec = MAX(usec, rd_resp->usec);
				bytes_read += rd_resp->read;
			}
			free(rd_resp);
		}

		if (err_cnt) {
			fprintf(stderr, "%s: thread reported error, exiting\n",
				__func__);
			_exit(-1);
		}
		perf = bytes_read / usec;
		printf("%s: %d threads read %ld bytes in %d usecs or %4.2f MB/s\n", __func__, tc,
		       (long)bytes_read, usec, perf);
	}

	/* Verify */
	if (perf_seq_writes_verify) {

		memset(targ, 0, tc * sizeof(*targ));
		memset(tresp, 0, tc * sizeof(*tresp));

		v_arg = calloc(tc, sizeof(*v_arg));
		if (!v_arg) {
			fprintf(stderr, "%s: Unable to allocate memory for read arguments\n",
				__func__);
			return merr(ENOMEM);
		}

		for (i = 0; i < tc; i++) {

			v_arg[i].ds = mp_ds;
			v_arg[i].rs = perf_seq_writes_record_size;
			v_arg[i].rc = write_cnt;
			v_arg[i].oid.oid[0] = oid[i].oid[0];
			v_arg[i].oid.oid[1] = oid[i].oid[1];

			targ[i].arg = &v_arg[i];
		}

		err = mpft_thread(tc, ml_verify, targ, tresp);
		if (err != 0) {
			fprintf(stderr, "%s: Error from mpft_thread", __func__);
			return err;
		}

		usec = 0;
		bytes_verified = 0;
		err_cnt = 0;

		for (i = 0; i < tc; i++) {
			v_resp = tresp[i].resp;
			if (v_resp->err) {
				err_cnt++;
			} else {
				usec = MAX(usec, v_resp->usec);
				bytes_verified += v_resp->verified;
			}
			free(v_resp);
		}

		if (err_cnt) {
			fprintf(stderr, "%s: thread reported error, exiting\n", __func__);
			_exit(-1);
		}
		perf = bytes_verified / usec;
		printf("%s: %d threads verified %ld bytes in %d usecs or %4.2f MB/s\n",
		       __func__, tc, (long)bytes_verified, usec, perf);
	}

free_oid:
	for (i = 0; i < tc; i++) {
		if (oid[i].oid[0] || oid[i].oid[1]) {
			err = mpool_mdc_delete(mp_ds, oid[i].oid[0], oid[i].oid[1]);
			if (err) {
				mpool_strinfo(err, err_str, sizeof(err_str));
				fprintf(stderr, "[%d]%s: unable to destroy mdc: %s\n",
					i, test_name, err_str);
			}
		}

	}
	free(oid);

free_tresp:
	free(tresp);
free_targ:
	free(targ);
free_wr_arg:
	(void)mpool_close(mp_ds);
	free(wr_arg);

	return err;
}

static void perf_seq_reads_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mlog.perf.seq_reads [options]\n");
	fprintf(co.co_fp, "e.g.: mpft mlog.perf.seq_reads rs=16\n");
	fprintf(co.co_fp, "\nmlog.perf.seq_reads will measure the performance "
		"in MB/s of reads of a given size (rs) to an mlog\n");

	show_default_params(perf_seq_writes_params, 0);
}

mpool_err_t perf_seq_reads(int argc, char **argv)
{
	perf_seq_writes_read = true;

	return perf_seq_writes(argc, argv);
}

/**
 * Helper Functions.
 */

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

u8 oflags = 0; /* flags to be used for mpool_mlog_open() */

/**
 *
 * Simple
 *
 */

/**
 * The simple test is meant to only test the basics of creating,
 * opening, closing, and destroying mlogs.
 *
 * Steps:
 * 1. Create a DS
 * 2. Open the DS
 * 3. Allocate and abort an mlog
 * 4. Realloc and commit an mlog
 * 5. Open the mlog
 * 6. Lookup the mlog
 * 7. Open another instance of the same mlog
 * 8. Try deleting mlog, this must fail due to the outstanding get
 * 9. Cleanup
 */

char mlog_correctness_simple_mpool[MPOOL_NAME_LEN_MAX];

static struct param_inst mlog_correctness_simple_params[] = {
	PARAM_INST_STRING(mlog_mclassp_str, sizeof(mlog_mclassp_str), "mc", "media class"),
	PARAM_INST_STRING(mlog_correctness_simple_mpool,
			  sizeof(mlog_correctness_simple_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mlog_correctness_simple_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mlog.correctness.simple [options]\n");

	show_default_params(mlog_correctness_simple_params, 0);
}

mpool_err_t mlog_correctness_simple(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	int    next_arg = 0;
	char   errbuf[ERROR_BUFFER_SIZE];
	u64    gen;
	u64    mlogid;

	struct mpool           *ds;
	struct mpool_mlog      *mlog1, *mlog2;
	struct mlog_capacity    capreq;
	struct mlog_props       props;

	show_args(argc, argv);
	err = process_params(argc, argv, mlog_correctness_simple_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mlog_correctness_simple_mpool;
	mlog_mclassp = mclassp_str2enum(mlog_mclassp_str);

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* 2. Open the DS */
	err = mpool_open(mpool, O_RDWR, &ds, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the dataset: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	capreq.lcp_captgt = 4 * 1024 * 1024;   /* 4M, arbitrary choice */
	capreq.lcp_spare  = false;

	/* 3. Allocate and abort an mlog */
	err = mpool_mlog_alloc(ds, mlog_mclassp, &capreq, &mlogid, &props);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to create mlog: %s\n", __func__, __LINE__, errbuf);
		goto close_ds;
	}

	err = mpool_mlog_abort(ds, mlogid);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to abort mlog: %s\n", __func__, __LINE__, errbuf);
		goto close_ds;
	}

	/* 4. Alloc and commit an mlog */
	err = mpool_mlog_alloc(ds, mlog_mclassp, &capreq, &mlogid, &props);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to create mlog: %s\n", __func__, __LINE__, errbuf);
		goto close_ds;
	}

	err = mpool_mlog_commit(ds, mlogid);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mlog: %s\n", __func__, __LINE__, errbuf);
		(void) mpool_mlog_abort(ds, mlogid);
		goto close_ds;
	}

	/* 5. Open the mlog */
	err = mpool_mlog_open(ds, mlogid, oflags, &gen, &mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open mlog: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mlog;
	}

	/* 7. Open another instance of the same mlog */
	err = mpool_mlog_open(ds, mlogid, oflags, &gen, &mlog2);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open mlog2: %s\n", __func__, __LINE__, errbuf);
		goto close_mlog;
	}

	/* 8. Try deleting mlog, this must fail due to the outstanding open */
	err = mpool_mlog_delete(ds, mlogid);
	if (!err) {
		if (!original_err)
			original_err = err = merr(EBUG);
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: delete mlog must have failed: %s\n",
			__func__, __LINE__, errbuf);
	}

	/* 9. Cleanup */
close_mlog:
	err = mpool_mlog_close(mlog2);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to put mlog: %s\n", __func__, __LINE__, errbuf);
	}

	err = mpool_mlog_close(mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mlog: %s\n", __func__, __LINE__, errbuf);
	}

destroy_mlog:
	err = mpool_mlog_delete(ds, mlogid);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to delete mlog: %s\n", __func__, __LINE__, errbuf);
	}

close_ds:
	err = mpool_close(ds);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close dataset: %s\n", __func__, __LINE__, errbuf);
	}

	return original_err;
}

static int verify_buf(char *buf_in, size_t buf_len, char val)
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

/**
 *
 * Basic IO - Single writer and reader
 *
 */

/**
 * The basic tests basic IO operation to an mlog.
 *
 * Steps:
 * 1. Create a DS
 * 2. Open the DS
 * 3. Allocate and commit an mlog
 * 4. Open the mlog
 * 5. Write patten to mlog both in sync and async mode
 * 6. Close and reopen the mlog
 * 7. Init for Read
 * 8. Read/Verify pattern
 * 9. Close and reopen the mlog
 * 10. Cleanup
 */

#define BUF_SIZE 4096
#define BUF_CNT  512

char mlog_correctness_basicio_mpool[MPOOL_NAME_LEN_MAX];

static struct param_inst mlog_correctness_basicio_params[] = {
	PARAM_INST_STRING(mlog_mclassp_str, sizeof(mlog_mclassp_str), "mc", "media class"),
	PARAM_INST_STRING(mlog_correctness_basicio_mpool,
			  sizeof(mlog_correctness_basicio_mpool), "mp", "mpool"),
	PARAM_INST_END
};

static void mlog_correctness_basicio_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mlog.correctness.basicio [options]\n");

	show_default_params(mlog_correctness_basicio_params, 0);
}

mpool_err_t mlog_correctness_basicio(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	int    next_arg = 0, i, rc;
	char   errbuf[ERROR_BUFFER_SIZE];
	char   buf[BUF_SIZE], buf_in[BUF_SIZE];
	size_t read_len, len1, len2;
	u64    gen1, gen2;
	u64    mlogid;

	struct mpool           *ds;
	struct mpool_mlog      *mlog1;
	struct mlog_capacity    capreq;
	struct mlog_props       props;

	show_args(argc, argv);
	err = process_params(argc, argv,
		mlog_correctness_basicio_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mlog_correctness_basicio_mpool;
	mlog_mclassp = mclassp_str2enum(mlog_mclassp_str);

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* 2. Open the DS */
	err = mpool_open(mpool, O_RDWR, &ds, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the dataset: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	capreq.lcp_captgt = 8 * 1024 * 1024;   /* 8M, arbitrary choice */
	capreq.lcp_spare  = false;

	/* 3. Allocate and commit an mlog */
	err = mpool_mlog_alloc(ds, mlog_mclassp, &capreq, &mlogid, &props);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to create mlog: %s\n", __func__, __LINE__, errbuf);
		goto close_ds;
	}

	err = mpool_mlog_commit(ds, mlogid);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mlog: %s\n", __func__, __LINE__, errbuf);
		(void) mpool_mlog_abort(ds, mlogid);
		goto close_ds;
	}

	/* 4. Open the mlog */
	err = mpool_mlog_open(ds, mlogid, oflags, &gen1, &mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open mlog: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mlog;
	}

	/* 5. Write pattern to mlog in sync mode */
	for (i = 0; i < BUF_CNT; i++) {
		struct iovec iov;

		memset(buf, i, BUF_SIZE);

		iov.iov_base = buf;
		iov.iov_len = BUF_SIZE;

		err = mpool_mlog_append(mlog1, &iov, iov.iov_len, true);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to append to mlog: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mlog;
		}
	}

	/* Write pattern to mlog in async mode */
	for (i = BUF_CNT; i < 2 * BUF_CNT; i++) {
		struct iovec iov;

		memset(buf, i, BUF_SIZE);

		iov.iov_base = buf;
		iov.iov_len = BUF_SIZE;

		err = mpool_mlog_append(mlog1, &iov, iov.iov_len, false);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to append to mlog: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mlog;
		}
	}

	err = mpool_mlog_len(mlog1, &len1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: mlog len failed: %s\n", __func__, __LINE__, errbuf);
		goto close_mlog;
	}

	/* 7. Flush the mlog */
	err = mpool_mlog_sync(mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to flush mlog: %s\n", __func__, __LINE__, errbuf);
		goto close_mlog;
	}

	/* 6. Close and reopen the mlog */
	err = mpool_mlog_close(mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mlog: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mlog;
	}

	err = mpool_mlog_open(ds, mlogid, oflags, &gen1, &mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open mlog: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mlog;
	}

	err = mpool_mlog_len(mlog1, &len2);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: mlog len failed: %s\n", __func__, __LINE__, errbuf);
		goto close_mlog;
	}

	if (len1 > len2) {
		original_err = merr(EBUG);
		fprintf(stderr, "%s.%d: mlog lengths are incorrect %lu %lu\n",
			__func__, __LINE__, len1, len2);
		goto close_mlog;
	}

	/* 7. Init for Read */
	err = mpool_mlog_rewind(mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Mlog read init failed: %s\n", __func__, __LINE__, errbuf);
		goto close_mlog;
	}

	/* 8. Read/Verify pattern */
	for (i = 0; i < BUF_CNT * 2; i++) {
		memset(buf_in, ~i, BUF_SIZE);

		err = mpool_mlog_read(mlog1, buf_in, BUF_SIZE, &read_len);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to read from mlog: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mlog;
		}

		if (BUF_SIZE != read_len) {
			fprintf(stderr, "%s.%d: Requested size not read exp %d, got %d\n",
				__func__, __LINE__, (int)BUF_SIZE, (int)read_len);
			goto close_mlog;
		}

		rc = verify_buf(buf_in, read_len, i);
		if (rc != 0) {
			fprintf(stderr, "%s.%d: Verify mismatch buf[%d]\n", __func__, __LINE__, i);
			err = merr(EINVAL);
			goto close_mlog;
		}
	}

	err = mpool_mlog_erase(mlog1, 0);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Mlog erase failed: %s\n", __func__, __LINE__, errbuf);
		goto close_mlog;
	}

	/* 9. Close and reopen the mlog */
	err = mpool_mlog_close(mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mlog: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mlog;
	}

	err = mpool_mlog_open(ds, mlogid, oflags, &gen2, &mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open mlog: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mlog;
	}

	if (gen2 <= gen1) {
		original_err = merr(EBUG);
		fprintf(stderr, "%s.%d: mlog gen is incorrect %lu %lu\n",
			__func__, __LINE__, gen1, gen2);
		goto close_mlog;
	}

	err = mpool_mlog_props_get(mlog1, &props);
	if (err || props.lpr_gen != gen2) {
		if (err)
			original_err = err;
		else
			original_err = err = merr(EBUG);
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: mlog get props failure: %s\n", __func__, __LINE__, errbuf);
		goto close_mlog;
	}

	/* 10. Cleanup */
close_mlog:
	err = mpool_mlog_close(mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mlog: %s\n", __func__, __LINE__, errbuf);
	}

destroy_mlog:
	err = mpool_mlog_delete(ds, mlogid);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to delete mlog: %s\n", __func__, __LINE__, errbuf);
	}

close_ds:
	err = mpool_close(ds);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close dataset: %s\n", __func__, __LINE__, errbuf);
	}

	return original_err;
}


/**
 *
 * Recovery
 *
 */

/**
 * mlog_correctness_recovery is a multi-part test that is intended
 * to verify that an mlog that is opened and written by one process can be
 * read from another process. Also, this checks when the spawned process exits
 * without explicitly releasing the resources, the kernel doesn't leak any
 * reference.
 *
 * Steps:
 * 1. create a DS
 * 2. open the DS in O_RDWR mode
 * 3. Allocate and commit an mlog
 * 4. Open the mlog in client serialization mode
 * 5. Write pattern to mlog in sync mode
 * 6. Validate that ds close fails due to outstanding alloc reference
 * 7. Read/Verify pattern
 * 8. Cleanup
 */

char mlog_correctness_recovery_mpool[MPOOL_NAME_LEN_MAX];

static struct param_inst mlog_correctness_recovery_params[] = {
	PARAM_INST_STRING(mlog_mclassp_str, sizeof(mlog_mclassp_str), "mc", "media class"),
	PARAM_INST_STRING(mlog_correctness_recovery_mpool,
			  sizeof(mlog_correctness_recovery_mpool), "mp", "mpool"),
	PARAM_INST_END
};

void mlog_correctness_recovery_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mlog.correctness.recovery [options]\n");

	show_default_params(mlog_correctness_recovery_params, 0);
}

mpool_err_t mlog_correctness_recovery(int argc, char **argv)
{
	mpool_err_t err = 0, original_err = 0;
	char  *mpool;
	char  *test = argv[0];
	int    next_arg = 0;
	char   errbuf[ERROR_BUFFER_SIZE];
	char   buf[BUF_SIZE], buf_in[BUF_SIZE];
	u64    gen, mlogid;
	int    i, rc;
	size_t read_len;

	struct mpool           *ds;
	struct mpool_mlog      *mlog1;
	struct mlog_capacity    capreq;
	struct mlog_props       props;


	show_args(argc, argv);
	err = process_params(argc, argv, mlog_correctness_recovery_params, &next_arg, 0);
	if (err != 0) {
		printf("%s process_params returned an error\n", __func__);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpool = mlog_correctness_recovery_mpool;
	mlog_mclassp = mclassp_str2enum(mlog_mclassp_str);

	if (mpool[0] == 0) {
		fprintf(stderr, "%s.%d: mpool (mp=<mpool>) must be specified\n",
			__func__, __LINE__);
		return merr(EINVAL);
	}

	/* 2. open the DS in O_RDWR mode */
	err = mpool_open(mpool, O_RDWR, &ds, NULL);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open the dataset: %s\n",
			__func__, __LINE__, errbuf);
		return err;
	}

	capreq.lcp_captgt = 8 * 1024 * 1024;   /* 4M, arbitrary choice */
	capreq.lcp_spare  = false;

	/* 3. Allocate and commit an mlog */
	err = mpool_mlog_alloc(ds, mlog_mclassp, &capreq, &mlogid, &props);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to create mlog: %s\n", __func__, __LINE__, errbuf);
		goto close_ds;
	}

	err = mpool_mlog_commit(ds, mlogid);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to commit mlog: %s\n", __func__, __LINE__, errbuf);
		(void) mpool_mlog_abort(ds, mlogid);
		goto close_ds;
	}

	/* 4. Open the mlog with client serialization */
	err = mpool_mlog_open(ds, mlogid, MLOG_OF_SKIP_SER, &gen, &mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to open mlog: %s\n", __func__, __LINE__, errbuf);
		goto destroy_mlog;
	}

	/* 5. Write pattern to mlog in sync mode */
	for (i = 0; i < BUF_CNT; i++) {
		struct iovec iov;

		memset(buf, i, BUF_SIZE);

		iov.iov_base = buf;
		iov.iov_len = BUF_SIZE;

		err = mpool_mlog_append(mlog1, &iov, iov.iov_len, true);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to append to mlog: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mlog;
		}
	}

	/* 6. Validate that ds close fails due to outstanding alloc reference */
	err = mpool_close(ds);
	if (!err) {
		original_err = err = merr(EBUG);
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: dataset close must have failed: %s\n",
			__func__, __LINE__, errbuf);
		fprintf(stderr, "\tTEST FAILURE: %s\n", test);
		return err;
	}

	/* 7. Read/Verify pattern */
	for (i = 0; i < BUF_CNT; i++) {
		memset(buf_in, ~i, BUF_SIZE);

		err = mpool_mlog_read(mlog1, buf_in, BUF_SIZE, &read_len);
		if (err) {
			mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
			fprintf(stderr, "%s.%d: Unable to read from mlog: %s\n",
				__func__, __LINE__, errbuf);
			goto close_mlog;
		}

		if (BUF_SIZE != read_len) {
			fprintf(stderr, "%s.%d: Requested size not read exp %d, got %d\n",
				__func__, __LINE__, (int)BUF_SIZE, (int)read_len);
			goto close_mlog;
		}

		rc = verify_buf(buf_in, read_len, i);
		if (rc != 0) {
			fprintf(stderr, "%s.%d: Verify mismatch buf[%d]\n", __func__, __LINE__, i);
			err = merr(EINVAL);
			goto close_mlog;
		}
	}

	/* 8. Cleanup */
close_mlog:
	err = mpool_mlog_close(mlog1);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close mlog: %s\n", __func__, __LINE__, errbuf);
	}

destroy_mlog:
	err = mpool_mlog_delete(ds, mlogid);
	if (err) {
		if (!original_err)
			original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to delete mlog: %s\n", __func__, __LINE__, errbuf);
	}

close_ds:
	err = mpool_close(ds);
	if (err) {
		original_err = err;
		mpool_strinfo(err, errbuf, ERROR_BUFFER_SIZE);
		fprintf(stderr, "%s.%d: Unable to close dataset: %s\n", __func__, __LINE__, errbuf);
	}

	return original_err;
}

struct test_s mlog_tests[] = {
	{ "seq_writes",  MPFT_TEST_TYPE_PERF, perf_seq_writes, perf_seq_writes_help },
	{ "seq_reads",  MPFT_TEST_TYPE_PERF, perf_seq_reads, perf_seq_reads_help },
	{ "simple",  MPFT_TEST_TYPE_CORRECTNESS, mlog_correctness_simple,
		mlog_correctness_simple_help },
	{ "basicio",  MPFT_TEST_TYPE_CORRECTNESS, mlog_correctness_basicio,
		mlog_correctness_basicio_help },
	{ "recovery", MPFT_TEST_TYPE_CORRECTNESS, mlog_correctness_recovery,
		mlog_correctness_recovery_help },
	{ NULL,  MPFT_TEST_TYPE_INVALID, NULL, NULL },
};

void mlog_help(void)
{
	fprintf(co.co_fp, "\nmlog tests validate the behavior of mlogs\n");
}

struct group_s mpft_mlog = {
	.group_name = "mlog",
	.group_test = mlog_tests,
	.group_help = mlog_help,
};
