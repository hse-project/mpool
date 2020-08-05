// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/*
 * This sample program shows how to use some basic mpool administrative APIs.
 *
 * This program does the following:
 * 1. Creates and opens an mpool
 * 2. Retrieves mpool space usage
 * 3. Retrieves mpool properties
 * 4. Closes and destroys mpool
 */

#include "common.h"

int main(int argc, char **argv)
{
	struct mpool_params    *propv;
	struct mpool_usage      usage;
	struct mpool           *mp;
	const char             *mpname, *devname;
	mpool_err_t             err;
	int                     propc = 0, rc = 0;

	if (argc < 3 || argc > 4) {
		fprintf(stdout, "Usage: %s <mpname> <capacity_dev> [staging_dev]\n", argv[0]);
		exit(1);
	}

	mpname = argv[1];
	devname = argv[2];

	/* Create mpool with the specified name and default params */
	err = mpool_create(mpname, devname, NULL, 0, NULL);
	if (err) {
		eprint(err, "%s: Unable to create mpool", mpname);
		return mpool_errno(err);
	}

	fprintf(stdout, "mpool %s created...\n", mpname);

	if (argc > 3) {
		devname = argv[3];

		/* Add staging media class to this mpool */
		err = mpool_mclass_add(mpname, devname, MP_MED_STAGING, NULL, 0, NULL);
		if (err) {
			rc = mpool_errno(err);
			eprint(err, "%s: Unable to add staging media", mpname);
			goto mp_destroy;
		}

		fprintf(stdout, "Staging media %s added to mpool %s\n", devname, mpname);
	}

	err = mpool_open(mpname, 0, &mp, NULL);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to open mpool", mpname);
		goto mp_destroy;
	}

	err = mpool_usage_get(mp, &usage);
	if (err) {
		mpool_close(mp);
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to fetch usage stats for mpool", mpname);
		goto mp_destroy;
	}

	fprintf(stdout, "mpool %s usage:\n \t Total: %luB \t Usable: %luB \t Used: %luB\n",
		mpname, usage.mpu_total, usage.mpu_usable, usage.mpu_used);

	err = mpool_close(mp);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to close mpool", mpname);
		goto mp_destroy;
	}

	/* Fetch mpool properties */
	err = mpool_list(&propc, &propv, NULL);
	if (err) {
		rc = mpool_errno(err);
		eprint(err, "%s: Unable to fetch props for mpool", mpname);
		goto mp_destroy;
	}

	fprintf(stdout, "mpool %s props:\n \t UID: %d \t Label: %s \t mblocksz: %uMiB\n",
		mpname, propv->mp_uid, propv->mp_label, propv->mp_mblocksz[MP_MED_CAPACITY]);

	free(propv);

mp_destroy:
	err = mpool_destroy(mpname, 0, NULL);
	if (err) {
		eprint(err, "%s: Unable to destroy mpool", mpname);
		return mpool_errno(err);
	}

	fprintf(stdout, "mpool %s destroyed...\n", mpname);

	return rc;
}
