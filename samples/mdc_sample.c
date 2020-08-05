// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/*
 * This sample program shows how to use mpool MDC APIs.
 *
 * This program does the following:
 * 1. Sets up an mpool
 * 2. Allocates and commits an MDC
 * 3. Appends 6 records to MDC which fills nearly 60% of the MDC
 * 4. Updates all 6 records in the MDC which fills up the MDC causing append failure
 * 5. Triggers MDC compaction which eliminates stale records and creates more room
 * 6. Retries the failed append and completes updating records
 * 7. Reads back records from the MDC and validates whether it contains the latest records
 * 8. Destroys the MDC and mpool
 */

#include "common.h"

struct person {
	char name[32];
	char address[2 * 1024];
	char skills[2 * 1024];
	char resume[100 * 1024];
} prec[] = {
	{"person1", "address1", "skill1", "resume1"},
	{"person2", "address2", "skill2", "resume2"},
	{"person3", "address3", "skill3", "resume3"},
	{"person4", "address4", "skill4", "resume4"},
	{"person5", "address5", "skill5", "resume5"},
	{"person6", "address6", "skill6", "resume6"},
};

int    reccnt = sizeof(prec) / sizeof(prec[0]);
size_t reclen = sizeof(prec[0]);

static int compact_records(struct mpool_mdc *mdc)
{
	mpool_err_t err;
	int         i, sync = 0;

	err = mpool_mdc_cstart(mdc);
	if (err)
		return mpool_errno(err);

	/* Serialize the latest in-memory records to MDC */
	for (i = 0; i < reccnt; i++) {
		err = mpool_mdc_append(mdc, &prec[i], reclen, sync);
		if (err)
			return mpool_errno(err);
	}

	err = mpool_mdc_cend(mdc);
	if (err)
		return mpool_errno(err);

	return 0;
}

/*
 * Update the records and log these updates to MDC.
 * These updates exceed MDC size, hence append fails with EFBIG.
 * EFBIG is an indication to trigger compaction.
 * The compaction logic in our sample serializes the latest in-memory records into the MDC.
 * Post compaction the stale records are eliminated from this MDC creating more room.
 */
static int update_records(struct mpool_mdc *mdc)
{
	int            i, sync;
	mpool_err_t    err;

	sync = 1;
	for (i = 0; i < reccnt; i++) {
		strcat(prec[i].address, "_updated");
retry:
		err = mpool_mdc_append(mdc, &prec[i], reclen, sync);
		if (err) {
			if (mpool_errno(err) == EFBIG) {
				int rc;

				fprintf(stdout, "Triggering MDC compaction\n");

				rc = compact_records(mdc);
				if (rc) {
					eprint(rc, "0x%lx: Unable to compact mdc", mdc);
					return rc;
				}

				fprintf(stdout, "MDC compaction successful!\n");

				goto retry; /* Compaction done, retry failed append */
			}

			return mpool_errno(err);
		}
	}

	return 0;
}

static bool match_records(char *rbuf)
{
	int i;

	for (i = 0; i < reccnt; i++) {
		if (!memcmp(&prec[i], rbuf, reclen))
			return true;
	}

	return false;
}

/* Read MDC until EOF and validate that the latest in-memory records are in the MDC */
static int validate_records(struct mpool_mdc *mdc)
{
	int             rc = 0, match;
	char           *rbuf;
	mpool_err_t     err;

	rbuf = aligned_alloc(PAGE_SIZE, roundup(reclen, PAGE_SIZE));
	if (!rbuf)
		return ENOMEM;
	memset(rbuf, 0, reclen);

	match = 0;
	do {
		size_t rdlen;

		err = mpool_mdc_read(mdc, rbuf, reclen, &rdlen);
		if (err) {
			rc = mpool_errno(err);
			break;
		}

		if (rdlen == 0)
			break; /* Hit end of log */

		if (rdlen != reclen) {
			rc = EFAULT;
			break;
		}

		if (match_records(rbuf))
			++match;
	} while (true);

	if (match < reccnt)
		rc = EFAULT;

	free(rbuf);

	return rc;
}

int main(int argc, char **argv)
{
	struct mpool_mdc       *mdc;
	struct mdc_props        props = {};
	struct mdc_capacity     capreq = {};
	struct mpool           *mp;
	const char             *mpname, *devname;
	mpool_err_t             err;
	uint64_t                mlogid1, mlogid2;
	int                     rc, i, sync;

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

	/* mlog capacity must be a multiple of allocation unit, 1MiB in this case */
	capreq.mdt_captgt = 1 << 20; /* 1 MiB */

	err = mpool_mdc_alloc(mp, &mlogid1, &mlogid2, MP_MED_CAPACITY, &capreq, &props);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to alloc MDC", mpname);
		goto mp_close;
	}

	/* mlog must first be committed before appending any data to it */
	err = mpool_mdc_commit(mp, mlogid1, mlogid2);
	if (err) {
		rc = mpool_errno(err);
		mpool_mdc_abort(mp, mlogid1, mlogid2);
		eprint(err, "(%s: (0x%lx, 0x%lx): Unable to commit MDC", mpname, mlogid1, mlogid2);
		goto mp_close;
	}

	fprintf(stdout, "MDC (0x%lx, 0x%lx) created in mpool %s...\n", mlogid1, mlogid2, mpname);

	err = mpool_mdc_open(mp, mlogid1, mlogid2, 0, &mdc);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "%s: (0x%lx, 0x%lx): Unable to open MDC", mpname, mlogid1, mlogid2);
		goto mdc_delete;
	}

	/* Append the initial set of records to MDC, writing each record synchronously. */
	sync = 1;
	for (i = 0; i < reccnt; i++) {
		err = mpool_mdc_append(mdc, &prec[i], reclen, sync);
		if (err) {
			rc = mpool_errno(err);
			eprint(err, "%s: (0x%lx, 0x%lx): Unable to append MDC, record %d/%d",
			       mpname, mlogid1, mlogid2, i, reccnt);
			goto mdc_close;
		}
	}

	rc = update_records(mdc);
	if (rc) {
		eprint(rc, "%s: (0x%lx, 0x%lx): Unable to update MDC records",
		       mpname, mlogid1, mlogid2);
		goto mdc_close;
	}

	/* Position internal MDC read cursor to the beginning */
	err = mpool_mdc_rewind(mdc);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "%s: (0x%lx, 0x%lx): Unable to rewind MDC", mpname, mlogid1, mlogid2);
		goto mdc_close;
	}

	rc = validate_records(mdc);
	if (rc) {
		eprint(rc, "%s: (0x%lx, 0x%lx): Unable to validate MDC records",
		       mpname, mlogid1, mlogid2);
		goto mdc_close;
	}

	fprintf(stdout, "MDC data validation successful!\n");

mdc_close:
	err = mpool_mdc_close(mdc);
	if (err && !rc) {
		rc = mpool_errno(err);
		eprint(err, "%s: (0x%lx, 0x%lx): Unable to close MDC", mpname, mlogid1, mlogid2);
	}

mdc_delete:
	err = mpool_mdc_delete(mp, mlogid1, mlogid2);
	if (err && !rc) {
		rc = mpool_errno(err);
		eprint(err, "%s: (0x%lx, 0x%lx): Unable to delete MDC", mpname, mlogid1, mlogid2);
	}

	fprintf(stdout, "MDC (0x%lx, 0x%lx) destroyed from mpool %s...\n",
		mlogid1, mlogid2, mpname);

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

	return rc;
}
