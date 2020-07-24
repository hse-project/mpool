// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/*
 * This sample program shows how to use mpool mblock APIs.
 *
 * This program does the following:
 * 1. Sets up an mpool
 * 2. Allocates and commits an mblock
 * 3. Writes to the mblock
 * 4. Reads back from the mblock and validates data
 * 5. Destroys the mblock and mpool
 */

#include "common.h"

/*
 * An mblock must be written in multiples of optimal IO size (except the last write
 * which must be in multiples of PAGE_SIZE)
 *
 * To keep things simple write the same data to all chunks
 */
static int write_mblock(struct mpool *mp, uint64_t mbid, void *wbuf, size_t buflen, int nchunks)
{
	struct iovec   iov;
	mpool_err_t    err;
	int            i;

	iov.iov_base = wbuf;
	iov.iov_len = buflen;

	for (i = 0; i < nchunks; i++) {
		err = mpool_mblock_write(mp, mbid, &iov, 1);
		if (err)
			return mpool_errno(err);
	}

	return 0;
}

/* Read back all chunks from the mblock and validate it */
static int
validate_mblock(struct mpool *mp, uint64_t mbid, void *refbuf, size_t buflen, int nchunks)
{
	struct iovec    iov;
	char           *rbuf;
	off_t           off;
	mpool_err_t     err;
	int             rc = 0, i;

	/* IO buffers used for mblock IO must be page-aligned */
	rbuf = aligned_alloc(PAGE_SIZE, roundup(buflen, PAGE_SIZE));
	if (!rbuf)
		return ENOMEM;

	memset(rbuf, 0, buflen);

	iov.iov_base = rbuf;
	iov.iov_len = buflen;
	off = 0;

	for (i = 0; i < nchunks; i++) {
		err = mpool_mblock_read(mp, mbid, &iov, 1, off);
		if (err) {
			rc = mpool_errno(err);
			break;
		}

		if (memcmp(rbuf, refbuf, buflen)) {
			rc = EFAULT;
			break;
		}

		off += buflen;
	}

	free(rbuf);

	return rc;
}

int main(int argc, char **argv)
{
	struct mblock_props     props = {};
	struct mpool           *mp;
	const char             *mpname, *devname;
	char                   *wbuf = NULL;
	mpool_err_t             err;
	uint64_t                mbid;
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

	err = mpool_mblock_alloc(mp, MP_MED_CAPACITY, false, &mbid, &props);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to alloc mblock", mpname);
		goto mp_close;
	}

	buflen = props.mpr_optimal_wrsz * 4;
	rc = alloc_and_prep_buf((void **)&wbuf, buflen);
	if (rc) {
		mpool_mblock_abort(mp, mbid);
		eprint(rc, "(%s, 0x%lx): Unable to prepare write buffer", mpname);
		goto mp_close;
	}

	/* Calculate the max no. of writes that can be issued for the given mblock capacity */
	nchunks = props.mpr_alloc_cap / buflen;
	nchunks = nchunks ? nchunks / 2 : 1; /* Fill half of mblock's capacity */

	rc = write_mblock(mp, mbid, wbuf, buflen, nchunks);
	if (rc) {
		mpool_mblock_abort(mp, mbid);
		eprint(err, "(%s, 0x%lx): Unable to write mblock", mpname, mbid);
		goto mp_close;
	}

	err = mpool_mblock_commit(mp, mbid);
	if (err) {
		rc = mpool_errno(err);
		mpool_mblock_abort(mp, mbid);
		eprint(err, "(%s, 0x%lx): Unable to commit mblock", mpname, mbid);
		goto mp_close;
	}

	fprintf(stdout, "mblock 0x%lx created in mpool %s...\n", mbid, mpname);

	rc = validate_mblock(mp, mbid, wbuf, buflen, nchunks);
	if (rc) {
		eprint(rc, "(%s, 0x%lx): Unable to validate mblock", mpname, mbid);
		goto mb_delete;
	}

	fprintf(stdout, "mblock data validation successful!\n");

mb_delete:
	err = mpool_mblock_delete(mp, mbid);
	if (err && !rc) {
		rc = mpool_errno(err);
		eprint(err, "(%s, 0x%lx): Unable to delete mblock", mpname, mbid);
	}

	fprintf(stdout, "mblock 0x%lx destroyed from mpool %s...\n", mbid, mpname);

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
