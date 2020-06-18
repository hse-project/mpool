// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */
/*
 * Mlog design pattern module.
 *
 * Defines convenience functions implementing design patterns on top of the
 * mlog API.  Does not assume any internal knowledge of mlog implementation.
 *
 */

#include <util/string.h>
#include <util/alloc.h>

#include <mpctl/imdc.h>
#include <mpctl/imlog.h>
#include <mpctl/ids.h>
#include <mpool/mpool.h>
#include <mpcore/mpcore.h>
#include <mpcore/mlog.h>

#include "logging.h"

#define mdc_logerr(_mpname, _msg, _mlh, _objid, _gen1, _gen2, _err)     \
	mp_pr_err("mpool %s, mdc open, %s "			        \
		  "mlog %p objid 0x%lx gen1 %lu gen2 %lu",		\
		  (_err), (_mpname), (_msg),		                \
		  (_mlh), (ulong)(_objid), (ulong)(_gen1),		\
		  (ulong)(_gen2))					\

/**
 * mdc_acquire() - Validate mdc handle and acquire mdc_lock
 *
 * @mlh: MDC handle
 * @rw:  read/append?
 */
static inline merr_t mdc_acquire(struct mpool_mdc *mdc, bool rw)
{
	if (!mdc || mdc->mdc_magic != MPC_MDC_MAGIC || !mdc->mdc_valid)
		return merr(EINVAL);

	if (rw && (mdc->mdc_flags & MDC_OF_SKIP_SER))
		return 0;

	/* Validate again after acquiring lock */
	mutex_lock(&mdc->mdc_lock);
	if (mdc->mdc_valid)
		return 0;

	mutex_unlock(&mdc->mdc_lock);

	return merr(EINVAL);
}

/**
 * mdc_release() - Release mdc_lock
 *
 * @mlh: MDC handle
 * @rw:  read/append?
 */
static inline void mdc_release(struct mpool_mdc *mdc, bool rw)
{
	if (rw && (mdc->mdc_flags & MDC_OF_SKIP_SER))
		return;

	mutex_unlock(&mdc->mdc_lock);
}

/**
 * mdc_invalidate() - Invalidates MDC handle by resetting the magic
 *
 * @mdc: MDC handle
 */
static inline void mdc_invalidate(struct mpool_mdc *mdc)
{
	mdc->mdc_magic = MPC_NO_MAGIC;
}

/**
 * mdc_mpname_get() - Get mpool name from MDC context or dataset handle
 *
 * @mp:     mpool handle
 * @mpname: buffer to store the mpool name (output)
 * @mplen:  buffer len
 */
static merr_t mdc_mpname_get(struct mpool *mp, char *mpname, size_t mplen)
{
	if (!mp || !mpname)
		return merr(EINVAL);

	mpool_name_get(mp, mpname, mplen);

	return 0;
}

uint64_t
mpool_mdc_alloc(
	struct mpool               *mp,
	u64                        *logid1,
	u64                        *logid2,
	enum mp_media_classp        mclassp,
	const struct mdc_capacity  *capreq,
	struct mdc_props           *props)
{
	struct mlog_capacity    mlcap;
	struct mlog_props       mlprops;

	merr_t err;

	if (!mp || !logid1 || !logid2 || !capreq)
		return merr(EINVAL);

	if (!mpool_mc_isvalid(mclassp))
		return merr(EINVAL);

	memset(&mlcap, 0, sizeof(mlcap));
	mlcap.lcp_captgt = capreq->mdt_captgt;
	mlcap.lcp_spare  = capreq->mdt_spare;

	err = mpool_mlog_alloc(mp, mclassp, &mlcap, logid1, &mlprops);
	if (err)
		return err;

	err = mpool_mlog_alloc(mp, mclassp, &mlcap, logid2, &mlprops);
	if (err) {
		mpool_mlog_abort(mp, *logid1);
		return err;
	}

	if (props) {
		props->mdc_objid1    = *logid1;
		props->mdc_objid2    = *logid2;
		props->mdc_alloc_cap = mlprops.lpr_alloc_cap;
		props->mdc_mclassp   = mclassp;
	}

	return 0;
}

uint64_t mpool_mdc_commit(struct mpool *mp, u64 logid1, u64 logid2)
{
	merr_t err;

	if (!mp)
		return merr(EINVAL);

	err = mpool_mlog_commit(mp, logid1);
	if (err) {
		mpool_mlog_abort(mp, logid1);
		mpool_mlog_abort(mp, logid2);

		return err;
	}

	err = mpool_mlog_commit(mp, logid2);
	if (err) {
		mpool_mlog_delete(mp, logid1);
		mpool_mlog_abort(mp, logid2);

		return err;
	}

	return 0;
}

uint64_t mpool_mdc_delete(struct mpool *mp, u64 logid1, u64 logid2)
{
	merr_t rval = 0, err;

	if (!mp)
		return merr(EINVAL);

	err = mpool_mlog_delete(mp, logid1);
	if (err) {
		mpool_mlog_abort(mp, logid1);
		rval = err;
	}

	err = mpool_mlog_delete(mp, logid2);
	if (err) {
		mpool_mlog_abort(mp, logid2);
		rval = err;
	}

	return rval;
}

uint64_t mpool_mdc_abort(struct mpool *mp, u64 logid1, u64 logid2)
{
	merr_t rval = 0, err;

	if (!mp)
		return merr(EINVAL);

	err = mpool_mlog_abort(mp, logid1);
	if (err)
		rval = err;

	err = mpool_mlog_abort(mp, logid2);
	if (err)
		rval = err;

	return rval;
}


uint64_t
mpool_mdc_open(
	struct mpool        *mp,
	u64                  logid1,
	u64                  logid2,
	u8                   flags,
	struct mpool_mdc   **mdc_out)
{
	struct mpool_mlog  *mlh[2] = {};
	struct mpool_mdc   *mdc;

	merr_t  err = 0, err1 = 0, err2 = 0;
	u64     gen1 = 0, gen2 = 0;
	bool    empty = false;
	u8      mlflags = 0;
	char   *mpname;

	if (!mp || !mdc_out)
		return merr(EINVAL);

	mdc = kzalloc(sizeof(*mdc), GFP_KERNEL);
	if (!mdc)
		return merr(ENOMEM);

	mdc->mdc_valid = 0;
	mdc->mdc_mp = mp;
	mdc_mpname_get(mp, mdc->mdc_mpname, sizeof(mdc->mdc_mpname));

	mpname = mdc->mdc_mpname;

	if (logid1 == logid2) {
		err = merr(EINVAL);
		goto exit;
	}

	if (flags & MDC_OF_SKIP_SER)
		mlflags |= MLOG_OF_SKIP_SER;

	mlflags |= MLOG_OF_COMPACT_SEM;

	err1 = mpool_mlog_open(mp, logid1, mlflags, &gen1, &mlh[0]);
	err2 = mpool_mlog_open(mp, logid2, mlflags, &gen2, &mlh[1]);

	if (err1 && merr_errno(err1) != EMSGSIZE && merr_errno(err1) != EBUSY) {
		err = err1;
	} else if (err2 && merr_errno(err2) != EMSGSIZE && merr_errno(err2) != EBUSY) {
		err = err2;
	} else if ((err1 && err2) || (!err1 && !err2 && gen1 && gen1 == gen2)) {
		err = merr(EINVAL);

		/*
		 * bad pair; both have failed erases/compactions or equal
		 * non-0 gens
		 */
		mp_pr_err(
			"mpool %s, mdc open, bad mlog handle, mlog1 %p logid1 0x%lx errno %d gen1 %lu, mlog2 %p logid2 0x%lx errno %d gen2 %lu",
			err, mpname, mlh[0], (ulong)logid1, merr_errno(err1), (ulong)gen1, mlh[1],
			(ulong)logid2, merr_errno(err2), (ulong)gen2);
	} else {
		/* active log is valid log with smallest gen */
		if (err1 || (!err2 && gen2 < gen1)) {
			mdc->mdc_alogh = mlh[1];
			if (!err1) {
				err = mpool_mlog_empty(mlh[0], &empty);
				if (err)
					mdc_logerr(mpname, "mlog1 empty check failed",
						   mlh[0], logid1, gen1, gen2, err);
			}
			if (!err && (err1 || !empty)) {
				if (err1) {
					err = mpool_mlog_erase_byoid(mp, logid1, gen2 + 1);
				} else {
					err = mpool_mlog_erase(mlh[0], gen2 + 1);
					if (!err)
						mpool_mlog_close(mlh[0]);
				}

				if (!err) {
					err = mpool_mlog_open(mp, logid1, mlflags, &gen1, &mlh[0]);
					if (err)
						mdc_logerr(mpname, "mlog1 open failed",
							   mlh[0], logid1, gen1, gen2, err);
				} else {
					mdc_logerr(mpname, "mlog1 erase failed",
						   mlh[0], logid1, gen1, gen2, err);
				}
			}
		} else {
			mdc->mdc_alogh = mlh[0];
			if (!err2) {
				err = mpool_mlog_empty(mlh[1], &empty);
				if (err)
					mdc_logerr(mpname, "mlog2 empty check failed",
						   mlh[1], logid2, gen1, gen2, err);
			}
			if (!err && (err2 || gen2 == gen1 || !empty)) {
				if (err2) {
					err = mpool_mlog_erase_byoid(mp, logid2, gen1 + 1);
				} else {
					err = mpool_mlog_erase(mlh[1], gen1 + 1);
					if (!err)
						mpool_mlog_close(mlh[1]);
				}

				if (!err) {
					err = mpool_mlog_open(mp, logid2, mlflags, &gen2, &mlh[1]);
					if (err)
						mdc_logerr(mpname, "mlog2 open failed",
							   mlh[1], logid2, gen1, gen2, err);
				} else {
					mdc_logerr(mpname, "mlog2 erase failed",
						   mlh[1], logid2, gen1, gen2, err);
				}
			}
		}

		if (!err) {
			err = mpool_mlog_empty(mdc->mdc_alogh, &empty);
			if (!err && empty) {
				/*
				 * first use of log pair so need to add
				 * cstart/cend recs; above handles case of
				 * failure between adding cstart and cend
				 */
				err = mpool_mlog_append_cstart(mdc->mdc_alogh);
				if (!err) {
					err = mpool_mlog_append_cend(mdc->mdc_alogh);
					if (err)
						mdc_logerr(mpname,
							   "adding cend to active mlog failed",
							   mdc->mdc_alogh,
							   mdc->mdc_alogh == mlh[0] ?
							   logid1 : logid2, gen1, gen2, err);
				} else {
					mdc_logerr(mpname,
						   "adding cstart to active mlog failed",
						   mdc->mdc_alogh, mdc->mdc_alogh == mlh[0] ?
						   logid1 : logid2, gen1, gen2, err);
				}

			} else if (err) {
				mdc_logerr(mpname, "active mlog empty check failed",
					   mdc->mdc_alogh, mdc->mdc_alogh == mlh[0] ?
					   logid1 : logid2, gen1, gen2, err);
			}
		}
	}

	if (!err) {
		mdc->mdc_logh1 = mlh[0];
		mdc->mdc_logh2 = mlh[1];
		mdc->mdc_valid = 1;
		mdc->mdc_magic = MPC_MDC_MAGIC;
		mdc->mdc_flags = flags;
		mutex_init(&mdc->mdc_lock);

		*mdc_out = mdc;
	} else {
		if (mlh[0])
			err1 = mpool_mlog_close(mlh[0]);
		if (mlh[1])
			err2 = mpool_mlog_close(mlh[1]);
	}

exit:
	if (err)
		kfree(mdc);

	return err;
}

uint64_t mpool_mdc_cstart(struct mpool_mdc *mdc)
{
	struct mpool_mlog  *tgth = NULL;

	merr_t err;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	if (mdc->mdc_alogh == mdc->mdc_logh1)
		tgth = mdc->mdc_logh2;
	else
		tgth = mdc->mdc_logh1;

	err = mpool_mlog_append_cstart(tgth);
	if (!err) {
		mdc->mdc_alogh = tgth;
	} else {
		mdc_release(mdc, rw);

		mp_pr_err("mpool %s, mdc %p cstart failed, mlog %p",
			  err, mdc->mdc_mpname, mdc, tgth);

		mpool_mdc_close(mdc);

		return err;
	}

	mdc_release(mdc, rw);

	return err;
}

uint64_t mpool_mdc_cend(struct mpool_mdc *mdc)
{
	struct mpool_mlog  *srch = NULL;
	struct mpool_mlog  *tgth = NULL;

	merr_t err;
	u64    gentgt = 0;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	if (mdc->mdc_alogh == mdc->mdc_logh1) {
		tgth = mdc->mdc_logh1;
		srch = mdc->mdc_logh2;
	} else {
		tgth = mdc->mdc_logh2;
		srch = mdc->mdc_logh1;
	}

	err = mpool_mlog_append_cend(tgth);
	if (!err) {
		err = mpool_mlog_gen(tgth, &gentgt);
		if (!err)
			err = mpool_mlog_erase(srch, gentgt + 1);
	}

	if (err) {
		mdc_release(mdc, rw);

		mp_pr_err("mpool %s, mdc %p cend failed, mlog %p",
			  err, mdc->mdc_mpname, mdc, tgth);

		mpool_mdc_close(mdc);

		return err;
	}

	mdc_release(mdc, rw);

	return err;
}

uint64_t mpool_mdc_close(struct mpool_mdc *mdc)
{
	merr_t err = 0;
	merr_t rval = 0;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	mdc->mdc_valid = 0;

	err = mpool_mlog_close(mdc->mdc_logh1);
	if (err) {
		mp_pr_err("mpool %s, mdc %p close failed, mlog1 %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_logh1);
		rval = err;
	}

	err = mpool_mlog_close(mdc->mdc_logh2);
	if (err) {
		mp_pr_err("mpool %s, mdc %p close failed, mlog2 %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_logh2);
		rval = err;
	}

	mdc_invalidate(mdc);
	mdc_release(mdc, false);

	kfree(mdc);

	return rval;
}

uint64_t mpool_mdc_sync(struct mpool_mdc *mdc)
{
	merr_t err;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	err = mpool_mlog_sync(mdc->mdc_alogh);
	if (err)
		mp_pr_err("mpool %s, mdc %p sync failed, mlog %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh);

	mdc_release(mdc, rw);

	return err;
}

uint64_t mpool_mdc_rewind(struct mpool_mdc *mdc)
{
	merr_t err;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	err = mpool_mlog_rewind(mdc->mdc_alogh);
	if (err)
		mp_pr_err("mpool %s, mdc %p rewind failed, mlog %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh);

	mdc_release(mdc, rw);

	return err;
}

uint64_t
mpool_mdc_read(
	struct mpool_mdc   *mdc,
	void               *data,
	size_t              len,
	size_t             *rdlen)
{
	merr_t err;
	bool   rw = true;

	if (!mdc || !data)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	err = mpool_mlog_read(mdc->mdc_alogh, data, len, rdlen);
	if (err && (merr_errno(err) != EOVERFLOW))
		mp_pr_err("mpool %s, mdc %p read failed, mlog %p len %lu",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh, len);

	mdc_release(mdc, rw);

	return err;
}

uint64_t
mpool_mdc_append(
	struct mpool_mdc   *mdc,
	void               *data,
	ssize_t             len,
	bool                sync)
{
	struct iovec iov;
	merr_t       err;
	bool         rw = true;

	if (!mdc || !data)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	iov.iov_base = data;
	iov.iov_len = len;

	err = mpool_mlog_append(mdc->mdc_alogh, &iov, len, sync);
	if (err)
		mp_pr_err("mpool %s, mdc %p append failed, mlog %p, len %lu sync %d",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh, len, sync);

	mdc_release(mdc, rw);

	return err;
}

uint64_t mpool_mdc_usage(struct mpool_mdc *mdc, size_t *usage)
{
	merr_t err;
	bool   rw = false;

	if (!mdc || !usage)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	err = mpool_mlog_len(mdc->mdc_alogh, usage);
	if (err)
		mp_pr_err("mpool %s, mdc %p usage failed, mlog %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh);

	mdc_release(mdc, rw);

	return err;
}

uint64_t mpool_mdc_get_root(struct mpool *mp, u64 *oid1, u64 *oid2)
{
	struct mpool_params    params;
	merr_t                 err;

	if (!mp || !oid1 || !oid2)
		return merr(EINVAL);

	err = mpool_params_get(mp, &params, NULL);
	if (!err) {
		*oid1 = params.mp_oidv[0];
		*oid2 = params.mp_oidv[1];
	}

	return err;
}
