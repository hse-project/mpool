// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/*
 * This sample program shows how to use mcache APIs.
 *
 * This program does the following:
 * 1. Sets up an mpool
 * 2. Allocates and commits a vector of mblocks
 * 3. Writes to the vector of mblocks
 * 4. Creates an mcache map for the vector of mblocks
 * 5. Reads ahead the mblock data using madvise and displays the resident data size (rss)
 * 6. Reads data from the mblocks through mcache and validates it
 * 7. Purges the mcache map and displays the resident data size (rss)
 * 8. Destroys the mcache map
 * 9. Destroys the vector of mblocks and mpool
 */

#include "common.h"

#define NUM_MBLOCKS    8

static void abort_mblocks(struct mpool *mp, uint64_t *mbidv, int mbidc)
{
	int i;

	for (i = 0; i < mbidc; i++)
		mpool_mblock_abort(mp, mbidv[i]);
}

static int alloc_mblocks(struct mpool *mp, uint64_t *mbidv, int mbidc, struct mblock_props *props)
{
	int            i;
	mpool_err_t    err;

	for (i = 0; i < mbidc; i++) {
		err = mpool_mblock_alloc(mp, MP_MED_CAPACITY, false, &mbidv[i], props);
		if (err) {
			abort_mblocks(mp, mbidv, i);
			return mpool_errno(err);
		}
	}

	return 0;
}

static int commit_mblocks(struct mpool *mp, uint64_t *mbidv, int mbidc)
{
	int            i;
	mpool_err_t    err;

	for (i = 0; i < mbidc; i++) {
		err = mpool_mblock_commit(mp, mbidv[i]);
		if (err) {
			abort_mblocks(mp, mbidv, i);
			return mpool_errno(err);
		}
	}

	return 0;
}

static int delete_mblocks(struct mpool *mp, uint64_t *mbidv, int mbidc)
{
	int            i, rc = 0;
	mpool_err_t    err;

	for (i = 0; i < mbidc; i++) {
		err = mpool_mblock_delete(mp, mbidv[i]);
		if (err && !rc)
			rc = mpool_errno(err);
	}

	return rc;
}

static int
write_mblocks(struct mpool *mp, uint64_t *mbidv, int mbidc, void *wbuf, size_t buflen, int nchunks)
{
	struct iovec   iov;
	mpool_err_t    err;
	int            i, j;

	/*
	 * An mblock must be written in multiples of optimal IO size (except the last write
	 * which must be in multiples of PAGE_SIZE).
	 */
	iov.iov_base = wbuf;
	iov.iov_len = buflen;

	for (i = 0; i < mbidc; i++) {
		for (j = 0; j < nchunks; j++) {
			err = mpool_mblock_write(mp, mbidv[i], &iov, 1);
			if (err)
				return mpool_errno(err);
		}
	}

	return 0;
}

static int
validate_mblocks_mcache(
	struct mpool   *mp,
	uint64_t       *mbidv,
	int             mbidc,
	void           *refbuf,
	size_t          buflen,
	int             nchunks)
{
	struct mpool_mcache_map    *mapp;
	char                       *rbuf;
	mpool_err_t                 err;
	int                         rc = 0, i, j;
	size_t                      rssp = 0, vssp = 0;

	err = mpool_mcache_mmap(mp, mbidc, mbidv, MPC_VMA_HOT, &mapp);
	if (err) {
		eprint(err, "Unable to create mcache map");
		return mpool_errno(err);
	}

	fprintf(stdout, "mcache map created for %d mblocks in mpool..\n", mbidc);

	err = mpool_mcache_madvise(mapp, 0, 0, SIZE_MAX, MADV_WILLNEED);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "Unable to madvise mcache map");
		goto exit;
	}

	/* Read back all chunks from mblocks through mcache and verify */
	for (i = 0; i < mbidc; i++) {
		rbuf = mpool_mcache_getbase(mapp, i);
		for (j = 0; j < nchunks; j++, rbuf += buflen) {
			if (memcmp(rbuf, refbuf, buflen)) {
				rc = EFAULT;
				eprint(rc, "Unable to verify mblock 0x%lx via mcache",
				       mbidv[i], j);
				goto exit;
			}
		}
	}

	fprintf(stdout, "mblocks validation successful via mcache!\n");

	err = mpool_mcache_mincore(mapp, mp, &rssp, &vssp);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "Unable to get page stats using mincore");
		goto exit;
	}

	fprintf(stdout, "mcache map before purge: virtual pages %lu resident pages %lu\n",
		vssp, rssp);

	err = mpool_mcache_purge(mapp, mp);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "Unable to purge mcache map");
		goto exit;
	}

	err = mpool_mcache_mincore(mapp, mp, &rssp, &vssp);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "Unable to get page stats using mincore");
		goto exit;
	}

	fprintf(stdout, "mcache map post purge: virtual pages %lu resident pages %lu\n",
		vssp, rssp);

exit:
	err = mpool_mcache_munmap(mapp);
	if (err && !rc) {
		rc = mpool_errno(err);
		eprint(err, "Unable to unmap mcache map");
	}

	fprintf(stdout, "mcache map destroyed...\n");

	return rc;
}

int main(int argc, char **argv)
{
	struct mblock_props     props = {};
	struct mpool           *mp;
	const char             *mpname, *devname;
	char                   *wbuf = NULL;
	mpool_err_t             err;
	uint64_t                mbidv[NUM_MBLOCKS];
	int                     rc;
	size_t                  buflen, nchunks;

	if (argc < 3 || argc > 4) {
		fprintf(stdout, "Usage: %s <mpname> <capacity_dev>\n", argv[0]);
		exit(1);
	}

	mpname = argv[1];
	devname = argv[2];

	rc = setup_mpool(mpname, devname, &mp, 1);
	if (rc) {
		eprint(rc, "%s: Unable to setup mpool", mpname);
		return rc;
	}

	rc = alloc_mblocks(mp, mbidv, NUM_MBLOCKS, &props);
	if (rc) {
		eprint(rc, "%s: Unable to alloc mbloks", mpname);
		goto mp_close;
	}

	buflen = props.mpr_optimal_wrsz * 4;
	rc = alloc_and_prep_buf((void **)&wbuf, buflen);
	if (rc) {
		abort_mblocks(mp, mbidv, NUM_MBLOCKS);
		eprint(rc, "%s: Unable to prepare write buffer", mpname);
		goto mp_close;
	}

	/* Calculate the max no. of writes that can be issued for the given mblock capacity */
	nchunks = props.mpr_alloc_cap / buflen;
	nchunks = nchunks ? nchunks / 2 : 1; /* Fill half of mblock's capacity */

	rc = write_mblocks(mp, mbidv, NUM_MBLOCKS, wbuf, buflen, nchunks);
	if (rc) {
		abort_mblocks(mp, mbidv, NUM_MBLOCKS);
		eprint(rc, "%s: Unable to write mblocks", mpname);
		goto mp_close;
	}

	rc = commit_mblocks(mp, mbidv, NUM_MBLOCKS);
	if (rc) {
		eprint(rc, "%s: Unable to commit mblocks", mpname);
		goto mp_close;
	}

	rc = validate_mblocks_mcache(mp, mbidv, NUM_MBLOCKS, wbuf, buflen, nchunks);
	if (rc) {
		eprint(rc, "%s: Unable to validate mblocks via mcache", mpname);
		goto mb_delete;
	}

mb_delete:
	rc = delete_mblocks(mp, mbidv, NUM_MBLOCKS);
	if (rc)
		eprint(rc, "%s: Unable to delete mblocks", mpname);

mp_close:
	err = mpool_close(mp);
	if (err && !rc) {
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to close mpool", mpname);
	}

	err = mpool_destroy(mpname, 0, NULL);
	if (err && !rc) {
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to destroy mpool", mpname);
	}

	free(wbuf);

	return rc;
}
