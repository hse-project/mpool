// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/*
 * This sample program shows how to use mpool mlog APIs.
 *
 * This program does the following:
 * 1. Sets up an mpool
 * 2. Allocates and commits an mlog
 * 3. Appends ten 512B records asynchronously to the mlog
 * 4. Appends five 1K records asynchronously to the mlog
 * 5. Reads back records from the mlog and validates it
 * 6. Destroys the mlog and mpool
 */

#include "common.h"

#define NUM_RECORDS    15
#define NUM_REC_512B   10
#define NUM_REC_1K      5

/*
 * Append a total of 10K bytes to this mlog: 10 x 512B records and 5 x 1K records.
 * 1. Append NUM_REC_512B records of 512B each asynchronously
 * 2. Sync mlog data to media
 * 3. Append NUM_REC_1K records of 1K each synchronously
 */
static int append_mlog(struct mpool_mlog *mlogh, void *wbuf, size_t buflen)
{
	struct iovec   iov;
	mpool_err_t    err;
	int            i, sync = 0;

	for (i = 0; i < NUM_RECORDS; i++) {
		iov.iov_base = wbuf;
		iov.iov_len = i < NUM_REC_512B ? buflen / 2 : buflen;

		if (i == NUM_REC_512B) {
			err = mpool_mlog_sync(mlogh);
			if (err)
				return mpool_errno(err);

			sync = 1; /* sync append for the next 5 records */
		}

		err = mpool_mlog_append(mlogh, &iov, iov.iov_len, sync);
		if (err)
			return mpool_errno(err);
	}

	return 0;
}

static int validate_mlog(struct mpool_mlog *mlogh, void *refbuf, size_t buflen)
{
	char           *rbuf;
	mpool_err_t     err;
	int             rc = 0, i;

	/* IO buffers used for mlog IO must be page-aligned */
	rbuf = aligned_alloc(PAGE_SIZE, roundup(buflen, PAGE_SIZE));
	if (!rbuf)
		return ENOMEM;
	memset(rbuf, 0, buflen);

	/* Position the mlog's internal read cursor to the start of mlog */
	err = mpool_mlog_rewind(mlogh);
	if (err)
		rc = mpool_errno(err);

	for (i = 0; !rc && i < NUM_RECORDS; i++) {
		size_t explen, rdlen;

		err = mpool_mlog_read(mlogh, rbuf, buflen, &rdlen);
		if (err) {
			rc = mpool_errno(err);
			break;
		}

		explen = i < NUM_REC_512B ? 512 : 1024;
		if (rdlen != explen) {
			rc = EFAULT;
			break;
		}

		if (memcmp(rbuf, refbuf, rdlen)) {
			rc = EFAULT;
			break;
		}
	}

	free(rbuf);

	return rc;
}

int main(int argc, char **argv)
{
	struct mpool_mlog      *mlogh;
	struct mlog_props       props = {};
	struct mlog_capacity    capreq = {};
	struct mpool           *mp;
	const char             *mpname, *devname;
	char                   *wbuf = NULL;
	mpool_err_t             err;
	uint64_t                mlogid, gen;
	int                     rc;
	size_t                  buflen;

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

	/* mlog capacity must be a multiple of allocation unit which is 1MiB in this case */
	capreq.lcp_captgt = 2 << 20; /* 2 MiB */

	err = mpool_mlog_alloc(mp, MP_MED_CAPACITY, &capreq, &mlogid, &props);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to alloc mlog", mpname);
		goto mp_close;
	}

	/* mlog must first be committed before appending any data to it */
	err = mpool_mlog_commit(mp, mlogid);
	if (err) {
		mpool_mlog_abort(mp, mlogid);
		rc = mpool_errno(err);
		eprint(err, "(%s, 0x%lx): Unable to commit mlog", mpname, mlogid);
		goto mp_close;
	}

	fprintf(stdout, "mlog 0x%lx created in mpool %s...\n", mlogid, mpname);

	err = mpool_mlog_open(mp, mlogid, 0, &gen, &mlogh);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "(%s, 0x%lx):Unable to open mlog", mpname, mlogid);
		goto ml_delete;
	}

	 /* An mlog can be appended with as minimum as 1 byte */
	buflen = 1024;
	rc = alloc_and_prep_buf((void **)&wbuf, buflen);
	if (rc) {
		eprint(rc, "(%s, 0x%lx): Unable to prepare wbuf", mpname, mlogid);
		goto ml_close;
	}

	rc = append_mlog(mlogh, wbuf, buflen);
	if (rc) {
		eprint(rc, "(%s, 0x%lx): Unable to append mlog", mpname, mlogid);
		goto ml_close;
	}

	rc = validate_mlog(mlogh, wbuf, buflen);
	if (rc) {
		eprint(rc, "(%s, 0x%lx): Unable to validate mlog", mpname, mlogid);
		goto ml_close;
	}

	fprintf(stdout, "mlog data validation successful!\n");

ml_close:
	err = mpool_mlog_close(mlogh);
	if (err && !rc) {
		rc = mpool_errno(err);
		eprint(err, "(%s, 0x%lx): Unable to close mlog", mpname, mlogid);
	}

ml_delete:
	err = mpool_mlog_delete(mp, mlogid);
	if (err && !rc) {
		rc = mpool_errno(err);
		eprint(err, "(%s, 0x%lx): Unable to delete mlog", mpname, mlogid);
	}

	fprintf(stdout, "mlog 0x%lx destroyed from mpool %s...\n", mlogid, mpname);

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
