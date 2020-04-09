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

#define OP_COMMIT      0
#define OP_DELETE      1

/**
 * mdc_acquire() - Validate mdc handle and acquire mdc_lock
 *
 * @mlh: MDC handle
 * @rw:  read/append?
 */
static inline merr_t
mdc_acquire(struct mpool_mdc *mdc, bool rw)
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
static inline void
mdc_release(struct mpool_mdc *mdc, bool rw)
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
static inline void
mdc_invalidate(struct mpool_mdc *mdc)
{
	mdc->mdc_magic = MPC_NO_MAGIC;
}

/**
 * mdc_get_mpname() - Get mpool name from MDC context or dataset handle
 *
 * @ds:     dataset handle
 * @mpname: buffer to store the mpool name (output)
 * @mplen:  buffer len
 */
static merr_t
mdc_get_mpname(
	struct mpool           *ds,
	char                   *mpname,
	size_t                  mplen)
{
	if (!ds || !mpname)
		return merr(EINVAL);

	mp_ds_mpname(ds, mpname, mplen);

	return 0;
}

/**
 * mdc_find_get() - Wrapper around get for mlog pair.
 */
static void
mdc_find_get(
	struct mpool            *ds,
	u64                     *logid,
	bool                     do_put,
	struct mlog_props       *props,
	struct mpool_mlog      **mlh,
	merr_t                  *ferr)
{
	merr_t err;
	int    i;

	for (i = 0; i < 2; i++) {
		ferr[i] = 0;
		err = mpool_mlog_find_get(ds, logid[i], &props[i], &mlh[i]);
		if (err)
			ferr[i] = err;
	}

	if (do_put && ((ferr[0] && !ferr[1]) || (ferr[1] && !ferr[0]))) {
		if (ferr[0])
			mpool_mlog_put(ds, mlh[1]);
		else
			mpool_mlog_put(ds, mlh[0]);
	}
}

/**
 * mdc_resolve() - Wrapper around resolve for mlog pair.
 */
static void
mdc_resolve(
	struct mpool            *ds,
	u64                     *logid,
	struct mlog_props       *props,
	struct mpool_mlog      **mlh,
	merr_t                  *ferr)
{
	merr_t err;
	int    i;

	for (i = 0; i < 2; i++) {
		ferr[i] = 0;
		err = mpool_mlog_resolve(ds, logid[i], &props[i], &mlh[i]);
		if (err)
			ferr[i] = err;
	}
}


/**
 * mdc_put() - Wrapper around put for mlog pair.
 */
static merr_t
mdc_put(
	struct mpool       *ds,
	struct mpool_mlog  *mlh1,
	struct mpool_mlog  *mlh2)
{
	merr_t rval = 0;
	merr_t err;

	err = mpool_mlog_put(ds, mlh1);
	if (err)
		rval = err;

	err = mpool_mlog_put(ds, mlh2);
	if (err)
		rval = err;

	return rval;
}

uint64_t
mpool_mdc_alloc(
	struct mpool               *ds,
	u64                        *logid1,
	u64                        *logid2,
	enum mp_media_classp        mclassp,
	const struct mdc_capacity  *capreq,
	struct mdc_props           *props)
{
	struct mlog_capacity    mlcap;
	struct mlog_props       mlprops;
	struct mpool_mlog      *mlh[2];

	merr_t err;
	merr_t err2;
	bool   beffort;

	if (!ds || !logid1 || !logid2 || !capreq)
		return merr(EINVAL);

	if (!mpool_mc_isvalid(mclassp))
		return merr(EINVAL);

	memset(&mlcap, 0, sizeof(mlcap));
	mlcap.lcp_captgt = capreq->mdt_captgt;
	mlcap.lcp_spare  = capreq->mdt_spare;

	beffort = mpool_mc_isbe(mclassp);
	mclassp = mpool_mc_first_get(mclassp);
	assert(mclassp < MP_MED_NUMBER);

	err2 = 0;
	do {
		if (err2 && ++mclassp >= MP_MED_NUMBER)
			return err2;

		err = mpool_mlog_alloc(ds, &mlcap, mclassp, &mlprops, &mlh[0]);
		if (err && (!beffort || (merr_errno(err) != ENOENT &&
					 merr_errno(err) != ENOSPC)))
			return err;

		err2 = err;
		if (!err)
			*logid1 = mlprops.lpr_objid;
		else
			continue;

		err = mpool_mlog_alloc(ds, &mlcap, mclassp, &mlprops, &mlh[1]);
		if (err)
			mpool_mlog_abort(ds, mlh[0]);

		if (err && (!beffort || (merr_errno(err) != ENOENT &&
					 merr_errno(err) != ENOSPC)))
			return err;

		err2 = err;
		if (!err) {
			*logid2 = mlprops.lpr_objid;
			break;
		}
	} while (beffort);

	if (props) {
		props->mdc_objid1    = *logid1;
		props->mdc_objid2    = *logid2;
		props->mdc_alloc_cap = mlprops.lpr_alloc_cap;
		props->mdc_mclassp   = mclassp;
	}

	return 0;
}

uint64_t
mpool_mdc_commit(
	struct mpool           *ds,
	u64                     logid1,
	u64                     logid2)
{
	struct mpool_mlog  *mlh[2];
	struct mlog_props   props[2];

	merr_t err;
	char   mpname[MPOOL_NAME_LEN_MAX];
	u64    id[2];
	merr_t ferr[2] = {0};

	if (!ds)
		return merr(EINVAL);

	mdc_get_mpname(ds, mpname, sizeof(mpname));

	/* We already have the reference from alloc */
	id[0] = logid1;
	id[1] = logid2;
	mdc_resolve(ds, id, props, mlh, ferr);
	if (ferr[0] || ferr[1])
		return ferr[0] ? : ferr[1];

	err = mpool_mlog_commit(ds, mlh[0]);
	if (err) {
		mpool_mlog_abort(ds, mlh[0]);
		mpool_mlog_abort(ds, mlh[1]);

		return err;
	}

	err = mpool_mlog_commit(ds, mlh[1]);
	if (err) {
		mpool_mlog_delete(ds, mlh[0]);
		mpool_mlog_abort(ds, mlh[1]);

		return err;
	}

	/*
	 * Now drop the alloc reference. The calls that follow will need
	 * a get until an mdc handle is established by mpool_mdc_open(). This
	 * comes from the API limitation that MDC commit and destroy operate
	 * on object IDs and not on handles. This will be cleaned in near
	 * future.
	 */
	return mdc_put(ds, mlh[0], mlh[1]);
}

uint64_t
mpool_mdc_destroy(
	struct mpool           *ds,
	u64                     logid1,
	u64                     logid2)
{
	struct mpool_mlog  *mlh[2];
	struct mlog_props   props[2];

	char   mpname[MPOOL_NAME_LEN_MAX];
	int    i;
	u64    id[2];
	merr_t ferr[2] = {0};
	merr_t rval = 0;

	if (!ds)
		return merr(EINVAL);

	mdc_get_mpname(ds, mpname, sizeof(mpname));

	/*
	 * This mdc_find_get can go away once mpool_mdc_destroy is modified to
	 * operate on handles.
	 */
	id[0] = logid1;
	id[1] = logid2;
	mdc_find_get(ds, id, false, props, mlh, ferr);

	/*
	 * If mdc_find_get encountered an error for both mlogs, then return
	 * the non-ENOENT merr first.
	 */
	if (ferr[0] && ferr[1])
		return (merr_errno(ferr[0]) != ENOENT) ? ferr[0] : ferr[1];

	/*
	 * Delete uses the ref from get irrespective of whether it's called
	 * from alloc thread's context or post crash. This works today as we
	 * drop the alloc reference explicitly, post commit.
	 */
	for (i = 0; i < 2; i++) {
		merr_t err;

		if (ferr[i])
			continue;

		if (props[i].lpr_iscommitted)
			err = mpool_mlog_delete(ds, mlh[i]);
		else
			err = mpool_mlog_abort(ds, mlh[i]);

		if (err)
			rval = err;
	}

	/*
	 * If mdc_find_get encountered an error for either mlogs, then return
	 * that error.
	 */
	if (ferr[0] || ferr[1])
		return ferr[0] ?: ferr[1];

	return rval;
}

uint64_t
mpool_mdc_open(
	struct mpool            *ds,
	u64                      logid1,
	u64                      logid2,
	u8                       flags,
	struct mpool_mdc       **mdc_out)
{
	struct mlog_props   props[2];
	struct mpool_mlog  *mlh[2];
	struct mpool_mdc      *mdc;

	merr_t  err = 0, err1 = 0, err2 = 0;
	u64     gen1 = 0, gen2 = 0;
	merr_t  ferr[2] = {0};
	bool    empty = false;
	u8      mlflags = 0;
	u64     id[2];
	char   *mpname;

	if (!ds || !mdc_out)
		return merr(EINVAL);

	mdc = kzalloc(sizeof(*mdc), GFP_KERNEL);
	if (!mdc)
		return merr(ENOMEM);

	mdc->mdc_valid = 0;
	mdc->mdc_ds    = ds;
	mdc_get_mpname(ds, mdc->mdc_mpname, sizeof(mdc->mdc_mpname));

	mpname = mdc->mdc_mpname;

	if (logid1 == logid2) {
		err = merr(EINVAL);
		goto exit;
	}

	/*
	 * This mdc_find_get can go away once mpool_mdc_open is modified to
	 * operate on handles.
	 */
	id[0] = logid1;
	id[1] = logid2;
	mdc_find_get(ds, id, true, props, mlh, ferr);
	if (ferr[0] || ferr[1]) {
		err = ferr[0] ? : ferr[1];
		goto exit;
	}
	mdc->mdc_logh1 = mlh[0];
	mdc->mdc_logh2 = mlh[1];

	if (flags & MDC_OF_SKIP_SER)
		mlflags |= MLOG_OF_SKIP_SER;

	mlflags |= MLOG_OF_COMPACT_SEM;

	err1 = mpool_mlog_open(ds, mdc->mdc_logh1, mlflags, &gen1);
	err2 = mpool_mlog_open(ds, mdc->mdc_logh2, mlflags, &gen2);

	if (err1 && merr_errno(err1) != EMSGSIZE &&
	    merr_errno(err1) != EBUSY) {
		err = err1;
	} else if (err2 && merr_errno(err2) != EMSGSIZE &&
		   merr_errno(err2) != EBUSY) {
		err = err2;
	} else if ((err1 && err2) ||
			(!err1 && !err2 && gen1 && gen1 == gen2)) {

		err = merr(EINVAL);

		/*
		 * bad pair; both have failed erases/compactions or equal
		 * non-0 gens
		 */
		mp_pr_err(
			"mpool %s, mdc open, bad mlog handle, mlog1 %p logid1 0x%lx errno %d gen1 %lu, mlog2 %p logid2 0x%lx errno %d gen2 %lu",
			err, mpname, mdc->mdc_logh1, (ulong)logid1,
			merr_errno(err1), (ulong)gen1, mdc->mdc_logh2,
			(ulong)logid2, merr_errno(err2), (ulong)gen2);
	} else {
		/* active log is valid log with smallest gen */
		if (err1 || (!err2 && gen2 < gen1)) {
			mdc->mdc_alogh = mdc->mdc_logh2;
			if (!err1) {
				err = mpool_mlog_empty(ds, mdc->mdc_logh1,
						       &empty);
				if (err)
					mdc_logerr(mpname,
						   "mlog1 empty check failed",
						   mdc->mdc_logh1, logid1,
						   gen1, gen2, err);
			}
			if (!err && (err1 || !empty)) {
				err = mpool_mlog_erase(ds, mdc->mdc_logh1,
						    gen2 + 1);
				if (!err) {
					err = mpool_mlog_open(ds,
							      mdc->mdc_logh1,
							      mlflags, &gen1);
					if (err)
						mdc_logerr(mpname,
						"mlog1 open failed",
						mdc->mdc_logh1,
						logid1, gen1, gen2, err);
				} else {
					mdc_logerr(mpname,
						"mlog1 erase failed",
						mdc->mdc_logh1,
						logid1, gen1, gen2, err);
				}
			}
		} else {
			mdc->mdc_alogh = mdc->mdc_logh1;
			if (!err2) {
				err = mpool_mlog_empty(ds, mdc->mdc_logh2,
						       &empty);
				if (err)
					mdc_logerr(mpname,
						   "mlog2 empty check failed",
						   mdc->mdc_logh2, logid2,
						   gen1, gen2, err);
			}
			if (!err && (err2 || gen2 == gen1 || !empty)) {
				err = mpool_mlog_erase(ds, mdc->mdc_logh2,
						    gen1 + 1);
				if (!err) {
					err = mpool_mlog_open(ds,
							      mdc->mdc_logh2,
							      mlflags, &gen2);
					if (err)
						mdc_logerr(mpname,
						"mlog2 open failed",
						mdc->mdc_logh2,
						logid2, gen1, gen2, err);
				} else {
					mdc_logerr(mpname,
						   "mlog2 erase failed",
						   mdc->mdc_logh2,
						   logid2, gen1, gen2, err);
				}
			}
		}

		if (!err) {
			err = mpool_mlog_empty(ds, mdc->mdc_alogh, &empty);
			if (!err && empty) {
				/*
				 * first use of log pair so need to add
				 * cstart/cend recs; above handles case of
				 * failure between adding cstart and cend
				 */
				err = mpool_mlog_append_cstart(ds,
							       mdc->mdc_alogh);
				if (!err) {
					err = mpool_mlog_append_cend(
						ds, mdc->mdc_alogh);
					if (err)
						mdc_logerr(mpname,
							   "adding cend to active mlog failed",
							   mdc->mdc_alogh,
							   mdc->mdc_alogh ==
							   mdc->mdc_logh1 ?
							   logid1 : logid2,
							   gen1, gen2, err);
				} else {
					mdc_logerr(mpname,
						   "adding cstart to active mlog failed",
						   mdc->mdc_alogh,
						   mdc->mdc_alogh ==
						   mdc->mdc_logh1 ? logid1 :
						   logid2, gen1, gen2, err);
				}

			} else if (err) {
				mdc_logerr(mpname,
					   "active mlog empty check failed",
					   mdc->mdc_alogh,
					   mdc->mdc_alogh == mdc->mdc_logh1 ?
					   logid1 : logid2, gen1, gen2, err);
			}
		}
	}

	if (!err) {
		mdc->mdc_valid = 1;
		mdc->mdc_magic = MPC_MDC_MAGIC;
		mdc->mdc_flags = flags;
		mutex_init(&mdc->mdc_lock);

		*mdc_out = mdc;
	} else {
		err1 = mpool_mlog_close(ds, mdc->mdc_logh1);
		err2 = mpool_mlog_close(ds, mdc->mdc_logh2);

		mdc_put(ds, mdc->mdc_logh1, mdc->mdc_logh2);
	}

exit:
	if (err)
		kfree(mdc);

	return err;
}

uint64_t
mpool_mdc_cstart(struct mpool_mdc *mdc)
{
	struct mpool       *ds;
	struct mpool_mlog  *tgth = NULL;

	merr_t err;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	ds = mdc->mdc_ds;

	if (mdc->mdc_alogh == mdc->mdc_logh1)
		tgth = mdc->mdc_logh2;
	else
		tgth = mdc->mdc_logh1;

	err = mpool_mlog_append_cstart(ds, tgth);
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

uint64_t
mpool_mdc_cend(struct mpool_mdc *mdc)
{
	struct mpool       *ds;
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

	ds = mdc->mdc_ds;

	if (mdc->mdc_alogh == mdc->mdc_logh1) {
		tgth = mdc->mdc_logh1;
		srch = mdc->mdc_logh2;
	} else {
		tgth = mdc->mdc_logh2;
		srch = mdc->mdc_logh1;
	}

	err = mpool_mlog_append_cend(ds, tgth);
	if (!err) {
		err = mpool_mlog_gen(ds, tgth, &gentgt);
		if (!err)
			err = mpool_mlog_erase(ds, srch, gentgt + 1);
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

uint64_t
mpool_mdc_close(struct mpool_mdc *mdc)
{
	struct mpool   *ds;

	merr_t err = 0;
	merr_t rval = 0;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	ds = mdc->mdc_ds;

	mdc->mdc_valid = 0;

	err = mpool_mlog_close(ds, mdc->mdc_logh1);
	if (err) {
		mp_pr_err("mpool %s, mdc %p close failed, mlog1 %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_logh1);
		rval = err;
	}

	err = mpool_mlog_close(ds, mdc->mdc_logh2);
	if (err) {
		mp_pr_err("mpool %s, mdc %p close failed, mlog2 %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_logh2);
		rval = err;
	}

	/*
	 * This mdc_put can go away once mpool_mdc_open is modified to
	 * operate on handles.
	 */
	err = mdc_put(ds, mdc->mdc_logh1, mdc->mdc_logh2);
	if (err) {
		mdc_release(mdc, rw);
		return err;
	}

	mdc_invalidate(mdc);
	mdc_release(mdc, false);

	kfree(mdc);

	return rval;
}

uint64_t
mpool_mdc_sync(struct mpool_mdc *mdc)
{
	merr_t err;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	err = mpool_mlog_flush(mdc->mdc_ds, mdc->mdc_alogh);
	if (err)
		mp_pr_err("mpool %s, mdc %p sync failed, mlog %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh);

	mdc_release(mdc, rw);

	return err;
}

uint64_t
mpool_mdc_rewind(struct mpool_mdc *mdc)
{
	merr_t err;
	bool   rw = false;

	if (!mdc)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	err = mpool_mlog_read_data_init(mdc->mdc_ds, mdc->mdc_alogh);
	if (err)
		mp_pr_err("mpool %s, mdc %p rewind failed, mlog %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh);

	mdc_release(mdc, rw);

	return err;
}

uint64_t
mpool_mdc_read(
	struct mpool_mdc      *mdc,
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

	err = mpool_mlog_read_data_next(mdc->mdc_ds, mdc->mdc_alogh, data,
				     len, rdlen);
	if (err && (merr_errno(err) != EOVERFLOW))
		mp_pr_err("mpool %s, mdc %p read failed, mlog %p len %lu",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh, len);

	mdc_release(mdc, rw);

	return err;
}

uint64_t
mpool_mdc_append(
	struct mpool_mdc      *mdc,
	void               *data,
	ssize_t             len,
	bool                sync)
{
	merr_t err;
	bool   rw = true;

	if (!mdc || !data)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	err = mpool_mlog_append_data(mdc->mdc_ds, mdc->mdc_alogh, data, len,
				     sync);
	if (err)
		mp_pr_err("mpool %s, mdc %p append failed, mlog %p, len %lu sync %d",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh, len, sync);

	mdc_release(mdc, rw);

	return err;
}

uint64_t
mpool_mdc_usage(struct mpool_mdc *mdc, size_t *usage)
{
	merr_t err;
	bool   rw = false;

	if (!mdc || !usage)
		return merr(EINVAL);

	err = mdc_acquire(mdc, rw);
	if (err)
		return err;

	err = mpool_mlog_len(mdc->mdc_ds, mdc->mdc_alogh, usage);
	if (err)
		mp_pr_err("mpool %s, mdc %p usage failed, mlog %p",
			  err, mdc->mdc_mpname, mdc, mdc->mdc_alogh);

	mdc_release(mdc, rw);

	return err;
}

uint64_t
mpool_mdc_get_root(struct mpool *ds, u64 *oid1, u64 *oid2)
{
	struct mp_props    props;
	merr_t             err;

	if (!ds || !oid1 || !oid2)
		return merr(EINVAL);

	err = mpool_props_get(ds, &props, NULL);
	if (!err) {
		*oid1 = props.mp_oidv[0];
		*oid2 = props.mp_oidv[1];
	}

	return err;
}
