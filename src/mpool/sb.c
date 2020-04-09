// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */
/*
 * Superblock module.
 *
 * Defines functions for managing per drive superblocks.
 *
 */

#include <mpool/mpool.h>

#include "mpcore_defs.h"
#include "logging.h"

/*
 * Drives have 4 superblocks.
 * + sb0 at byte offset 0
 * + sb1 at byte offset SB_AREA_SZ + MDC0MD_AREA_SZ
 *
 * Read: sb0 is the authoritative copy, other copies are not used.
 * Updates: sb0 is updated first; if successful sb1 is updated
 */

/*
 * sb internal functions
 */

/**
 * sb_prop_valid() - Validate the PD properties needed to read the erase
 *	superblocks.
 *	When the superblocks are read, the zone parameters may not been known
 *	yet. They may be obtained from the superblocks.
 *
 * Returns: true if we have enough to read the superblocks.
 */
static bool sb_prop_valid(struct mpool_dev_info *pd)
{
	struct pd_prop *pd_prop = &(pd->pdi_parm.dpr_prop);

	if (SB_AREA_SZ < OMF_SB_DESC_PACKLEN) {
		merr_t err = merr(EINVAL);

		/* guarantee that the SB area is large enough to hold an SB */
		mp_pr_err("sb(%s): structure too big %lu %lu",
			  err, pd->pdi_name, (ulong)SB_AREA_SZ,
			  OMF_SB_DESC_PACKLEN);
		return false;
	}

	if ((pd_prop->pdp_devtype != PD_DEV_TYPE_BLOCK_STD) &&
	    (pd_prop->pdp_devtype != PD_DEV_TYPE_BLOCK_NVDIMM) &&
	    (pd_prop->pdp_devtype != PD_DEV_TYPE_FILE)) {
		merr_t err = merr(EINVAL);

		mp_pr_err("sb(%s): unknown device type %d",
			  err, pd->pdi_name, pd_prop->pdp_devtype);
		return false;
	}

	if (PD_LEN(pd_prop) == 0) {
		merr_t err = merr(EINVAL);

		mp_pr_err("sb(%s): unknown device size", err, pd->pdi_name);
		return false;
	}

	return true;
};

static u64 sb_idx2woff(struct mpool_dev_info *pd, u32 idx)
{
	return (u64)idx * (SB_AREA_SZ + MDC0MD_AREA_SZ);
}

/*
 * sb API functions
 */

/*
 * Determine if the mpool magic value exists in at least one place where
 * expected on drive pd.  Does NOT imply drive has a valid superblock.
 *
 * Note: only pd.status and pd.parm must be set; no other pd fields accessed.
 *
 * Returns: 1 if found, 0 if not found, -(errno) if error reading
 *
 */
int sb_magic_check(struct mpool_dev_info *pd)
{
	struct iovec    iovbuf;

	int     rval = 0, i;
	char   *inbuf;
	merr_t  err;

	if (!sb_prop_valid(pd)) {
		err = merr(EINVAL);
		mp_pr_err("sb(%s): invalid param, zonepg %u zonetot %u",
			  err, pd->pdi_name,
			  pd->pdi_parm.dpr_zonepg, pd->pdi_parm.dpr_zonetot);
		return -merr_errno(err);
	}

	assert(SB_AREA_SZ >= OMF_SB_DESC_PACKLEN);

	inbuf = kcalloc(SB_AREA_SZ, sizeof(char), GFP_KERNEL);
	if (!inbuf) {
		err = merr(ENOMEM);
		mp_pr_err("sb(%s) magic check: buffer alloc failed",
			  err, pd->pdi_name);
		return -merr_errno(err);
	}

	iovbuf.iov_base = inbuf;
	iovbuf.iov_len = SB_AREA_SZ;

	for (i = 0; i < SB_SB_COUNT; i++) {
		u64 woff = sb_idx2woff(pd, i);

		err = pd_file_preadv(pd, &iovbuf, 1, 0, woff);
		if (err) {
			rval = merr_errno(err);
			mp_pr_err("sb(%s, %d) magic: read failed, woff %lu",
				  err, pd->pdi_name, i, (ulong)woff);
		} else if (omf_sb_has_magic_le(inbuf)) {
			kfree(inbuf);
			return 1;
		}
	}

	kfree(inbuf);
	return rval;
}

/*
 * Erase superblock on drive pd.
 *
 * Note: only pd properties must be set.
 *
 * Returns: 0 if successful; merr_t otherwise
 *
 */
merr_t sb_erase(struct mpool_dev_info *pd)
{
	struct iovec    iovbuf;

	merr_t  err = 0;
	char   *buf;
	int     i;

	if (!sb_prop_valid(pd)) {
		err = merr(EINVAL);
		mp_pr_err("sb(%s) invalid param, zonepg %u zonetot %u",
			  err, pd->pdi_name, pd->pdi_parm.dpr_zonepg,
			  pd->pdi_parm.dpr_zonetot);
		return err;
	}

	assert(SB_AREA_SZ >= OMF_SB_DESC_PACKLEN);

	buf = kcalloc(SB_AREA_SZ, sizeof(char), GFP_KERNEL);
	if (!buf)
		return merr(EINVAL);

	iovbuf.iov_base = buf;
	iovbuf.iov_len = SB_AREA_SZ;

	for (i = 0; i < SB_SB_COUNT; i++) {
		u64 woff = sb_idx2woff(pd, i);

		err = pd_file_pwritev(pd, &iovbuf, 1, 0, woff, REQ_FUA);
		if (err) {
			mp_pr_err("sb(%s, %d): erase failed",
				  err, pd->pdi_name, i);
		}
	}

	kfree(buf);

	return err;
}
