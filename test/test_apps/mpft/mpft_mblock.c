// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/**
 * This file implements tests that are to be run in the mpft (MPool
 * Functional Test) framework.
 *
 * Available tests:
 * * perf_seq_writes - test performance of mblock writes
 *   - required parameters:
 *     - mpool (mp)
 *   - options:
 *     - mblock size (ms), default: 4M
 *     - write size (ws), default: 4K
 *     - total size (ts), default: all available space in mpool
 *     - thread count (threads), default: 1
 *     - pre-alloc (pre-alloc), default: false
 *     - post-commit (post-commit), default: false)
 *
 *     Description: In the specified mpool alloc, write, and
 *       commit mblocks, using the specified number of threads.
 *       If total size (ts) is not given on the command line, the amount
 *       of space available in the mpool is determined and used for total space.
 *
 *       The number of writes is determined by dividing total space by the
 *       write size. The number of mblocks used will be equal to the number
 *       of writes. Each write will start at the beginning of an mblock and
 *       will be of the specified size.
 *
 *       For example, if the total size is 16M, and the mblock size is 4M,
 *       and the write size is 1M, then 16 mblocks will be allocated,
 *       and the first 1M of each mblock will be written.
 *
 *       By default each mblock is alloc'd, written, and committed serially.
 *       The pre-alloc option will cause all of the mblocks to be pre-allocated
 *       before any are written. The post-commit option will cause all of the
 *       commits to wait until all of the mblocks have been written.
 *
 *       The same number of threads will be used for pre-alloc and post-commit
 *       as are used for the writing.
 *
 * * perf_seq_reads
 *   - parameters and options are the same as for perf_seq_writes
 *
 *     Description: perf_seq_reads follows the same steps as perf_seq_writes,
 *       but adds a loop reading back all of the mblocks.
 */

#include <stdio.h>
#include <pthread.h>

#include <util/platform.h>
#include <util/parse_num.h>
#include <util/page.h>
#include <util/param.h>
#include <mpool/mpool.h>

#include "mpft.h"
#include "mpft_thread.h"

#define merr(_errnum)   (_errnum)

static size_t perf_seq_writes_write_size = 4096;    /* Bytes */
static size_t perf_seq_writes_total_size;         /* Bytes, 0 = all available */
static size_t perf_seq_writes_thread_cnt = 1;
static char perf_seq_writes_mpool[MPOOL_NAMESZ_MAX];
static bool perf_seq_writes_pre_alloc;
static bool perf_seq_writes_post_commit;
static bool perf_seq_writes_reads;

static
struct param_inst perf_seq_writes_params[] = {
	PARAM_INST_U64_SIZE(perf_seq_writes_total_size, "ts", "total size"),
	PARAM_INST_U32_SIZE(perf_seq_writes_write_size, "rs", "record size"),
	PARAM_INST_U32(perf_seq_writes_thread_cnt, "threads", "number of threads"),
	PARAM_INST_STRING(perf_seq_writes_mpool, sizeof(perf_seq_writes_mpool), "mp", "mpool"),
	PARAM_INST_BOOL(perf_seq_writes_pre_alloc, "pre-alloc", "alloc all mblocks before writing"),
	PARAM_INST_BOOL(perf_seq_writes_post_commit, "post-commit",
			"commit only after all writes are done"),
	PARAM_INST_END
};

static void perf_seq_writes_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mblock.perf.seq_writes [options]\n");
	fprintf(co.co_fp, "e.g.: mpft mblock.perf.seq_writes ws=8192\n");
	fprintf(co.co_fp, "\nmblock.perf.seq_writes will measure the performance "
		"in MB/s of writes of a given size (ws) to mblocks\n");

	show_default_params(perf_seq_writes_params, 0);
}

struct mb_allocator_args {
	struct mpool *mp;
	u32           mblock_cnt;
};

struct mb_allocator_resp {
	mpool_err_t err;
	u32    usec;
	u32    allocated;
};

struct mb_committor_args {
	struct mpool *mp;
	u32           mblock_cnt;
};

struct mb_committor_resp {
	mpool_err_t err;
	u32    usec;
	u32    committed;
};

struct mb_writer_args {
	struct mpool            *mp;
	u32                      ws;  /* write size in bytes */
	u32                      wc;  /* write count */
	struct mb_allocator_args ma_args;
};

struct mb_writer_resp {
	mpool_err_t err;
	u32    usec;
	u64    wrote;
};

struct mb_reader_args {
	struct mpool   *mp;
	u32             rs;  /* read size in bytes */
	u32             rc;  /* read count */
};

struct mb_reader_resp {
	mpool_err_t err;
	u32    usec;
	u64    read;
};

struct mbo {
	u64                 mblock_id;
	struct mblock_props props;
};
struct mbo *mbo;
atomic_t mbo_cnt = ATOMIC_INIT(0);
atomic_t mbo_dist = ATOMIC_INIT(0);

static void *mb_committor(void *arg)
{
	mpool_err_t err = 0;
	int    i;
	char   err_str[256];
	u32    idx;
	u32    usec;

	struct timeval start_tv, stop_tv;

	struct mpft_thread_args *targs = (struct mpft_thread_args *)arg;
	int                      id = targs->instance;
	struct mb_committor_args *args = (struct mb_committor_args *)targs->arg;
	struct mb_committor_resp *resp;

	resp = calloc(1, sizeof(*resp));
	if (!resp) {
		err = merr(ENOMEM);
		fprintf(stderr,
			"[%d]%s: Unable to allocate response struct:%s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	mpft_thread_wait_for_start(targs);

	/* start timer */
	gettimeofday(&start_tv, NULL);

	for (i = 0; i < args->mblock_cnt; i++) {
		idx = atomic_inc_return(&mbo_dist) - 1;

		err = mpool_mblock_commit(args->mp, mbo[idx].mblock_id);
		if (err) {
			mpool_strinfo(err, err_str, sizeof(err_str));
			fprintf(stderr, "%s: Error in mpool_mblock_write %s\n",
				__func__, err_str);
			resp->err = err;
			break;
		}
		resp->committed++;
	}

	/* end timer */
	gettimeofday(&stop_tv, NULL);

	if (stop_tv.tv_usec < start_tv.tv_usec) {
		stop_tv.tv_sec--;
		stop_tv.tv_usec += 1000000;
	}
	usec = (stop_tv.tv_sec - start_tv.tv_sec) * 1000000 +
		(stop_tv.tv_usec - start_tv.tv_usec);

	resp->usec = usec;

	return resp;
}

static void *mb_allocator(void *arg)
{
	mpool_err_t err = 0;
	int    i;
	char   err_str[256];
	u32    idx;
	u32    usec;

	struct timeval            start_tv, stop_tv;
	struct mpft_thread_args  *targs = (struct mpft_thread_args *)arg;
	int                       id = targs->instance;
	struct mb_allocator_args *args = (struct mb_allocator_args *)targs->arg;
	struct mb_allocator_resp *resp;

	resp = calloc(1, sizeof(*resp));
	if (!resp) {
		err = merr(ENOMEM);
		fprintf(stderr,
			"[%d]%s: Unable to allocate response struct:%s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	mpft_thread_wait_for_start(targs);

	/* start timer */
	gettimeofday(&start_tv, NULL);

	for (i = 0; i < args->mblock_cnt; i++) {
		idx = atomic_inc_return(&mbo_cnt) - 1;
		err = mpool_mblock_alloc(args->mp, MP_MED_CAPACITY, false,
					 &mbo[idx].mblock_id, &mbo[idx].props);
		if (err) {
			resp->err = err;
			break;
		}
		resp->allocated++;
	}

	/* end timer */
	gettimeofday(&stop_tv, NULL);

	if (stop_tv.tv_usec < start_tv.tv_usec) {
		stop_tv.tv_sec--;
		stop_tv.tv_usec += 1000000;
	}
	usec = (stop_tv.tv_sec - start_tv.tv_sec) * 1000000 + (stop_tv.tv_usec - start_tv.tv_usec);

	resp->usec = usec;

	return resp;
}

u32 get_mblock(struct mb_allocator_args *args)
{
	u32    idx;
	mpool_err_t err;
	char   err_str[256];

	if (perf_seq_writes_pre_alloc == true) {
		idx = atomic_inc_return(&mbo_dist) - 1;
	} else {
		idx = atomic_inc_return(&mbo_cnt) - 1;
		err = mpool_mblock_alloc(args->mp, MP_MED_CAPACITY, false,
					 &mbo[idx].mblock_id, &mbo[idx].props);
		if (err) {
			fprintf(stderr, "%s: Error in mpool_mblock_write %s\n", __func__,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			idx = ~0;
		}
	}
	return idx;
}

static void *mb_writer(void *arg)
{
	struct mpft_thread_args  *targs = (struct mpft_thread_args *)arg;
	struct mb_writer_args    *args = (struct mb_writer_args *)targs->arg;
	struct mb_writer_resp    *resp;
	struct timeval            start_tv, stop_tv;

	mpool_err_t    err = 0;
	char          *buf;
	char           err_str[256];
	struct iovec   iov;
	u32            usec;

	int  rc;
	int  id = targs->instance;
	u32  idx;
	u32  writes_remaining = args->wc;

	resp = calloc(1, sizeof(*resp));
	if (!resp) {
		err = merr(ENOMEM);
		fprintf(stderr,
			"[%d]%s: Unable to allocate response struct:%s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	rc = posix_memalign((void **)&buf, PAGE_SIZE, args->ws);
	if (rc) {
		fprintf(stderr, "[%d]: Unable to allocate buf\n", id);
		resp->err = merr(rc);
		return resp;
	}
	memset(buf, 42, args->ws);

	iov.iov_base = buf;
	iov.iov_len = args->ws;

	mpft_thread_wait_for_start(targs);

	/* start timer */
	gettimeofday(&start_tv, NULL);

	while (writes_remaining) {
		idx = get_mblock(&args->ma_args);
		if (idx == ~0) {	/* out of space */
			resp->err = merr(ENOMEM);
			fprintf(stderr, "[%d]%s: Unable to allocate mblock\n", id, __func__);
			fprintf(stderr, "\t%d writes remaining\n", writes_remaining);
			fprintf(stderr, "cnt %d dist %d\n", atomic_read(&mbo_cnt),
				atomic_read(&mbo_dist));
			return resp;

		}

		/* write */
		err = mpool_mblock_write(args->mp, mbo[idx].mblock_id, &iov, 1);
		if (err) {
			fprintf(stderr, "%s: Error in mpool_mblock_write %s\n", __func__,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			resp->err = err;
			break;
		}

		/* commit */
		if (perf_seq_writes_post_commit == false) {

			err = mpool_mblock_commit(args->mp, mbo[idx].mblock_id);
			if (err) {
				mpool_strinfo(err, err_str, sizeof(err_str));
				fprintf(stderr, "%s: Error in mpool_mblock_write %s\n",
					__func__, err_str);
				resp->err = err;
				break;
			}
		}
		writes_remaining--;
	}
	/* end timer */
	gettimeofday(&stop_tv, NULL);

	free(buf);

	if (stop_tv.tv_usec < start_tv.tv_usec) {
		stop_tv.tv_sec--;
		stop_tv.tv_usec += 1000000;
	}
	usec = (stop_tv.tv_sec - start_tv.tv_sec) * 1000000 + (stop_tv.tv_usec - start_tv.tv_usec);

	resp->usec = usec;
	resp->wrote = args->ws * args->wc;

	return resp;
}

static void *mb_reader(void *arg)
{
	struct mpft_thread_args  *targs = (struct mpft_thread_args *)arg;
	struct mb_reader_args    *args = (struct mb_reader_args *)targs->arg;
	struct mb_reader_resp    *resp;
	struct timeval            start_tv, stop_tv;

	mpool_err_t    err = 0;
	char          *buf;
	char           err_str[256];
	struct iovec   iov;
	u32            usec;

	int  rc;
	int  id = targs->instance;
	u32  idx;
	u32  reads_remaining = args->rc;

	resp = calloc(1, sizeof(*resp));
	if (!resp) {
		err = merr(ENOMEM);
		fprintf(stderr,
			"[%d]%s: Unable to allocate response struct:%s\n", id,
			__func__, mpool_strinfo(err, err_str, sizeof(err_str)));
		return resp;
	}

	rc = posix_memalign((void **)&buf, PAGE_SIZE, args->rs);
	if (rc) {
		fprintf(stderr, "[%d]: Unable to allocate buf\n", id);
		resp->err = merr(rc);
		return resp;
	}
	memset(buf, 42, args->rs);

	iov.iov_base = buf;
	iov.iov_len = args->rs;

	mpft_thread_wait_for_start(targs);

	/* start timer */
	gettimeofday(&start_tv, NULL);

	while (reads_remaining) {
		idx = atomic_inc_return(&mbo_dist) - 1;

		/* read */
		err = mpool_mblock_read(args->mp, mbo[idx].mblock_id, &iov, 1, 0);
		if (err) {
			fprintf(stderr, "%s: Error in mpool_mblock_read %s\n", __func__,
				mpool_strinfo(err, err_str, sizeof(err_str)));
			resp->err = err;
			break;
		}

		reads_remaining--;
	}
	/* end timer */
	gettimeofday(&stop_tv, NULL);

	free(buf);

	if (stop_tv.tv_usec < start_tv.tv_usec) {
		stop_tv.tv_sec--;
		stop_tv.tv_usec += 1000000;
	}
	usec = (stop_tv.tv_sec - start_tv.tv_sec) * 1000000 + (stop_tv.tv_usec - start_tv.tv_usec);

	resp->usec = usec;
	resp->read = args->rs * args->rc;

	return resp;
}


void perf_seq_write_show_default_params(void)
{
	char   string[80];

	printf("mpool %s\n", perf_seq_writes_mpool);

	show_u64_size(string, sizeof(string), &perf_seq_writes_total_size, 0);
	printf("total size %s\n", string);

	show_u32_size(string, sizeof(string), &perf_seq_writes_write_size, 0);
	printf("write size %s\n", string);

	printf("thread count %d\n", (int)perf_seq_writes_thread_cnt);

	show_bool(string, sizeof(string), &perf_seq_writes_pre_alloc, 0);
	printf("pre-alloc = %s\n", string);

	show_bool(string, sizeof(string), &perf_seq_writes_post_commit, 0);
	printf("post-commit = %s\n", string);
}


static mpool_err_t perf_seq_writes(int argc, char **argv)
{
	mpool_err_t err = 0;
	int    next_arg = 0;
	char  *mpname;
	u32    tc;
	int    i;
	int    err_cnt;
	char   errbuf[256];
	u32    ma_usec = 0;	/* Alloc */
	u32    wr_usec = 0;	/* Write */
	u32    mc_usec = 0;	/* Commit */
	u32    agg_usec = 0;	/* Aggregate */
	u32    rd_usec = 0;	/* Write */
	u32    per_thread_write_cnt;
	u64    bytes_wrote;
	u64    bytes_read;
	double perf;
	u32    mblocks_needed;
	u32    mblocks_available;
	u32    mblocks_needed_per_thread;
	u32    mblocks_allocated;
	u32    mblocks_committed;
	char  *test_name = argv[0];

	struct mb_writer_resp    *wr_resp;
	struct mb_writer_args    *wr_arg = NULL;
	struct mb_reader_resp    *rd_resp;
	struct mb_reader_args    *rd_arg;
	struct mb_allocator_resp  *ma_resp;
	struct mb_allocator_args  *ma_arg;
	struct mb_committor_resp  *mc_resp;
	struct mb_committor_args  *mc_arg;
	struct mpft_thread_args   *targ = NULL;
	struct mpft_thread_resp   *tresp = NULL;
	struct mpool              *mp;
	struct mpool_usage         usage;
	struct mpool_params        params;
	u64                        mblocksz;

	err = process_params(perf_seq_writes_params, argc, argv, &next_arg);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		fprintf(stderr, "%s: unable to convert `%s': %s\n",
			test_name, argv[next_arg], errbuf);
		return err;
	}

	/* advance the arg pointer once for the "verb" */
	next_arg++;

	mpname = perf_seq_writes_mpool;
	if (mpname[0] == 0) {
		fprintf(stderr, "%s: mpool (mp=<mpool>) must be specified\n", test_name);
		return merr(EINVAL);
	}

	tc = perf_seq_writes_thread_cnt;

	/* Determine available space */
	err = mpool_open(mpname, 0, &mp, NULL);
	if (err) {
		fprintf(stderr, "%s: Unable to open mpool %s\n", test_name, mpname);
		goto free_tresp;
	}

	err = mpool_params_get(mp, &params, NULL);
	if (err) {
		fprintf(stderr, "%s: Error getting params. %s\n", test_name,
			mpool_strinfo(err, errbuf, sizeof(errbuf)));
		mpool_close(mp);
		return err;
	}

	err = mpool_usage_get(mp, &usage);
	if (err) {
		fprintf(stderr, "%s: Error getting usage. %s\n", test_name,
			mpool_strinfo(err, errbuf, sizeof(errbuf)));
		mpool_close(mp);
		return err;
	}

	mblocksz = params.mp_mblocksz[MP_MED_CAPACITY] << 20;
	if (perf_seq_writes_write_size > mblocksz) {
		fprintf(stderr,
			"%s: write size cannot be greater than mblock size\n", test_name);
		fprintf(stderr, "write size %lu, mblock size %lu\n",
			perf_seq_writes_write_size, mblocksz);
		return merr(EINVAL);
	}

	mblocks_available = usage.mpu_fusable / mblocksz;

	if (perf_seq_writes_total_size == 0) {

		perf_seq_writes_total_size = perf_seq_writes_write_size *
			(mblocks_available - (mblocks_available % tc));

		fprintf(stdout, "total_size (ts) not specified, using %ld bytes\n",
			(long)perf_seq_writes_total_size);
	}

	mblocks_needed = perf_seq_writes_total_size / perf_seq_writes_write_size;

	if (mblocks_needed % tc)
		mblocks_needed += (tc - (mblocks_needed % tc));

	per_thread_write_cnt = mblocks_needed / tc;

	if (co.co_verbose) {
		fprintf(stderr, "%s: mblock size %ld\n", __func__, (long)mblocksz);
		fprintf(stderr, "%s: write size %ld\n", __func__, (long)perf_seq_writes_write_size);
		fprintf(stderr, "%s: thread count %d\n", __func__, tc);
		fprintf(stderr, "%s: mblocks needed %d\n", __func__, mblocks_needed);
		fprintf(stderr, "%s: mblocks available %d\n", __func__, mblocks_available);
		fprintf(stderr, "%s: per thread write cnt %d\n", __func__, per_thread_write_cnt);
	}

	if (mblocks_available < mblocks_needed) {
		fprintf(stderr, "%s: Insufficient space for test parameters\n", __func__);
		fprintf(stderr, "\tAvailable: %d, Needed %d\n", mblocks_available, mblocks_needed);
		mpool_close(mp);
		return merr(EINVAL);
	}

	wr_arg = calloc(tc, sizeof(*wr_arg));
	targ = calloc(tc, sizeof(*targ));
	if (!wr_arg || !targ) {
		fprintf(stderr, "%s: Unable to allocate memory for arguments\n", test_name);
		err = merr(ENOMEM);
		goto free_arg;
	}

	tresp = calloc(tc, sizeof(*tresp));
	if (!tresp) {
		fprintf(stderr, "%s: Unable to allocate memory for response pointers\n", test_name);
		err = merr(ENOMEM);
		goto free_arg;
	}

	mbo = calloc(mblocks_needed, sizeof(*mbo));

	mblocks_needed_per_thread = (mblocks_needed / tc);
	if (mblocks_needed % tc)
		mblocks_needed_per_thread++;

	atomic_set(&mbo_cnt, 0);

	if (perf_seq_writes_pre_alloc == true) {

		ma_arg = calloc(tc, sizeof(*ma_arg));
		if (ma_arg == NULL) {
			fprintf(stderr, "%s: Unable to alloc mem for ma_arg\n", test_name);
			goto free_tresp;
		}
		for (i = 0; i < tc; i++) {
			ma_arg[i].mp = mp;
			ma_arg[i].mblock_cnt = mblocks_needed_per_thread;
			targ[i].arg = &ma_arg[i];
		}
		err = mpft_thread(tc, mb_allocator, targ, tresp);
		if (err != 0) {
			fprintf(stderr, "%s: Error from mpft_thread", test_name);
			free(ma_arg);
			goto free_tresp;
		}
		err_cnt = 0;
		ma_usec = 0;
		mblocks_allocated = 0;

		for (i = 0; i < tc; i++) {
			ma_resp = tresp[i].resp;
			if (ma_resp->err)
				err_cnt++;
			mblocks_allocated += ma_resp->allocated;
			ma_usec = MAX(ma_usec, ma_resp->usec);
			free(ma_resp);
		}
		free(ma_arg);
		if (err_cnt != 0) {
			fprintf(stderr, "%s: aborting due to error allocating mblocks\n",
				test_name);
			err = merr(EIO);
			goto free_tresp;
		}
		fprintf(stdout, "%s: %d mblocks allocated in %d usecs\n",
			test_name, mblocks_allocated, ma_usec);
	}

	for (i = 0; i < tc; i++) {

		wr_arg[i].mp = mp;
		wr_arg[i].ws = perf_seq_writes_write_size;
		wr_arg[i].wc = per_thread_write_cnt;
		wr_arg[i].ma_args.mp = mp;
		wr_arg[i].ma_args.mblock_cnt = mblocks_needed_per_thread;

		targ[i].arg = &wr_arg[i];
	}

	err = mpft_thread(tc, mb_writer, targ, tresp);
	if (err != 0) {
		fprintf(stderr, "%s: Error from mpft_thread", test_name);
		goto free_tresp;
	}

	wr_usec = 0;
	bytes_wrote = 0;
	err_cnt = 0;

	for (i = 0; i < tc; i++) {
		wr_resp = tresp[i].resp;
		if (wr_resp->err) {
			err_cnt++;
		} else {
			wr_usec = MAX(wr_usec, wr_resp->usec);
			bytes_wrote += wr_resp->wrote;
		}
		free(wr_resp);
	}

	if (err_cnt) {
		fprintf(stderr, "%s: thread reported error, exiting\n", test_name);
		err = merr(EIO);
		goto destroy_mblocks;
	}
	perf = bytes_wrote / wr_usec;
	printf("%s: %d threads wrote %ld bytes in %d usecs or %4.2f MB/s\n",
	       test_name, tc, (long)bytes_wrote, wr_usec, perf);

	if (perf_seq_writes_post_commit == true) {

		atomic_set(&mbo_dist, 0);

		mc_arg = calloc(tc, sizeof(*mc_arg));
		for (i = 0; i < tc; i++) {
			mc_arg[i].mp = mp;
			mc_arg[i].mblock_cnt = mblocks_needed_per_thread;
			targ[i].arg = &mc_arg[i];
		}
		err = mpft_thread(tc, mb_committor, targ, tresp);
		if (err != 0) {
			fprintf(stderr, "%s: Error from mpft_thread", test_name);
			goto destroy_mblocks;
		}

		err_cnt = 0;
		mc_usec = 0;
		mblocks_committed = 0;

		for (i = 0; i < tc; i++) {
			mc_resp = tresp[i].resp;
			if (mc_resp->err)
				err_cnt++;
			mblocks_committed += mc_resp->committed;
			mc_usec = MAX(mc_usec, mc_resp->usec);
			free(mc_resp);
		}
		free(mc_arg);
		if (err_cnt != 0) {
			fprintf(stderr, "%s: aborting due to error committing mblocks\n",
				test_name);
			err = merr(EIO);
			goto destroy_mblocks;
		}
		fprintf(stdout, "[%s]%d mblocks committed in %d usecs\n",
			test_name, mblocks_committed, mc_usec);
	}

	if (perf_seq_writes_post_commit || perf_seq_writes_pre_alloc) {
		agg_usec = ma_usec + wr_usec + mc_usec;

		perf = bytes_wrote / agg_usec;
		printf("%s: aggregate: %d threads wrote %ld bytes in %d usecs or %4.2f MB/s\n",
		       test_name, tc, (long)bytes_wrote, agg_usec, perf);
	}

	if (perf_seq_writes_reads == true) {

		atomic_set(&mbo_dist, 0);

		rd_arg = calloc(tc, sizeof(*rd_arg));
		if (rd_arg == NULL) {
			fprintf(stderr, "%s: Unable to alloc mem for rd_arg\n", test_name);
			goto destroy_mblocks;
		}
		for (i = 0; i < tc; i++) {
			rd_arg[i].mp = mp;
			rd_arg[i].rc = mblocks_needed_per_thread;
			rd_arg[i].rs = perf_seq_writes_write_size;
			targ[i].arg = &rd_arg[i];
		}
		err = mpft_thread(tc, mb_reader, targ, tresp);
		if (err != 0) {
			fprintf(stderr, "%s: Error from mpft_thread", test_name);
			free(rd_arg);
			goto free_tresp;
		}
		err_cnt = 0;
		rd_usec = 0;
		bytes_read = 0;

		for (i = 0; i < tc; i++) {
			rd_resp = tresp[i].resp;
			if (rd_resp->err)
				err_cnt++;
			bytes_read += rd_resp->read;
			rd_usec = MAX(rd_usec, rd_resp->usec);
			free(rd_resp);
		}
		free(rd_arg);
		if (err_cnt != 0) {
			fprintf(stderr, "%s: aborting due to error reading mblocks\n", test_name);
			err = merr(EIO);
			goto free_tresp;
		}
		perf = bytes_read / rd_usec;
		printf("%s: %d threads read %ld bytes in %d usecs or %4.2f MB/s\n", test_name, tc,
		       (long)bytes_read, rd_usec, perf);
	}

destroy_mblocks:
	for (i = 0; i < mblocks_needed; i++) {
		err = mpool_mblock_delete(mp, mbo[i].mblock_id);
		if (err) {
			mpool_strinfo(err, errbuf, sizeof(errbuf));
			fprintf(stderr, "%s: Error deleting mblocks: %s\n", test_name, errbuf);
			break;
		}
	}

free_tresp:
	free(tresp);

free_arg:
	free(wr_arg);
	free(targ);

	(void)mpool_close(mp);

	return err;
}

static void perf_seq_reads_help(void)
{
	fprintf(co.co_fp, "\nusage: mpft mblock.perf.seq_reads [options]\n");
	fprintf(co.co_fp, "e.g.: mpft mblock.perf.seq_reads ws=8192\n");
	fprintf(co.co_fp, "\nmblock.perf.seq_writes will measure the performance "
		"in MB/s of reads of a given size (ws) to mblocks\n");

	show_default_params(perf_seq_writes_params, 0);
}

static mpool_err_t perf_seq_reads(int argc, char **argv)
{
	perf_seq_writes_reads = true;

	return perf_seq_writes(argc, argv);

}

struct test_s mblock_tests[] = {
	{ "seq_writes",  MPFT_TEST_TYPE_PERF, perf_seq_writes, perf_seq_writes_help },
	{ "seq_reads",  MPFT_TEST_TYPE_PERF, perf_seq_reads, perf_seq_reads_help },
	{ NULL,  MPFT_TEST_TYPE_INVALID, NULL, NULL },
};

void mblock_help(void)
{
	fprintf(co.co_fp, "\nmblock tests validate the behavior of mblocks\n");
}

struct group_s mpft_mblock = {
	.group_name = "mblock",
	.group_test = mblock_tests,
	.group_help = mblock_help,
};
