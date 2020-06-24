// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/*
 * Media pool (mpool) manager module.
 *
 * Defines functions to create and maintain mpools comprising multiple drives
 * in multiple media classes used for storing mblocks and mlogs.
 */

#include <util/string.h>
#include <util/minmax.h>
#include <util/compiler.h>

#include "mpcore_defs.h"
#include <mpool/mpool.h>

#include <stdarg.h>

enum prx_pd_status mpool_pd_status_get(struct mpool_dev_info *pd)
{
	enum prx_pd_status  val;

	/* Acquire semantics used so that no reads will be re-ordered from
	 * before to after this read.
	 */
	val = atomic_read_acq(&pd->pdi_status);

	return val;
}

void mpool_pd_status_set(struct mpool_dev_info *pd, enum prx_pd_status status)
{
	/* All prior writes must be visible prior to the status change */
	smp_wmb();
	atomic_set(&pd->pdi_status, status);
}

static merr_t
mpool_dev_init_all(
	struct mpool_dev_info  *pdv,
	u64                     dcnt,
	char                  **dpaths,
	struct mpool_devrpt    *devrpt,
	struct pd_prop	       *pd_prop)
{
	merr_t      err = 0;
	int         idx;
	char       *pdname;

	if (dcnt == 0)
		return merr(EINVAL);

	for (idx = 0; idx < dcnt; idx++, pd_prop++) {
		err = pd_file_open(dpaths[idx], &pdv[idx].pdi_parm);
		if (err) {
			mpool_devrpt(devrpt, MPOOL_RC_ERRMSG, -1,
				     "Getting device %s params, open failed %d",
				     dpaths[idx], merr_errno(err));
		} else {
			pd_file_init(&pdv[idx].pdi_parm, pd_prop);
			pdv[idx].pdi_state = OMF_PD_ACTIVE;

			pdname = strrchr(dpaths[idx], '/');
			pdname = pdname ? pdname + 1 : dpaths[idx];
			strncpy(pdv[idx].pdi_name, pdname, sizeof(pdv[idx].pdi_name)-1);
			mpool_pd_status_set(&pdv[idx], PRX_STAT_ONLINE);
		}

		if (err) {
			while (idx-- > 0)
				pd_file_close(&pdv[idx].pdi_parm);
			break;
		}
	}

	return err;
}

merr_t mpool_sb_magic_check(char *dpath, struct pd_prop *pd_prop, struct mpool_devrpt *devrpt)
{
	struct mpool_dev_info *pd;
	merr_t                 err;
	int                    rval;

	if (!dpath || !pd_prop || !devrpt)
		return merr(EINVAL);

	pd = calloc(1, sizeof(*pd));
	if (!pd) {
		mpool_devrpt(devrpt, MPOOL_RC_ERRMSG, -1, "mpool dev info alloc failed");
		return merr(ENOMEM);
	}

	err = mpool_dev_init_all(pd, 1, &dpath, devrpt, pd_prop);
	if (err) {
		free(pd);
		return err;
	}

	/* confirm drive does not contain mpool magic value */
	rval = sb_magic_check(pd);
	if (rval < 0) {
		mpool_devrpt(devrpt, MPOOL_RC_ERRMSG, -1,
			     "superblock magic read from %s failed", pd->pdi_name);
		err = merr(rval);
	} else if (rval > 0) {
		mpool_devrpt(devrpt, MPOOL_RC_MAGIC, 0, NULL);
		err = merr(EBUSY);
	}

	pd_file_close(&pd->pdi_parm);
	free(pd);

	return err;
}

merr_t mpool_sb_erase(int dcnt, char **dpaths, struct pd_prop *pd, struct mpool_devrpt *devrpt)
{
	struct mpool_dev_info *pdv;
	merr_t                 err;
	int                    i;

	if (!dpaths || !pd || !devrpt || dcnt < 1 || dcnt > MPOOL_DRIVES_MAX)
		return merr(EINVAL);

	pdv = calloc((MPOOL_DRIVES_MAX + 1), sizeof(*pdv));
	if (!pdv)
		return merr(ENOMEM);

	err = mpool_dev_init_all(pdv, dcnt, dpaths, devrpt, pd);
	if (err)
		goto exit;

	for (i = 0; i < dcnt; i++) {
		merr_t sberr;

		sberr = sb_erase(&pdv[i]);

		if (sberr && !err) {
			mpool_devrpt(devrpt, MPOOL_RC_ERRMSG, -1,
				     "superblock erase of %s failed", dpaths[i]);
			err = sberr;
		}

		pd_file_close(&pdv[i].pdi_parm);
	}

exit:
	free(pdv);

	return err;
}

/*
 * mpool internal functions
 */
void mpool_devrpt_init(struct mpool_devrpt *devrpt)
{
	if (!devrpt)
		return;

	devrpt->mdr_rcode = MPOOL_RC_NONE;
	devrpt->mdr_off = -1;
	devrpt->mdr_msg[0] = '\000';
}

void mpool_devrpt(struct mpool_devrpt *devrpt, enum mpool_rc rcode, int off, const char *fmt, ...)
{
	va_list ap;

	if (!devrpt)
		return;

	devrpt->mdr_rcode = rcode;
	devrpt->mdr_off = off;

	if (fmt) {
		va_start(ap, fmt);
		vsnprintf(devrpt->mdr_msg, sizeof(devrpt->mdr_msg), fmt, ap);
		va_end(ap);
	}
}

struct mpool_descriptor *mpool_user_desc_alloc(char *mpname)
{
	struct mpool_descriptor    *mp;

	if (!mpname || !(*mpname))
		return NULL;

	mp = calloc(1, sizeof(*mp));
	if (!mp)
		return NULL;

	strlcpy(mp->pds_name, mpname, sizeof(mp->pds_name));

	return mp;
}

void mpool_user_desc_free(struct mpool_descriptor *mp)
{
	free(mp);
}
