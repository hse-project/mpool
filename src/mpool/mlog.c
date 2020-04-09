// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */
/*
 * Mlog module.
 *
 * Defines functions for writing, reading, and managing the lifecycle of mlogs.
 */

#include <util/page.h>
#include <util/minmax.h>
#include <util/log2.h>

#include <mpool/mpool.h>

#include <mpctl/imlog.h>

#include "mpcore_defs.h"
#include "logging.h"

/**
 * Force 4K-alignment by default for 512B sectors. Having it as a non-static
 * global so that we can unit test it.
 */
bool mlog_force_4ka = true;

/**
 * pmd_obj_rdlock() - Read-lock object layout with appropriate nesting level.
 * @mp:
 * @layout:
 */
static inline void
pmd_obj_rdlock(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout)
{
	down_read(&layout->eld_rwlock);
}

/**
 * pmd_obj_rdunlock() - Release read lock on object layout.
 * @mp:
 * @layout:
 */
static inline void
pmd_obj_rdunlock(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout)
{
	up_read(&layout->eld_rwlock);
}

/**
 * pmd_obj_wrlock() - Write-lock object layout with appropriate nesting level.
 * @mp:
 * @layout:
 */
static inline void
pmd_obj_wrlock(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout)
{
	down_read(&layout->eld_rwlock);
}

/**
 * pmd_obj_wrunlock() - Release write lock on object layout.
 * @mp:
 * @layout:
 */
static inline void
pmd_obj_wrunlock(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout)
{
	up_write(&layout->eld_rwlock);
}

/**
 * ecio_user_layout_alloc() - Allocate a minimal layout descriptor for
 * user space mlogs support
 *
 * @mp:
 * @objid:
 * @gen:
 */
static struct ecio_layout_descriptor *
ecio_user_layout_alloc(
	struct mpool_descriptor    *mp,
	struct mpool_uuid            *uuid,
	u64                         objid,
	u64                         gen)
{
	struct ecio_layout_descriptor  *layout;

	layout = kzalloc(sizeof(*layout), GFP_KERNEL);
	if (!layout)
		return NULL;

	assert(objid_type(objid) == OMF_OBJ_MLOG);

	layout->eld_mlo = kzalloc(sizeof(*layout->eld_mlo), GFP_KERNEL);
	if (!layout->eld_mlo) {
		kfree(layout);
		return NULL;
	}

	layout->eld_mlo->mlo_layout = layout;
	layout->eld_objid = objid;
	layout->eld_gen   = gen;
	layout->eld_state = ECIO_LYT_NONE;

	mpool_uuid_copy(&layout->eld_uuid, uuid);

	return layout;
}

/**
 * ecio_user_layout_free() - Free the layout descriptor used for user-space
 * mlogs
 *
 * @layout:
 */
static void ecio_user_layout_free(struct ecio_layout_descriptor *layout)
{
	struct ecio_layout_mlo *mlo;

	if (!layout)
		return;

	mlo = layout->eld_mlo;
	assert(pmd_objid_type(layout->eld_objid) == OMF_OBJ_MLOG);
	assert(mlo != NULL);

	if (mlo->mlo_lstat)
		mp_pr_warn("eld_lstat object %p not freed properly", mlo);

	kfree(mlo);
	kfree(layout);
}

/**
 * ecio_user_layout_set() - Set the generation and state in the layout. Used
 * only for user space mlog support
 *
 * @layout:
 * @gen:
 * @state:
 *
 * Caller must hold write lock on the layout
 */
static merr_t
ecio_user_layout_set(struct ecio_layout_descriptor *layout, u64 gen, u8 state)
{
	if (!layout)
		return merr(EINVAL);

	layout->eld_gen   = gen;
	layout->eld_state = state;

	return 0;
}

/**
 * mlog2layout() - convert opaque mlog handle to ecio_layout_descriptor
 *
 * This function converts the opaque handle (mlog_descriptor) used by
 * clients to the internal representation (ecio_layout_descriptor).  The
 * conversion is a simple cast, followed by a sanity check to verify the
 * layout object is an mlog object.  If the validation fails, a NULL
 * pointer is returned.
 */
static struct ecio_layout_descriptor *mlog2layout(struct mlog_descriptor *mlh)
{
	bool                            ret;
	struct ecio_layout_descriptor  *layout =
	       (struct ecio_layout_descriptor *)mlh;

	ret = mlog_objid(layout->eld_objid);

	return ret ? layout : NULL;
}

/**
 * layout2mlog() - convert ecio_layout_descriptor to opaque mlog_descriptor
 *
 * This function converts the internally used ecio_layout_descriptor to
 * the externally used opaque mlog_descriptor.
 */
static struct mlog_descriptor *
layout2mlog(struct ecio_layout_descriptor *layout)
{
	return (struct mlog_descriptor *)layout;
}

/*
 * mlog API functions
 */

/*
 * Error codes: all mlog fns can return one or more of the below error embedded
 * in merr_t:
 * EINVAL = invalid fn args
 * ENOENT = log not open or logid not found
 * EFBIG = log full
 * EMSGSIZE = cstart w/o cend indicating a crash during compaction
 * ENODATA = malformed or corrupted log
 * EIO = unable to read/write log on media
 * ENOMEM = insufficient room in copy-out buffer
 * EBUSY = log is in erasing state; wait or retry erase
 */

/**
 * mlog_free_abuf() - Free log pages in the append buffer, range:[start, end].
 *
 * @lstat: mlog_stat
 * @start: start log page index, inclusive
 * @end:   end log page index, inclusive
 */
static void mlog_free_abuf(struct mlog_stat *lstat, int start, int end)
{
	int i;

	for (i = start; i <= end; i++) {
		if (lstat->lst_abuf[i]) {
			free_page((unsigned long)lstat->lst_abuf[i]);
			lstat->lst_abuf[i] = NULL;
		}
	}
}

/**
 * mlog_free_rbuf() - Free log pages in the read buffer, range:[start, end].
 *
 * @lstat: mlog_stat
 * @start: start log page index, inclusive
 * @end:   end log page index, inclusive
 */
static void mlog_free_rbuf(struct mlog_stat *lstat, int start, int end)
{
	int i;

	for (i = start; i <= end; i++) {
		if (lstat->lst_rbuf[i]) {
			free_page((unsigned long)lstat->lst_rbuf[i]);
			lstat->lst_rbuf[i] = NULL;
		}
	}
}

/**
 * mlog_init_fsetparms() - Initialize frequently used mlog & flush set
 * parameters.
 *
 * @mp:     mpool descriptor
 * @layout: layout descriptor
 * @mfp:    fset parameters (output)
 */
static void
mlog_init_fsetparms(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	struct mlog_fsetparms      *mfp)
{
	struct ecio_layout_descriptor  *layout;
	struct mlog_user               *mluser;

	u8     secshift;
	u16    sectsz;

	layout = mlog2layout(mlh);
	assert(layout);

	mluser = layout->eld_mlo->mlo_mlog;
	secshift        = mluser->ml_secshift;
	mfp->mfp_totsec = mluser->ml_totsec;

	sectsz = 1 << secshift;
	assert((sectsz == PAGE_SIZE) || (sectsz == 512));

	mfp->mfp_sectsz  = sectsz;
	mfp->mfp_lpgsz   = PAGE_SIZE;
	mfp->mfp_secpga  = IS_ALIGNED(mfp->mfp_sectsz, mfp->mfp_lpgsz);
	mfp->mfp_nlpgmb  = MB >> PAGE_SHIFT;
	mfp->mfp_nsecmb  = MB >> secshift;
	mfp->mfp_nseclpg = mfp->mfp_lpgsz >> secshift;
}

/**
 * mlog_extract_fsetparms() - Helper to extract flush set parameters.
 *
 * @lstat:   mlog stat
 * @sectsz:  sector size
 * @totsec:  total number of sectors in the mlog
 * @nsecmb:  number of sectors in 1 MiB
 * @nseclpg: number of sectors in a log page
 */
static inline void
mlog_extract_fsetparms(
	struct mlog_stat   *lstat,
	u16                *sectsz,
	u32                *totsec,
	u16                *nsecmb,
	u8                 *nseclpg)
{
	if (sectsz)
		*sectsz  = MLOG_SECSZ(lstat);
	if (totsec)
		*totsec  = MLOG_TOTSEC(lstat);
	if (nsecmb)
		*nsecmb  = MLOG_NSECMB(lstat);
	if (nseclpg)
		*nseclpg = MLOG_NSECLPG(lstat);
}

/**
 * mlog_stat_free()
 *
 * Deallocate log stat struct for mlog layout (if any).
 */
static void mlog_stat_free(struct ecio_layout_descriptor *layout)
{
	struct mlog_stat *lstat;

	lstat = layout->eld_lstat;

	if (!lstat) {
		mp_pr_warn("mlog 0x%lx null status", (ulong)layout->eld_objid);
		return;
	}

	mlog_free_rbuf(lstat, 0, MLOG_NLPGMB(lstat) - 1);
	mlog_free_abuf(lstat, 0, MLOG_NLPGMB(lstat) - 1);

	kfree(lstat);
	layout->eld_lstat = NULL;
}

/**
 * mlog_logrecs_validate()
 *
 * Validate records in lstat.rbuf relative to lstat state where midrec
 * indicates if mid data record from previous log block; updates lstate to
 * reflect valid markers found (if any).
 *
 * Returns:
 *   0 if successful; merr_t otherwise
 *
 *   In the output param, i.e., midrec, we store:
 *   1 if log records are valid and ended mid data record
 *   0 if log records are valid and did NOT end mid data record
 */
static merr_t
mlog_logrecs_validate(
	struct mlog_descriptor *mp,
	struct mlog_stat       *lstat,
	int                    *midrec,
	u16                     rbidx,
	u8                      lbidx)
{
	/*
	 * note: header len must be based on version since not
	 * guaranteed latest
	 */
	merr_t                       err = 0;
	u64                          recnum = 0;
	int                          recoff;
	struct omf_logrec_descriptor lrd;
	char                        *rbuf;
	u16                          sectsz = 0;

	sectsz = MLOG_SECSZ(lstat);
	rbuf   = lstat->lst_rbuf[rbidx] + lbidx * sectsz;

	recoff = omf_logblock_header_len_le(rbuf);
	if (recoff < 0)
		return merr(ENODATA);

	while (sectsz - recoff >= OMF_LOGREC_DESC_PACKLEN) {
		omf_logrec_desc_unpack_letoh(&lrd, &rbuf[recoff]);

		assert(lrd.olr_rtype <= OMF_LOGREC_CEND);

		if (lrd.olr_rtype == OMF_LOGREC_CSTART) {
			if (!lstat->lst_csem || lstat->lst_rsoff || recnum) {
				/*
				 * no compaction or not first rec in
				 * first log block; logging err
				 */
				err = merr(ENODATA);
				mp_pr_err("no compaction or not first rec in first log block %u %ld %u %u %lu",
					  err, lstat->lst_csem,
					  lstat->lst_rsoff,
					  rbidx, lbidx, (ulong)recnum);
				return err;
			}
			lstat->lst_cstart = 1;
			*midrec = 0;
		} else if (lrd.olr_rtype == OMF_LOGREC_CEND) {
			if (!lstat->lst_csem || !lstat->lst_cstart ||
			    lstat->lst_cend || *midrec) {
				/* no compaction or cend before
				 * cstart or more than one cend or
				 * cend mid-record all of which are
				 * logging errors
				 */
				err = merr(ENODATA);
				mp_pr_err("inconsistent compaction recs %u %u %u %d",
					  err, lstat->lst_csem,
					  lstat->lst_cstart, lstat->lst_cend,
					  *midrec);
				return err;
			}
			lstat->lst_cend = 1;
		} else if (lrd.olr_rtype == OMF_LOGREC_EOLB) {
			if (*midrec || !recnum) {
				/*
				 * EOLB mid-record or first record;
				 * logging error
				 */
				err = merr(ENODATA);
				mp_pr_err("end of log block marker at wrong place %d %lu",
					  err, *midrec, (ulong)recnum);
				return err;
			}
			/* no more records in log buffer */
			break;
		} else if (lrd.olr_rtype == OMF_LOGREC_DATAFULL) {
			if (*midrec && recnum) {
				/*
				 * can occur mid data rec only
				 * if is first rec in log block
				 * indicating partial data rec
				 * at end of last log block
				 * which is a valid failure
				 * mode; otherwise is a logging
				 * error
				 */
				err = merr(ENODATA);
				mp_pr_err("data full marker at wrong place %d %lu",
					  err, *midrec, (ulong)recnum);
				return err;
			}
			*midrec = 0;
		} else if (lrd.olr_rtype == OMF_LOGREC_DATAFIRST) {
			if (*midrec && recnum) {
				/* see comment for DATAFULL */
				err = merr(ENODATA);
				mp_pr_err("data first marker at wrong place %d %lu",
					  err, *midrec, (ulong)recnum);
				return err;
			}
			*midrec = 1;
		} else if (lrd.olr_rtype == OMF_LOGREC_DATAMID) {
			if (!*midrec) {
				/*
				 * must occur mid data record; logging
				 * error
				 */
				err = merr(ENODATA);
				mp_pr_err("data mid marker at wrong place %d %lu",
					  err, *midrec, (ulong)recnum);
				return err;
			}
		} else if (lrd.olr_rtype == OMF_LOGREC_DATALAST) {
			if (!(*midrec)) {
				/*
				 * must occur mid data record; logging
				 * error
				 */
				err = merr(ENODATA);
				mp_pr_err("data last marker at wrong place %d %lu",
					  err, *midrec, (ulong)recnum);
				return err;
			}
			*midrec = 0;
		} else {
			/* unknown rtype; logging error */
			err = merr(ENODATA);
			mp_pr_err("unknown record type %d %lu",
				  err, lrd.olr_rtype, (ulong)recnum);
			return err;
		}

		recnum = recnum + 1;
		recoff = recoff + OMF_LOGREC_DESC_PACKLEN + lrd.olr_rlen;
	}

	return err;
}

/**
 * mlog_read_iter_init() - Initialize read iterator
 *
 * @layout: mlog layout
 * @lstat:  mlog stat
 * @lri"    mlog read iterator
 */
static void
mlog_read_iter_init(
	struct ecio_layout_descriptor  *layout,
	struct mlog_stat               *lstat,
	struct mlog_read_iter          *lri)
{
	lri->lri_layout = layout;
	lri->lri_gen    = layout->eld_gen;
	lri->lri_soff   = 0;
	lri->lri_roff   = 0;
	lri->lri_valid  = 1;
	lri->lri_rbidx  = 0;
	lri->lri_sidx   = 0;

	lstat->lst_rsoff  = -1;
	lstat->lst_rseoff = -1;
}

/**
 * mlog_stat_init_common() - Initialize mlog_stat fields.
 *
 * @lstat: mlog_stat
 */
static void
mlog_stat_init_common(
	struct ecio_layout_descriptor  *layout,
	struct mlog_stat               *lstat)
{
	struct mlog_read_iter  *lri;

	lstat->lst_pfsetid = 0;
	lstat->lst_cfsetid = 1;
	lstat->lst_abidx   = 0;
	lstat->lst_asoff   = -1;
	lstat->lst_cfssoff = OMF_LOGBLOCK_HDR_PACKLEN;
	lstat->lst_aoff    = OMF_LOGBLOCK_HDR_PACKLEN;
	lstat->lst_abdirty = false;
	lstat->lst_wsoff   = 0;
	lstat->lst_cstart  = 0;
	lstat->lst_cend    = 0;

	lri = &lstat->lst_citr;
	mlog_read_iter_init(layout, lstat, lri);
}

/**
 * mlog_stat_reinit()
 *
 * Reinit log stat struct for mlog layout.
 *
 * Returns: 0 if successful, merr_t otherwise
 */
merr_t
mlog_stat_reinit(struct mpool_descriptor *mp, struct mlog_descriptor *mlh)
{
	struct ecio_layout_descriptor  *layout;
	struct mlog_stat               *lstat;

	layout = mlog2layout(mlh);
	if (!layout)
		return merr(EINVAL);

	pmd_obj_wrlock(mp, layout);

	lstat = (struct mlog_stat *)layout->eld_lstat;
	if (!lstat) {
		/* Nothing to free, erase called on a closed mlog */
		pmd_obj_wrunlock(mp, layout);

		return 0;
	}

	mlog_free_abuf(lstat, 0, lstat->lst_abidx);
	mlog_free_rbuf(lstat, 0, MLOG_NLPGMB(lstat) - 1);

	mlog_stat_init_common(layout, lstat);

	pmd_obj_wrunlock(mp, layout);

	return 0;
}

/*
 * IO Interfaces
 */

/**
 * mlog_rw:
 *
 * If called in mpctl context call call into mpctl user. Otherwise, directly
 * call into mpcore.
 *
 * @mp:      mpool descriptor
 * @mlh:     mlog descriptor
 * iov:      iovec
 * iovcnt:   iov cnt
 * boff:     IO offset
 * rw:       MPOOL_OP_READ or MPOOL_OP_WRITE
 * skip_ser: client guarantees serialization
 */
static merr_t
mlog_rw(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	struct iovec            *iov,
	int                      iovcnt,
	u64                      boff,
	u8                       rw,
	bool                     skip_ser)
{
	struct ecio_layout_descriptor *layout;

	layout = mlog2layout(mlh);
	if (!layout)
		return merr(EINVAL);

	return mpool_mlog_rw(layout->eld_mlo->mlo_mlog->ml_mlh, iov, iovcnt,
			  boff, rw);
}

/**
 * mlog_stat_init()
 *
 * Allocate and init log stat struct for mlog layout.
 *
 * Returns: 0 if successful, merr_t otherwise
 */
static merr_t
mlog_stat_init(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	bool                        csem)
{
	struct ecio_layout_descriptor  *layout;
	struct mlog_stat               *lstat;
	struct mlog_fsetparms           mfp;
	u32                             bufsz;
	merr_t                          err;

	layout = mlog2layout(mlh);
	if (!layout)
		return merr(EINVAL);

	layout->eld_lstat = NULL;

	mlog_init_fsetparms(mp, mlh, &mfp);

	bufsz = sizeof(*lstat) + (2 * mfp.mfp_nlpgmb * sizeof(char *));

	lstat = kzalloc(bufsz, GFP_KERNEL);
	if (!lstat) {
		err = merr(ENOMEM);
		mp_pr_err("mpool %s, allocating mlog 0x%lx status failed %u",
			  err, mp->pds_name, (ulong)layout->eld_objid, bufsz);
		return err;
	}

	lstat->lst_abuf = (char **)((char *)lstat + sizeof(*lstat));
	lstat->lst_rbuf = lstat->lst_abuf + mfp.mfp_nlpgmb;
	lstat->lst_mfp  = mfp;
	lstat->lst_csem = csem;

	mlog_stat_init_common(layout, lstat);

	layout->eld_lstat  = lstat;

	return 0;
}

/**
 * mlog_setup_buf()
 *
 * Build an iovec list to read into an mlog read buffer, or write from
 * an mlog append buffer.  In the read case, the read buffer pages will be
 * allocated if not already populated.
 *
 * @lstat:   mlog_stat
 * @riov:    iovec (output)
 * @iovcnt:  number of iovecs
 * @l_iolen: IO length for the last log page in the buffer
 * @op:      MPOOL_OP_READ or MPOOL_OP_WRITE
 */
static merr_t
mlog_setup_buf(
	struct mlog_stat    *lstat,
	struct iovec       **riov,
	u16                  iovcnt,
	u16                  l_iolen,
	u8                   op)
{
	struct iovec  *iov = *riov;

	char  *buf;
	u16    i;
	u16    len       = MLOG_LPGSZ(lstat);
	bool   alloc_iov = false;

	assert(len == PAGE_SIZE);
	assert(l_iolen <= PAGE_SIZE);

	if (!iov) {
		assert((iovcnt * sizeof(*iov)) <= PAGE_SIZE);

		iov = kcalloc(iovcnt, sizeof(*iov), GFP_KERNEL);
		if (!iov)
			return merr(ENOMEM);

		alloc_iov = true;
		*riov = iov;
	}

	for (i = 0; i < iovcnt; i++, iov++) {

		buf = ((op == MPOOL_OP_READ) ?
			lstat->lst_rbuf[i] : lstat->lst_abuf[i]);

		/* iov_len for the last log page in read/write buffer. */
		if (i == iovcnt - 1 && l_iolen != 0)
			len = l_iolen;

		assert(IS_ALIGNED(len, MLOG_SECSZ(lstat)));

		if (op == MPOOL_OP_WRITE && buf) {
			iov->iov_base = buf;
			iov->iov_len  = len;
			continue;
		}

		/*
		 * Pages for the append buffer are allocated in
		 * mlog_append_*(), so we shouldn't be here for MPOOL_OP_WRITE.
		 */
		assert(op == MPOOL_OP_READ);

		/*
		 * If the read buffer contains stale log pages from a prior
		 * iterator, reuse them. No need to zero these pages for
		 * the same reason provided in the following comment.
		 */
		if (buf) {
			iov->iov_base = buf;
			iov->iov_len  = len;
			continue;
		}

		/*
		 * No need to zero the read buffer as we never read more than
		 * what's needed and do not consume beyond what's read.
		 */
		buf = (char *)__get_free_page(GFP_KERNEL);
		if (!buf) {
			mlog_free_rbuf(lstat, 0, i - 1);
			if (alloc_iov) {
				kfree(iov);
				*riov = NULL;
			}

			return merr(ENOMEM);
		}

		/*
		 * Must be a page-aligned buffer so that it can be used
		 * in bio_add_page().
		 */
		assert(PAGE_ALIGNED(buf));

		lstat->lst_rbuf[i] = iov->iov_base = buf;
		iov->iov_len = len;
	}

	return 0;
}

static inline void
max_cfsetid(
	struct omf_logblock_header    *lbh,
	struct ecio_layout_descriptor *layout,
	u32                           *fsetid)
{
	if (!mpool_uuid_compare(&lbh->olh_magic, &layout->eld_uuid) &&
	    (lbh->olh_gen == layout->eld_gen))
		*fsetid  = max_t(u32, *fsetid, lbh->olh_cfsetid);
}

/**
 * mlog_logpage_validate() - Validate log records at log page index 'rbidx' in
 * the read buffer.
 *
 * @mlh:        mlog_descriptor
 * @lstat:      mlog_stat
 * @rbidx:      log page index in the read buffer to validate
 * @nseclpg:    number of sectors in the log page @rbidx
 * @midrec:     refer to mlog_logrecs_validate
 * @leol_found: true, if LEOL found. false, if LEOL not found/log full (output)
 * @fsetidmax:  maximum flush set ID found in the log (output)
 * @pfsetid:    previous flush set ID, if LEOL found (output)
 */
static merr_t
mlog_logpage_validate(
	struct mlog_descriptor    *mlh,
	struct mlog_stat          *lstat,
	u16                        rbidx,
	u8                         nseclpg,
	int                       *midrec,
	bool                      *leol_found,
	u32                       *fsetidmax,
	u32                       *pfsetid)
{
	merr_t                         err = 0;
	char                          *rbuf;
	u8                             lbidx;
	u16                            sectsz;
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);

	sectsz = MLOG_SECSZ(lstat);
	rbuf   = lstat->lst_rbuf[rbidx];

	/* Loop through nseclpg sectors in the log page @rbidx. */
	for (lbidx = 0; lbidx < nseclpg; lbidx++) {
		struct omf_logblock_header lbh;

		memset(&lbh, 0, sizeof(lbh));

		(void)omf_logblock_header_unpack_letoh(&lbh, rbuf);

		/*
		 * If LEOL is already found, then this loop determines
		 * fsetidmax, i.e., scans through the sectors to determine
		 * any stale flush set id from a prior failed CFS flush.
		 */
		if (*leol_found) {
			max_cfsetid(&lbh, layout, fsetidmax);
			rbuf += sectsz;
			continue;
		}

		/*
		 * Check for LEOL based on prev and cur flush set ID.
		 * If LEOL is detected, then no need to validate this and
		 * the log blocks that follow.
		 *
		 * We issue DISCARD commands to erase mlogs. However the data
		 * read from a discarded block is non-determinstic. It could be
		 * all 0s, all 1s or last written data.
		 *
		 * We could read following 5 types of data from mlog:
		 * 1) Garbage
		 * 2) Stale logs with different log block gen
		 * 3) Stale logs with different flushset ID
		 * 4) Stale logs with different magic (UUID)
		 * 5) Valid logs
		 */
		if (mpool_uuid_compare(&lbh.olh_magic, &layout->eld_uuid) ||
		    (lbh.olh_gen != layout->eld_gen) ||
		    (lbh.olh_pfsetid != *fsetidmax)) {
			*leol_found = true;
			*pfsetid    = *fsetidmax;
			rbuf       += sectsz;
			max_cfsetid(&lbh, layout, fsetidmax);
			continue;
		}

		*fsetidmax = lbh.olh_cfsetid;

		/* Validate the log block at lbidx. */
		err = mlog_logrecs_validate(mlh, lstat, midrec, rbidx, lbidx);
		if (err) {
			mp_pr_err("mlog %p,, midrec %d, log pg idx %u, sector idx %u",
				  err, mlh, *midrec, rbidx, lbidx);

			return err;
		}

		++lstat->lst_wsoff;
		rbuf += sectsz;
	}

	return 0;
}

/**
 * mlog_populate_abuf() - Makes append offset page-aligned and performs the
 * read operation in the read-modify-write cycle.
 *
 * This is to ensure that IO-requests to the device are always 4K-aligned.
 * The read-modify-write cycle happens *only* if the first append post mlog
 * open lands on a non-page aligned sector offset. For any further appends,
 * read-modify-write cycle doesn't happen, as the 4k-aligned version of the
 * flush set algorithm ensures 4k-alignment of sector offsets at the start
 * of each log page.
 *
 * @mp:       mpool descriptor
 * @layout:   layout descriptor
 * @soff:     sector/LB offset
 * @buf:      buffer to populate. Size of this buffer must be MLOG_LPGSZ(lstat).
 * @skip_ser: client guarantees serialization
 */
static merr_t
mlog_populate_abuf(
	struct mpool_descriptor       *mp,
	struct ecio_layout_descriptor *layout,
	off_t                         *soff,
	char                          *buf,
	bool                           skip_ser)
{
	struct mlog_stat       *lstat;
	struct iovec            iov;

	merr_t err;
	off_t  off;
	u32    leadb;
	u16    sectsz;
	u16    iovcnt;
	u8     leading;

	lstat  = (struct mlog_stat *)layout->eld_lstat;
	sectsz = MLOG_SECSZ(lstat);

	/* Find the leading number of sectors to make it page-aligned. */
	leading = ((*soff * sectsz) & ~PAGE_MASK) >> ilog2(sectsz);
	if (leading == 0)
		return 0; /* Nothing to do */

	*soff = *soff - leading;
	leadb = leading * sectsz;

	iovcnt       = 1;
	iov.iov_base = buf;
	iov.iov_len  = MLOG_LPGSZ(lstat);

	off = *soff * sectsz;
	assert(IS_ALIGNED(off, MLOG_LPGSZ(lstat)));

	err = mlog_rw(mp, layout2mlog(layout), &iov, iovcnt, off,
			MPOOL_OP_READ, skip_ser);
	if (err) {
		mp_pr_err("mpool %s, mlog 0x%lx, read IO failed, iovcnt: %u, off: 0x%lx",
			  err, mp->pds_name, (ulong)layout->eld_objid,
			  iovcnt, off);

		return err;
	}

	memset(&buf[leadb], 0, MLOG_LPGSZ(lstat) - leadb);

	return 0;
}

/**
 * mlog_populate_rbuf() - Fill the read buffer after aligning the read offset
 * to page boundary. Having the read offsets page-aligned avoids unnecessary
 * complexity at the ecio layer.
 *
 * In the worst case, for 512 byte sectors, we would end up reading 7
 * additional sectors, which is acceptable. There won't be any overhead for
 * 4 KiB sectors as they are naturally page-aligned.
 *
 * Caller must hold the write lock on the layout
 *
 * @mp:       mpool descriptor
 * @layout:   layout descriptor
 * @nsec:     number of sectors to populate
 * @soff:     start sector/LB offset
 * @skip_ser: client guarantees serialization
 */
static merr_t
mlog_populate_rbuf(
	struct mpool_descriptor       *mp,
	struct ecio_layout_descriptor *layout,
	u16                           *nsec,
	off_t                         *soff,
	bool                           skip_ser)
{
	struct iovec           *iov = NULL;
	struct mlog_stat       *lstat;

	merr_t err;
	off_t  off;
	u16    maxsec;
	u16    l_iolen;
	u16    sectsz;
	u16    iovcnt;
	u8     nseclpg;
	u8     leading;

	lstat  = (struct mlog_stat *)layout->eld_lstat;
	mlog_extract_fsetparms(lstat, &sectsz, NULL, &maxsec, &nseclpg);

	/* Find the leading number of sectors to make it page-aligned. */
	leading = ((*soff * sectsz) & ~PAGE_MASK) >> ilog2(sectsz);
	*soff   = *soff - leading;
	*nsec  += leading;

	*nsec   = min_t(u32, maxsec, *nsec);
	iovcnt  = (*nsec + nseclpg - 1) / nseclpg;

	/* No. of sectors in the last log page. */
	l_iolen = MLOG_LPGSZ(lstat);
	if (!FORCE_4KA(lstat) && !(IS_SECPGA(lstat)))
		l_iolen = (*nsec % nseclpg) * sectsz;

	err = mlog_setup_buf(lstat, &iov, iovcnt, l_iolen, MPOOL_OP_READ);
	if (err) {
		mp_pr_err("mpool %s, mlog 0x%lx setup failed, iovcnt: %u, last iolen: %u",
			  err, mp->pds_name, (ulong)layout->eld_objid,
			  iovcnt, l_iolen);

		return err;
	}

	off = *soff * sectsz;
	assert(IS_ALIGNED(off, MLOG_LPGSZ(lstat)));

	err = mlog_rw(mp, layout2mlog(layout), iov, iovcnt, off,
			MPOOL_OP_READ, skip_ser);
	if (err) {
		mp_pr_err("mpool %s, mlog 0x%lx populate read buffer, read IO failed iovcnt: %u, off: 0x%lx",
			  err, mp->pds_name, (ulong)layout->eld_objid,
			  iovcnt, off);

		mlog_free_rbuf(lstat, 0, MLOG_NLPGMB(lstat) - 1);
		kfree(iov);

		return err;
	}

	/*
	 * If there're any unused buffers beyond iovcnt, free it. This is
	 * likely to happen when there're multiple threads reading from
	 * the same mlog simultaneously, using their own iterator.
	 */
	mlog_free_rbuf(lstat, iovcnt, MLOG_NLPGMB(lstat) - 1);

	kfree(iov);

	return 0;
}

/**
 * mlog_read_and_validate() - Called by mlog_open() to read and validate log
 * records in the mlog. In-addition, determine the previous and current flush
 * set ID to be used by the next flush.
 *
 * Note: this function reads the entire mlog. Doing so allows us to confirm that
 * the mlog's contents are completely legit, and also to recognize the case
 * where a compaction started but failed to complete (CSTART with no CEND) -
 * for which the recovery is to use the other mlog of the mlpair.
 * If the mlog is huge, or if there are a bazillion of them, this could be an
 * issue to revisit in future performance or functionality optimizations.
 *
 * Transactional logs are expensive; this does some "extra" reading at open
 * time, with some serious benefits.
 *
 * Caller must hold the write lock on the layout, which protects the mutation
 * of the read buffer.
 *
 * @mp:     mpool descriptor
 * @layout: layout descriptor
 * @lempty: is the log empty? (output)
 */
static merr_t
mlog_read_and_validate(
	struct mpool_descriptor       *mp,
	struct ecio_layout_descriptor *layout,
	bool                          *lempty)
{
	struct mlog_stat  *lstat;

	merr_t err         = 0;
	off_t  leol_off    = 0;
	off_t  rsoff;
	int    midrec      = 0;
	int    remsec;
	bool   leol_found  = false;
	bool   fsetid_loop = false;
	u32    fsetidmax   = 0;
	u32    pfsetid     = 0;
	u16    maxsec;
	u16    nsecs;
	u16    nlpgs;
	u8     nseclpg;
	bool   skip_ser = false;

	lstat = (struct mlog_stat *)layout->eld_lstat;

	remsec = MLOG_TOTSEC(lstat);
	maxsec = MLOG_NSECMB(lstat);
	rsoff  = lstat->lst_wsoff;

	while (remsec > 0) {
		u16 rbidx;

		nseclpg = MLOG_NSECLPG(lstat);
		nsecs   = min_t(u32, maxsec, remsec);

		err = mlog_populate_rbuf(mp, layout, &nsecs, &rsoff, skip_ser);
		if (err) {
			mp_pr_err("mpool %s, mlog 0x%lx rbuf validation, read failed, nsecs: %u, rsoff: 0x%lx",
				  err, mp->pds_name, (ulong)layout->eld_objid,
				  nsecs, rsoff);

			goto exit;
		}

		nlpgs = (nsecs + nseclpg - 1) / nseclpg;
		lstat->lst_rsoff = rsoff;

		/* Validate the read buffer, one log page at a time. */
		for (rbidx = 0; rbidx < nlpgs; rbidx++) {

			/* No. of sectors in the last log page. */
			if (rbidx == nlpgs - 1) {
				nseclpg = nsecs % nseclpg;
				nseclpg = nseclpg > 0 ? nseclpg :
						MLOG_NSECLPG(lstat);
			}

			/* Validate the log block(s) in the log page @rbidx. */
			err = mlog_logpage_validate(layout2mlog(layout),
					lstat, rbidx, nseclpg, &midrec,
					&leol_found, &fsetidmax, &pfsetid);
			if (err) {
				mp_pr_err("mpool %s, mlog 0x%lx rbuf validate failed, leol: %d, fsetidmax: %u, pfsetid: %u",
					  err, mp->pds_name,
					  (ulong)layout->eld_objid, leol_found,
					  fsetidmax, pfsetid);

				mlog_free_rbuf(lstat, rbidx, nlpgs - 1);
				goto exit;
			}

			mlog_free_rbuf(lstat, rbidx, rbidx);

			/*
			 * If LEOL is found, then note down the LEOL offset
			 * and kick off the scan to identify any stale flush
			 * set id from a prior failed flush. If there's one,
			 * then the next flush set ID must be set one greater
			 * than the stale fsetid.
			 */
			if (leol_found && !fsetid_loop) {
				leol_off    = lstat->lst_wsoff;
				fsetid_loop = true;
			}
		}

		remsec -= nsecs;
		if (remsec == 0)
			break;
		assert(remsec > 0);

		if (fsetid_loop) {
			u16    compsec;
			off_t  endoff;
			/*
			 * To determine the new flush set ID, we need to
			 * scan only through the next min(MLOG_NSECMB, remsec)
			 * sectors. This is because of the max flush size being
			 * 1 MB and hence a failed flush wouldn't have touched
			 * any sectors beyond 1 MB from LEOL.
			 */
			endoff  = rsoff + nsecs - 1;
			compsec = endoff - leol_off + 1;
			remsec  = min_t(u32, remsec, maxsec - compsec);
			assert(remsec >= 0);

			rsoff = endoff + 1;
		} else {
			rsoff = lstat->lst_wsoff;
		}
	}

	/* LEOL wouldn't have been set for a full log. */
	if (!leol_found)
		pfsetid = fsetidmax;

	if (pfsetid != 0)
		*lempty = false;

	lstat->lst_pfsetid = pfsetid;
	lstat->lst_cfsetid = fsetidmax + 1;

exit:
	lstat->lst_rsoff = -1;

	return err;
}

merr_t
mlog_open(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	u8                       flags,
	u64                     *gen)
{
	struct mlog_stat              *lstat = NULL;
	struct ecio_layout_descriptor *layout;

	merr_t err    = 0;
	bool   lempty = false;
	bool   csem   = false;
	bool   skip_ser   = false;

	layout = mlog2layout(mlh);
	if (!layout)
		return merr(EINVAL);

	*gen = 0;

	pmd_obj_wrlock(mp, layout);

	flags &= MLOG_OF_SKIP_SER | MLOG_OF_COMPACT_SEM;

	if (flags & MLOG_OF_COMPACT_SEM)
		csem = true;

	if (flags & MLOG_OF_SKIP_SER)
		skip_ser = true;

	if (layout->eld_lstat) {
		/* log already open */
		lstat = (struct mlog_stat *)layout->eld_lstat;
		if (csem && !lstat->lst_csem) {
			/* re-open has inconsistent csem flag */
			pmd_obj_wrunlock(mp, layout);
			err = merr(EINVAL);
			mp_pr_err("mpool %s, re-opening of mlog 0x%lx, inconsistent compaction setting %u %u",
				  err, mp->pds_name, (ulong)layout->eld_objid,
				  csem, lstat->lst_csem);
		} else if (skip_ser &&
				!(layout->eld_flags & MLOG_OF_SKIP_SER)) {
			pmd_obj_wrunlock(mp, layout);
			err = merr(EINVAL);

			/* re-open has inconsistent seralization flag */
			mp_pr_err("mpool %s, re-opening of mlog 0x%lx, inconsistent serialization setting %u %u",
				  err, mp->pds_name, (ulong)layout->eld_objid,
				  skip_ser,
				  layout->eld_flags & MLOG_OF_SKIP_SER);
		} else {
			*gen = layout->eld_gen;
			pmd_obj_wrunlock(mp, layout);
		}
		return err;
	}

	if (!(layout->eld_state & ECIO_LYT_COMMITTED)) {
		*gen = 0;
		pmd_obj_wrunlock(mp, layout);

		err = merr(EINVAL);
		mp_pr_err("mpool %s, mlog 0x%lx, not committed",
			  err, mp->pds_name, (ulong)layout->eld_objid);
		return err;
	}

	if (skip_ser)
		layout->eld_flags |= MLOG_OF_SKIP_SER;

	err = mlog_stat_init(mp, mlh, csem);
	if (err) {
		*gen = 0;
		pmd_obj_wrunlock(mp, layout);

		mp_pr_err("mpool %s, mlog 0x%lx, mlog status initialization failed",
			  err, mp->pds_name, (ulong)layout->eld_objid);
		return err;
	}

	lempty = true;
	lstat = (struct mlog_stat *)layout->eld_lstat;

	err = mlog_read_and_validate(mp, layout, &lempty);
	if (err) {
		mlog_stat_free(layout);
		pmd_obj_wrunlock(mp, layout);

		mp_pr_err("mpool %s, mlog 0x%lx, mlog content validation failed",
			  err, mp->pds_name, (ulong)layout->eld_objid);
		return err;
	} else if (!lempty && csem) {
		if (!lstat->lst_cstart) {
			mlog_stat_free(layout);
			pmd_obj_wrunlock(mp, layout);
			err = merr(ENODATA);
			mp_pr_err("mpool %s, mlog 0x%lx, compaction start missing",
				  err, mp->pds_name, (ulong)layout->eld_objid);
			return err;
		} else if (!lstat->lst_cend) {
			mlog_stat_free(layout);
			pmd_obj_wrunlock(mp, layout);
			/* incomplete compaction */
			err = merr(EMSGSIZE);
			mp_pr_err("mpool %s, mlog 0x%lx, incomplete compaction",
				  err, mp->pds_name, (ulong)layout->eld_objid);
			return err;
		}
	}

	*gen = layout->eld_gen;

	pmd_obj_wrunlock(mp, layout);

	return err;
}

/**
 * mlog_alloc_abufpg() - Allocate a log page at append buffer index 'abidx'.
 * If the sector size is 512B AND 4K-alignment is forced AND the append offset
 * at buffer index '0' is not 4K-aligned, then call mlog_populate_abuf().
 *
 * @mp:       mpool descriptor
 * @layout:   layout descriptor
 * @abidx:    allocate log page at index 'abidx'.
 * @skip_ser: client guarantees serialization
 */
static merr_t
mlog_alloc_abufpg(
	struct mpool_descriptor       *mp,
	struct ecio_layout_descriptor *layout,
	u16                            abidx,
	bool                           skip_ser)
{
	struct mlog_stat  *lstat;
	char  *abuf;

	lstat = layout->eld_lstat;
	assert(MLOG_LPGSZ(lstat) == PAGE_SIZE);

	abuf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!abuf)
		return merr(ENOMEM);

	assert(PAGE_ALIGNED(abuf));

	lstat->lst_abuf[abidx] = abuf;

	if (abidx == 0) {
		merr_t err;
		off_t  asoff;
		off_t  wsoff;
		u16    aoff;
		u16    sectsz;

		/* This path is taken *only* for the first append following
		 * an mlog_open().
		 */
		sectsz = MLOG_SECSZ(lstat);
		wsoff  = lstat->lst_wsoff;
		aoff   = lstat->lst_aoff;

		if ((!FORCE_4KA(lstat)) ||
			(IS_ALIGNED(wsoff * sectsz, MLOG_LPGSZ(lstat)))) {
			/* This is the common path */
			lstat->lst_asoff = wsoff;
			return 0;
		}

		/*
		 * This path is taken *only* if,
		 * - the log block size is 512B AND
		 * - lst_wsoff is non page-aligned, which is possible for the
		 *   first append post mlog_open.
		 */
		asoff = wsoff;
		err = mlog_populate_abuf(mp, layout, &asoff, abuf, skip_ser);
		if (err) {
			mlog_free_abuf(lstat, abidx, abidx);
			mp_pr_err("mpool %s, mlog 0x%lx, making write offset %ld 4K-aligned failed",
				  err, mp->pds_name,
				  (ulong)layout->eld_objid, wsoff);

			return err;
		}

		assert(asoff <= wsoff);
		assert(IS_ALIGNED(asoff * sectsz, MLOG_LPGSZ(lstat)));
		lstat->lst_cfssoff = ((wsoff - asoff) * sectsz) + aoff;
		lstat->lst_asoff   = asoff;
	}

	return 0;
}

/**
 * mlog_logblocks_hdrpack() - Called prior to CFS flush to pack log
 * block header in all log blocks in the append buffer.
 *
 * @layout: object layout
 */
static merr_t mlog_logblocks_hdrpack(struct ecio_layout_descriptor *layout)
{
	struct omf_logblock_header lbh;
	struct mlog_stat          *lstat;

	merr_t err;
	off_t  lpgoff;
	u32    pfsetid;
	u32    cfsetid;
	u16    idx;
	u16    abidx;
	u16    sectsz;
	u8     nseclpg;
	u8     sec;
	u8     start;

	lstat   = (struct mlog_stat *)layout->eld_lstat;
	sectsz  = MLOG_SECSZ(lstat);
	nseclpg = MLOG_NSECLPG(lstat);
	abidx   = lstat->lst_abidx;
	pfsetid = lstat->lst_pfsetid;
	cfsetid = lstat->lst_cfsetid;

	lbh.olh_vers = OMF_LOGBLOCK_VERS;

	for (idx = 0; idx <= abidx; idx++) {
		start = 0;

		if (FORCE_4KA(lstat) && idx == 0)
			start = (lstat->lst_cfssoff >> ilog2(sectsz));

		if (idx == abidx)
			nseclpg = lstat->lst_wsoff -
				(nseclpg * abidx + lstat->lst_asoff) + 1;

		for (sec = start; sec < nseclpg; sec++) {
			lbh.olh_pfsetid = pfsetid;
			lbh.olh_cfsetid = cfsetid;
			mpool_uuid_copy(&lbh.olh_magic, &layout->eld_uuid);
			lbh.olh_gen = layout->eld_gen;
			lpgoff = sec * sectsz;

			/* Pack the log block header. */
			err = omf_logblock_header_pack_htole(&lbh,
						&lstat->lst_abuf[idx][lpgoff]);
			if (err) {
				mp_pr_err("mlog packing log block header at log pg idx %u, vers %u failed",
					  err, idx, lbh.olh_vers);

				return err;
			}

			/*
			 * If there's more than one sector to flush, pfsetid
			 * is set to cfsetid.
			 */
			pfsetid = cfsetid;
		}
	}

	return 0;
}

/**
 * mlog_flush_abuf() - Set up iovec and flush the append buffer to media.
 *
 * @mp:       mpool descriptor
 * @layout:   layout descriptor
 * @skip_ser: client guarantees serialization
 */
static merr_t
mlog_flush_abuf(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout,
	bool                            skip_ser)
{
	struct iovec           *iov = NULL;
	struct mlog_stat       *lstat;

	merr_t err;
	off_t  off;
	u16    abidx;
	u16    l_iolen;
	u16    sectsz;
	u8     nseclpg;

	lstat  = (struct mlog_stat *)layout->eld_lstat;
	mlog_extract_fsetparms(lstat, &sectsz, NULL, NULL, &nseclpg);

	abidx   = lstat->lst_abidx;
	l_iolen = MLOG_LPGSZ(lstat);

	if (!FORCE_4KA(lstat) && !(IS_SECPGA(lstat))) {
		u8 asidx;

		asidx = lstat->lst_wsoff -
			(nseclpg * abidx + lstat->lst_asoff);

		/* No. of sectors in the last log page. */
		if (asidx < nseclpg - 1)
			l_iolen = (asidx + 1) * sectsz;
	}

	err = mlog_setup_buf(lstat, &iov, abidx + 1, l_iolen, MPOOL_OP_WRITE);
	if (err) {
		mp_pr_err("mpool %s, mlog 0x%lx flush, buffer setup failed, iovcnt: %u, last iolen: %u",
			  err, mp->pds_name,
			  (ulong)layout->eld_objid, abidx + 1, l_iolen);

		return err;
	}

	off = lstat->lst_asoff * sectsz;

	assert((IS_ALIGNED(off, MLOG_LPGSZ(lstat))) ||
		(!FORCE_4KA(lstat) && IS_ALIGNED(off, MLOG_SECSZ(lstat))));

	err = mlog_rw(mp, layout2mlog(layout), iov, abidx + 1, off,
			MPOOL_OP_WRITE, skip_ser);
	if (err) {
		mp_pr_err("mpool %s, mlog 0x%lx flush append buffer, IO failed iovcnt %u, off 0x%lx",
			  err, mp->pds_name,
			  (ulong)layout->eld_objid, abidx + 1, off);
		kfree(iov);

		return err;
	}

	kfree(iov);

	return 0;
}

/**
 * mlog_flush_posthdlr_4ka() - Handles both successful and failed flush for
 * 512B sectors with 4K-Alignment.
 *
 * @mp:     mpool descriptor
 * @layout: layout descriptor
 * @fsucc:  flush status
 */
static void
mlog_flush_posthdlr_4ka(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout,
	bool                            fsucc)
{
	struct mlog_stat   *lstat;

	char  *abuf;
	off_t  asoff;
	off_t  wsoff;
	u32    nsecwr;
	u16    abidx;
	u16    sectsz;
	u8     asidx;

	lstat  = (struct mlog_stat *)layout->eld_lstat;
	sectsz = MLOG_SECSZ(lstat);
	abidx  = lstat->lst_abidx;
	asoff  = lstat->lst_asoff;
	wsoff  = lstat->lst_wsoff;

	asidx  = wsoff - ((MLOG_NSECLPG(lstat) * abidx) + asoff);

	/* Set the current filling log page index to 0. */
	lstat->lst_abidx = 0;
	abuf = lstat->lst_abuf[0];

	if (!fsucc) {
		u32    cfssoff;

		/*
		 * Last CFS flush or header packing failed.
		 * Retain the pfsetid of the first log block.
		 */
		cfssoff = lstat->lst_cfssoff;
		memset(&abuf[cfssoff], 0, MLOG_LPGSZ(lstat) - cfssoff);
		asidx = (cfssoff >> ilog2(sectsz));
		lstat->lst_aoff  = cfssoff - (asidx * sectsz);
		lstat->lst_wsoff = asoff + asidx;

		goto exit2;
	}

	/* Last CFS flush succeded. */
	if (abidx != 0) {
		/* Reorganize buffers if the active log page not at index 0. */
		abuf = lstat->lst_abuf[abidx];
		lstat->lst_abuf[abidx] = NULL;
	}

	nsecwr = wsoff - (asoff + (lstat->lst_cfssoff >> ilog2(sectsz)));
	asoff  = wsoff - asidx;

	/* The last logblock of the just-written CFS is not full. */
	if (sectsz - lstat->lst_aoff >= OMF_LOGREC_DESC_PACKLEN) {
		if (nsecwr != 0)
			/* Set pfsetid to the cfsetid of just-written CFS. */
			lstat->lst_pfsetid  = lstat->lst_cfsetid;

		goto exit1;
	}

	/* The last logblock of the just-written CFS is full. */
	lstat->lst_aoff = OMF_LOGBLOCK_HDR_PACKLEN;
	++wsoff;
	if ((wsoff - asoff) == MLOG_NSECLPG(lstat)) {
		memset(&abuf[0], 0, MLOG_LPGSZ(lstat));
		asoff = wsoff;
	}
	/* Set pfsetid to the cfsetid of just-written CFS. */
	lstat->lst_pfsetid  = lstat->lst_cfsetid;

exit1:
	asidx              = wsoff - asoff;
	lstat->lst_cfssoff = (asidx * sectsz) + lstat->lst_aoff;
	lstat->lst_asoff   = asoff;
	lstat->lst_wsoff   = wsoff;

exit2:
	/* Increment cfsetid in all cases. */
	++lstat->lst_cfsetid;

	lstat->lst_abuf[0] = abuf;
}

/**
 * mlog_flush_posthdlr() - Handles both successful and failed flush for
 * 512B and 4K-sectors with native alignment, i.e., 512B and 4K resply.
 *
 * @mp:     mpool descriptor
 * @layout: layout descriptor
 * @fsucc:  flush status
 */
static void
mlog_flush_posthdlr(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout,
	bool                            fsucc)
{
	struct mlog_stat          *lstat;

	char  *abuf;
	off_t  asoff;
	off_t  lpgoff;
	u16    abidx;
	u16    sectsz;
	u8     asidx;

	lstat  = (struct mlog_stat *)layout->eld_lstat;
	sectsz = MLOG_SECSZ(lstat);
	abidx  = lstat->lst_abidx;
	asoff  = lstat->lst_asoff;

	asidx  = lstat->lst_wsoff - ((MLOG_NSECLPG(lstat) * abidx) + asoff);
	lpgoff = asidx * sectsz;

	/* Set the current filling log page index to 0. */
	lstat->lst_abidx = 0;
	abuf = lstat->lst_abuf[0];

	if (!fsucc) {
		u32    cfssoff;

		/*
		 * Last CFS flush or header packing failed.
		 * Retain the pfsetid of the first log block.
		 */
		cfssoff = lstat->lst_cfssoff;
		memset(&abuf[cfssoff], 0, MLOG_LPGSZ(lstat) - cfssoff);
		lstat->lst_aoff  = cfssoff;
		lstat->lst_wsoff = asoff;

		goto exit2;
	}

	/* Last CFS flush succeded. */
	if (abidx != 0) {
		/* Reorganize buffers if the active log page not at index 0. */
		abuf = lstat->lst_abuf[abidx];
		lstat->lst_abuf[abidx] = NULL;
	}

	/* The last logblock of the just-written CFS is not full. */
	if (sectsz - lstat->lst_aoff >= OMF_LOGREC_DESC_PACKLEN) {
		/*
		 * If the last logblock in the just-written CFS is
		 * first in the append buffer at abidx.
		 */
		if (lpgoff == 0) {
			if (abidx != 0)
				lstat->lst_pfsetid = lstat->lst_cfsetid;

			goto exit1;
		}

		memcpy(&abuf[0], &abuf[lpgoff], sectsz);
		memset(&abuf[sectsz], 0, lpgoff - sectsz + lstat->lst_aoff);
	} else { /* The last logblock of the just-written CFS is full. */
		memset(&abuf[0], 0, lpgoff + sectsz);
		lstat->lst_aoff = OMF_LOGBLOCK_HDR_PACKLEN;
		++lstat->lst_wsoff;
	}
	/* Set pfsetid to the cfsetid of just-written CFS. */
	lstat->lst_pfsetid  = lstat->lst_cfsetid;

exit1:
	lstat->lst_cfssoff = lstat->lst_aoff;
	lstat->lst_asoff   = lstat->lst_wsoff;

exit2:
	/* Increment cfsetid in all cases. */
	++lstat->lst_cfsetid;

	lstat->lst_abuf[0] = abuf;
}

/**
 * mlog_logblocks_flush() - Flush CFS and handle both successful and
 * failed flush.
 *
 * @mp:       mpool descriptor
 * @layout:   layout descriptor
 * @skip_ser: client guarantees serialization
 */
static merr_t
mlog_logblocks_flush(
	struct mpool_descriptor       *mp,
	struct ecio_layout_descriptor *layout,
	bool                           skip_ser)
{
	struct mlog_stat          *lstat;

	merr_t err;
	bool   fsucc = true;
	int    start;
	int    end;
	u16    abidx;

	lstat  = (struct mlog_stat *)layout->eld_lstat;
	abidx  = lstat->lst_abidx;

	/* Pack log block header in all the log blocks. */
	err = mlog_logblocks_hdrpack(layout);
	if (err) {
		mp_pr_err("mpool %s, mlog 0x%lx packing header failed",
			  err, mp->pds_name, (ulong)layout->eld_objid);

	} else {
		err = mlog_flush_abuf(mp, layout, skip_ser);
		if (err)
			mp_pr_err("mpool %s, mlog 0x%lx log block flush failed",
				  err, mp->pds_name, (ulong)layout->eld_objid);
	}

	if (err) {
		/* If flush failed, free all log pages except the first one. */
		start = 1;
		end   = abidx;
		fsucc = false;
	} else {
		/* If flush succeeded, free all log pages except the last one.*/
		start = 0;
		end   = abidx - 1;
	}
	mlog_free_abuf(lstat, start, end);

	if (FORCE_4KA(lstat))
		mlog_flush_posthdlr_4ka(mp, layout, fsucc);
	else
		mlog_flush_posthdlr(mp, layout, fsucc);

	return err;
}

/**
 * mlog_close()
 *
 * Flush and close log and release resources; no op if log is not open.
 *
 * Returns: 0 on sucess; merr_t otherwise
 */
merr_t mlog_close(struct mpool_descriptor *mp, struct mlog_descriptor *mlh)
{
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);
	struct mlog_stat              *lstat = NULL;

	merr_t err  = 0;
	bool   skip_ser = false;

	if (!layout)
		return merr(EINVAL);

	pmd_obj_wrlock(mp, layout);

	lstat = (struct mlog_stat *)layout->eld_lstat;

	if (!lstat) {
		/* Log already closed */
		pmd_obj_wrunlock(mp, layout);

		return 0;
	}

	/*
	 * flush log if potentially dirty and remove layout from
	 * open list
	 */
	if (lstat->lst_abdirty) {
		err = mlog_logblocks_flush(mp, layout, skip_ser);
		lstat->lst_abdirty = false;
		if (err)
			mp_pr_err("mpool %s, mlog 0x%lx close, log block flush failed",
				  err, mp->pds_name, (ulong)layout->eld_objid);
	}

	mlog_stat_free(layout);

	/* Reset Mlog flags */
	layout->eld_flags &= (~MLOG_OF_SKIP_SER);

	pmd_obj_wrunlock(mp, layout);

	return err;
}

/**
 * mlog_flush()
 *
 * Flush mlog; no op if log is not open.
 *
 * Returns: 0 on success; merr_t otherwise
 */
merr_t mlog_flush(struct mpool_descriptor *mp, struct mlog_descriptor *mlh)
{
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);
	struct mlog_stat              *lstat = NULL;
	merr_t err  = 0;
	bool   skip_ser = false;

	if (!layout)
		return merr(EINVAL);

	pmd_obj_wrlock(mp, layout);

	lstat = (struct mlog_stat *)layout->eld_lstat;

	if (!lstat) {
		pmd_obj_wrunlock(mp, layout);
		return merr(EINVAL);
	}

	/* flush log if potentially dirty */
	if (lstat->lst_abdirty) {
		err = mlog_logblocks_flush(mp, layout, skip_ser);
		lstat->lst_abdirty = false;
	}

	pmd_obj_wrunlock(mp, layout);

	return err;
}

/**
 * mlog_gen()
 *
 * Get generation number for log; log can be open or closed.
 *
 * Returns: 0 if successful; merr_t otherwise
 */
merr_t
mlog_gen(struct mpool_descriptor *mp, struct mlog_descriptor *mlh, u64 *gen)
{
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);

	*gen = 0;

	if (!layout)
		return merr(EINVAL);

	pmd_obj_rdlock(mp, layout);
	*gen = layout->eld_gen;
	pmd_obj_rdunlock(mp, layout);

	return 0;
}

/**
 * mlog_empty()
 *
 * Determine if log is empty; log must be open.
 *
 * Returns: 0 if successful; merr_t otherwise
 */
merr_t
mlog_empty(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	bool                    *empty)
{
	merr_t                         err = 0;
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);
	struct mlog_stat              *lstat = NULL;

	*empty = false;

	if (!layout)
		return merr(EINVAL);

	pmd_obj_rdlock(mp, layout);

	if (layout->eld_lstat) {
		lstat = (struct mlog_stat *)layout->eld_lstat;
		if ((!lstat->lst_wsoff &&
		     (lstat->lst_aoff == OMF_LOGBLOCK_HDR_PACKLEN)))
			*empty = true;
	} else {
		err = merr(ENOENT);
	}

	pmd_obj_rdunlock(mp, layout);

	if (err)
		mp_pr_err("mpool %s, determining if mlog 0x%lx is empty, inconsistency: no mlog status",
			  err, mp->pds_name, (ulong)layout->eld_objid);

	return err;
}

/**
 * mlog_len()
 *
 * Returns the raw mlog bytes consumed. log must be open.
 * Need to account for both metadata and user bytes while computing the
 * log length.
 */
merr_t
mlog_len(struct mpool_descriptor *mp, struct mlog_descriptor *mlh, u64 *len)
{
	struct ecio_layout_descriptor *layout;
	struct mlog_stat              *lstat;
	merr_t                         err = 0;

	layout = mlog2layout(mlh);

	if (!layout)
		return merr(EINVAL);

	pmd_obj_rdlock(mp, layout);

	lstat = layout->eld_lstat;
	if (lstat)
		*len = ((u64) lstat->lst_wsoff * MLOG_SECSZ(lstat))
			+ lstat->lst_aoff;
	else
		err = merr(ENOENT);

	pmd_obj_rdunlock(mp, layout);

	if (err)
		mp_pr_err("mpool %s, determining mlog 0x%lx bytes consumed, inconsistency: no mlog status",
			  err, mp->pds_name, (ulong)layout->eld_objid);

	return err;
}

/**
 * mlog_update_append_idx()
 *
 * Check whether the active log block is full and update the append offsets
 * accordingly.
 *
 * Returns: 0 on sucess; merr_t otherwise
 */
static merr_t
mlog_update_append_idx(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout,
	bool                            skip_ser)
{
	struct mlog_stat  *lstat;

	merr_t err;
	u16    sectsz;
	u16    abidx;
	u8     asidx;
	u8     nseclpg;

	lstat   = layout->eld_lstat;
	sectsz  = MLOG_SECSZ(lstat);
	nseclpg = MLOG_NSECLPG(lstat);

	if (sectsz - lstat->lst_aoff < OMF_LOGREC_DESC_PACKLEN) {
		/*
		 * If the log block is full, move to the next log
		 * block in the buffer.
		 */
		abidx = lstat->lst_abidx;
		asidx = lstat->lst_wsoff - ((nseclpg * abidx) +
				lstat->lst_asoff);
		if (asidx == nseclpg - 1)
			++lstat->lst_abidx;
		++lstat->lst_wsoff;
		lstat->lst_aoff = OMF_LOGBLOCK_HDR_PACKLEN;
	}

	abidx = lstat->lst_abidx;
	if (!lstat->lst_abuf[abidx]) {
		/* Allocate a log page at 'abidx' */
		err = mlog_alloc_abufpg(mp, layout, abidx, skip_ser);
		if (err)
			return err;
	}

	return 0;
}

/**
 * mlog_append_marker()
 *
 * Append a marker (log rec with zero-length data field) of type mtype.
 *
 * Returns: 0 on sucess; merr_t otherwise
 * One of the possible errno values in merr_t:
 * EFBIG - if no room in log
 */
static merr_t
mlog_append_marker(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout,
	enum logrec_type_omf            mtype)
{
	struct mlog_stat            *lstat;
	struct omf_logrec_descriptor lrd;

	merr_t err;
	u16    sectsz;
	u16    abidx;
	u16    aoff;
	char  *abuf;
	off_t  lpgoff;
	u8     asidx;
	u8     nseclpg;
	bool   skip_ser = false;

	lstat   = layout->eld_lstat;
	sectsz  = MLOG_SECSZ(lstat);
	nseclpg = MLOG_NSECLPG(lstat);

	if (mlog_append_dmax(mp, layout) == -1) {
		/* mlog is already full, flush whatever we can */
		if (lstat->lst_abdirty) {
			(void)mlog_logblocks_flush(mp, layout, skip_ser);
			lstat->lst_abdirty = false;
		}

		return merr(EFBIG);
	}

	err = mlog_update_append_idx(mp, layout, skip_ser);
	if (err)
		return err;

	abidx  = lstat->lst_abidx;
	abuf   = lstat->lst_abuf[abidx];
	asidx  = lstat->lst_wsoff - ((nseclpg * abidx) + lstat->lst_asoff);
	lpgoff = asidx * sectsz;
	aoff   = lstat->lst_aoff;

	lrd.olr_tlen  = 0;
	lrd.olr_rlen  = 0;
	lrd.olr_rtype = mtype;

	assert(abuf != NULL);
	err = omf_logrec_desc_pack_htole(&lrd, &abuf[lpgoff + aoff]);
	if (!err) {
		lstat->lst_aoff = aoff + OMF_LOGREC_DESC_PACKLEN;
		err = mlog_logblocks_flush(mp, layout, skip_ser);
		lstat->lst_abdirty = false;
		if (err)
			mp_pr_err("mpool %s, mlog 0x%lx log block flush failed",
				  err, mp->pds_name, (ulong)layout->eld_objid);
	} else {
		mp_pr_err("mpool %s, mlog 0x%lx log record descriptor packing failed",
			  err, mp->pds_name, (ulong)layout->eld_objid);
	}

	return err;
}

/**
 * mlog_append_cstart()
 *
 * Append compaction start marker; log must be open with csem flag true.
 *
 * Returns: 0 on sucess; merr_t otherwise
 * One of the possible errno values in merr_t:
 * EFBIG - if no room in log
 */
merr_t
mlog_append_cstart(struct mpool_descriptor *mp, struct mlog_descriptor *mlh)
{
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);
	struct mlog_stat              *lstat = NULL;
	merr_t err = 0;

	if (!layout)
		return merr(EINVAL);

	pmd_obj_wrlock(mp, layout);

	lstat = layout->eld_lstat;
	if (!lstat) {
		pmd_obj_wrunlock(mp, layout);

		err = merr(ENOENT);
		mp_pr_err("mpool %s, in mlog 0x%lx, inconsistency: no mlog status",
			  err, mp->pds_name, (ulong)layout->eld_objid);
		return err;
	}

	if (!lstat->lst_csem || lstat->lst_cstart) {
		pmd_obj_wrunlock(mp, layout);

		err = merr(EINVAL);
		mp_pr_err("mpool %s, in mlog 0x%lx, inconsistent state %u %u",
			  err, mp->pds_name, (ulong)layout->eld_objid,
			  lstat->lst_csem, lstat->lst_cstart);
		return err;
	}

	err = mlog_append_marker(mp, layout, OMF_LOGREC_CSTART);
	if (err) {
		pmd_obj_wrunlock(mp, layout);

		mp_pr_err("mpool %s, in mlog 0x%lx, marker append failed",
			  err, mp->pds_name, (ulong)layout->eld_objid);
		return err;
	}

	lstat->lst_cstart = 1;
	pmd_obj_wrunlock(mp, layout);

	return 0;
}

/**
 * mlog_append_cend()
 *
 * Append compaction start marker; log must be open with csem flag true.
 *
 * Returns: 0 on sucess; merr_t otherwise
 * One of the possible errno values in merr_t:
 * EFBIG - if no room in log
 */
merr_t
mlog_append_cend(struct mpool_descriptor *mp, struct mlog_descriptor *mlh)
{
	merr_t                         err = 0;
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);
	struct mlog_stat              *lstat = NULL;

	if (!layout)
		return merr(EINVAL);

	pmd_obj_wrlock(mp, layout);

	lstat = layout->eld_lstat;
	if (!lstat) {
		pmd_obj_wrunlock(mp, layout);

		err =  merr(ENOENT);
		mp_pr_err("mpool %s, mlog 0x%lx, inconsistency: no mlog status",
			  err, mp->pds_name, (ulong)layout->eld_objid);
		return err;
	}

	if (!lstat->lst_csem || !lstat->lst_cstart || lstat->lst_cend) {
		pmd_obj_wrunlock(mp, layout);

		err = merr(EINVAL);
		mp_pr_err("mpool %s, mlog 0x%lx, inconsistent state %u %u %u",
			  err, mp->pds_name, (ulong)layout->eld_objid,
			  lstat->lst_csem, lstat->lst_cstart,
			  lstat->lst_cend);
		return err;
	}

	err = mlog_append_marker(mp, layout, OMF_LOGREC_CEND);
	if (err) {
		pmd_obj_wrunlock(mp, layout);

		mp_pr_err("mpool %s, mlog 0x%lx, marker append failed",
			  err, mp->pds_name, (ulong)layout->eld_objid);
		return err;
	}

	lstat->lst_cend = 1;
	pmd_obj_wrunlock(mp, layout);

	return 0;
}

/**
 * memcpy_from_iov - Moves contents from an iovec to one or more destination
 * buffers.
 *
 * @iov    : One or more source buffers in the form of an iovec
 * @buf    : Destination buffer
 * @buflen : The length of either source or destination whichever is minimum
 * @nextidx: The next index in iov if the copy requires multiple invocations
 *           of memcpy_from_iov.
 *
 * No bounds check is done on iov. The caller is expected to give the minimum
 * of source and destination buffers as the length (buflen) here.
 */
static void
memcpy_from_iov(struct iovec *iov, char *buf, size_t buflen, int *nextidx)
{
	int i = *nextidx;
	int cp;

	if ((buflen > 0) && (iov[i].iov_len == 0))
		i++;

	while (buflen > 0) {

		cp = (buflen < iov[i].iov_len) ? buflen : iov[i].iov_len;

		if (iov[i].iov_base)
			memcpy(buf, iov[i].iov_base, cp);

		iov[i].iov_len  -= cp;
		iov[i].iov_base += cp;
		buflen          -= cp;
		buf             += cp;

		if (iov[i].iov_len == 0)
			i++;
	}

	*nextidx = i;
}

/**
 * mlog_append_data_internal() - Append data record with buflen data bytes
 * from buf; log must be open; if log opened with csem true then a compaction
 * start marker must be in place;
 *
 * @mp:       mpool descriptor
 * @mlh:      mlog descriptor
 * @iov:      iovec containing user data
 * @buflen:   length of the user buffer
 * @sync:     if true, then we do not return until data is on media
 * @skip_ser: client guarantees serialization
 *
 * Returns: 0 on sucess; merr_t otherwise
 * One of the possible errno values in merr_t:
 * EFBIG - if no room in log
 */
static merr_t
mlog_append_data_internal(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	struct iovec            *iov,
	u64                      buflen,
	int                      sync,
	bool                     skip_ser)
{
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);
	struct mlog_stat              *lstat = NULL;
	struct omf_logrec_descriptor   lrd;

	merr_t     err = 0;
	char      *abuf;
	off_t      lpgoff;
	int        dfirst;
	u64        bufoff;
	u64        rlenmax;
	u32        datasec;
	u16        aoff;
	u16        sectsz;
	u16        abidx;
	u8         asidx;
	u8         nseclpg;
	int        cpidx;

	lstat = (struct mlog_stat *)layout->eld_lstat;
	mlog_extract_fsetparms(lstat, &sectsz, &datasec, NULL, &nseclpg);

	bufoff = 0;
	dfirst = 1;
	cpidx  = 0;

	lrd.olr_tlen = buflen;

	while (true) {
		if ((bufoff != buflen) &&
				(mlog_append_dmax(mp, layout) == -1)) {

			/* mlog is full and there's more to write;
			 * mlog_append_dmax() should prevent this, but it lied.
			 */
			mp_pr_warn("mpool %s, mlog 0x%lx append, mlog free space was incorrectly reported",
				   mp->pds_name, (ulong)layout->eld_objid);

			return merr(EFBIG);
		}

		err = mlog_update_append_idx(mp, layout, skip_ser);
		if (err)
			return err;

		abidx  = lstat->lst_abidx;
		abuf   = lstat->lst_abuf[abidx];
		asidx  = lstat->lst_wsoff - ((nseclpg * abidx) +
				lstat->lst_asoff);
		lpgoff = asidx * sectsz;
		aoff   = lstat->lst_aoff;

		assert(abuf != NULL);

		rlenmax = min(
			(u64)(sectsz - aoff - OMF_LOGREC_DESC_PACKLEN),
			(u64)OMF_LOGREC_DESC_RLENMAX);

		if (buflen - bufoff <= rlenmax) {
			lrd.olr_rlen = buflen - bufoff;
			if (dfirst)
				lrd.olr_rtype = OMF_LOGREC_DATAFULL;
			else
				lrd.olr_rtype = OMF_LOGREC_DATALAST;
		} else {
			lrd.olr_rlen = rlenmax;
			if (dfirst) {
				lrd.olr_rtype = OMF_LOGREC_DATAFIRST;
				dfirst = 0;
			} else {
				lrd.olr_rtype = OMF_LOGREC_DATAMID;
			}
		}

		err = omf_logrec_desc_pack_htole(&lrd, &abuf[lpgoff + aoff]);
		if (err) {
			mp_pr_err("mpool %s, mlog 0x%lx, log record packing failed",
				  err, mp->pds_name, (ulong)layout->eld_objid);
			break;
		}

		lstat->lst_abdirty = true;

		aoff = aoff + OMF_LOGREC_DESC_PACKLEN;
		if (lrd.olr_rlen) {
			memcpy_from_iov(iov, &abuf[lpgoff + aoff],
					lrd.olr_rlen, &cpidx);
			aoff   = aoff + lrd.olr_rlen;
			bufoff = bufoff + lrd.olr_rlen;
		}
		lstat->lst_aoff = aoff;

		/*
		 * Flush log block if sync and no more to write (or)
		 * if the CFS is full.
		 */
		if ((sync && buflen == bufoff) ||
			(abidx == MLOG_NLPGMB(lstat) - 1 &&
			 asidx == nseclpg - 1 &&
			 sectsz - aoff < OMF_LOGREC_DESC_PACKLEN)) {

			err = mlog_logblocks_flush(mp, layout, skip_ser);
			lstat->lst_abdirty = false;
			if (err) {
				mp_pr_err("mpool %s, mlog 0x%lx, log block flush failed",
					  err, mp->pds_name,
					  (ulong)layout->eld_objid);
				break;
			}
		}

		assert(err == 0);
		if (bufoff == buflen)
			break;
	}

	return err;
}

/**
 * mlog_append_datav():
 */
merr_t
mlog_append_datav(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	struct iovec            *iov,
	u64                      buflen,
	int                      sync)
{
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);
	struct mlog_stat              *lstat = NULL;

	merr_t err   = 0;
	s64    dmax  = 0;
	bool   skip_ser  = false;

	if (!layout)
		return merr(EINVAL);

	if (layout->eld_flags & MLOG_OF_SKIP_SER)
		skip_ser = true;

	if (!skip_ser)
		pmd_obj_wrlock(mp, layout);

	lstat = (struct mlog_stat *)layout->eld_lstat;

	if (!lstat) {
		err = merr(ENOENT);
		mp_pr_err("mpool %s, mlog 0x%lx, inconsistency: no mlog status",
			  err, mp->pds_name, (ulong)layout->eld_objid);
	} else if (lstat->lst_csem && !lstat->lst_cstart) {
		err = merr(EINVAL);
		mp_pr_err("mpool %s, mlog 0x%lx, inconsistent state %u %u",
			  err, mp->pds_name, (ulong)layout->eld_objid,
			  lstat->lst_csem, lstat->lst_cstart);
	} else {
		dmax = mlog_append_dmax(mp, layout);
		if (dmax < 0 || buflen > dmax) {
			err = merr(EFBIG);
			mp_pr_debug("mpool %s, mlog 0x%lx mlog full %ld",
				    err, mp->pds_name,
				    (ulong)layout->eld_objid, (long)dmax);

			/* Flush whatever we can. */
			if (lstat->lst_abdirty) {
				(void)mlog_logblocks_flush(mp, layout,
						skip_ser);
				lstat->lst_abdirty = false;
			}
		}
	}

	if (err) {
		if (!skip_ser)
			pmd_obj_wrunlock(mp, layout);
		return err;
	}

	err = mlog_append_data_internal(mp, mlh, iov, buflen, sync, skip_ser);
	if (err) {
		mp_pr_err("mpool %s, mlog 0x%lx append failed",
			  err, mp->pds_name, (ulong)layout->eld_objid);

		/* Flush whatever we can. */
		if (lstat->lst_abdirty) {
			(void)mlog_logblocks_flush(mp, layout, skip_ser);
			lstat->lst_abdirty = false;
		}
	}

	if (!skip_ser)
		pmd_obj_wrunlock(mp, layout);

	return err;
}

/**
 * mlog_append_data()
 */
merr_t
mlog_append_data(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	char                    *buf,
	u64                      buflen,
	int                      sync)
{
	struct iovec iov;

	iov.iov_base = buf;
	iov.iov_len  = buflen;

	return mlog_append_datav(mp, mlh, &iov, buflen, sync);
}

/**
 * mlog_read_data_init()
 *
 * Initialize iterator for reading data records from log; log must be open;
 * skips non-data records (markers).
 *
 * Returns: 0 on success; merr_t otherwise
 */
merr_t
mlog_read_data_init(struct mpool_descriptor *mp, struct mlog_descriptor *mlh)
{
	merr_t                         err = 0;
	struct ecio_layout_descriptor *layout = mlog2layout(mlh);
	struct mlog_stat              *lstat;
	struct mlog_read_iter         *lri;

	if (!layout)
		return merr(EINVAL);

	lstat = layout->eld_lstat;

	pmd_obj_wrlock(mp, layout);

	if (!lstat) {
		err = merr(ENOENT);
	} else {
		lri = &lstat->lst_citr;

		mlog_read_iter_init(layout, lstat, lri);
	}

	pmd_obj_wrunlock(mp, layout);

	return err;
}

/**
 * mlog_logblocks_load_media() - Read log blocks from media, upto a maximum
 * of 1 MiB.
 *
 * @mp:    mpool descriptor
 * @lri:   read iterator
 * @inbuf: buffer to into (output)
 */
merr_t
mlog_logblocks_load_media(
	struct mpool_descriptor   *mp,
	struct mlog_read_iter     *lri,
	char                     **inbuf)
{
	struct ecio_layout_descriptor  *layout;
	struct mlog_stat               *lstat;

	off_t  rsoff;
	int    remsec;
	u16    maxsec;
	u16    nsecs;
	u16    sectsz;
	merr_t err;
	bool   skip_ser = false;

	layout = lri->lri_layout;
	lstat  = layout->eld_lstat;
	mlog_extract_fsetparms(lstat, &sectsz, NULL, &maxsec, NULL);

	/*
	 * The read and append buffer must never overlap. So, the read buffer
	 * can only hold sector offsets in the range [0, lstat->lst_asoff - 1].
	 */
	if (lstat->lst_asoff < 0)
		remsec = lstat->lst_wsoff;
	else
		remsec = lstat->lst_asoff;

	if (remsec == 0) {
		err = merr(EBUG);
		mp_pr_err("mpool %s, objid 0x%lx, mlog read cannot be served from read buffer",
			  err, mp->pds_name, (ulong)lri->lri_layout->eld_objid);
		return err;
	}

	lri->lri_rbidx = 0;
	lri->lri_sidx  = 0;

	rsoff   = lri->lri_soff;
	remsec -= rsoff;
	assert(remsec > 0);
	nsecs   = min_t(u32, maxsec, remsec);

	if (layout->eld_flags & MLOG_OF_SKIP_SER)
		skip_ser = true;

	err = mlog_populate_rbuf(mp, lri->lri_layout, &nsecs, &rsoff, skip_ser);
	if (err) {
		mp_pr_err("mpool %s, objid 0x%lx, mlog read failed, nsecs: %u, rsoff: 0x%lx",
			  err, mp->pds_name,
			  (ulong)lri->lri_layout->eld_objid, nsecs, rsoff);

		lstat->lst_rsoff = lstat->lst_rseoff = -1;

		return err;
	}

	/*
	 * 'nsecs' and 'rsoff' can be changed by mlog_populate_rbuf, if the
	 * read offset is not page-aligned. Adjust lri_sidx and lst_rsoff
	 * accordingly.
	 */
	lri->lri_sidx     = lri->lri_soff - rsoff;
	lstat->lst_rsoff  = rsoff;
	lstat->lst_rseoff = rsoff + nsecs - 1;

	*inbuf = lstat->lst_rbuf[lri->lri_rbidx];
	*inbuf += lri->lri_sidx * sectsz;

	return 0;
}

/**
 * mlog_logblock_load_internal() - Read log blocks from either the read
 * buffer or media.
 *
 * @mp:    mpool descriptor
 * @lri:   read iterator
 * @inbuf: buffer to load into (output)
 */
static merr_t
mlog_logblock_load_internal(
	struct mpool_descriptor *mp,
	struct mlog_read_iter   *lri,
	char                   **inbuf)
{
	struct mlog_stat       *lstat;

	merr_t err = 0;
	off_t  rsoff;
	off_t  rseoff;
	off_t  soff;
	u16    nsecs;
	u16    rbidx;
	u16    nlpgs;
	u8     nseclpg;
	u8     rsidx;

	lstat = (struct mlog_stat *)lri->lri_layout->eld_lstat;

	nseclpg = MLOG_NSECLPG(lstat);
	rbidx   = lri->lri_rbidx;
	rsidx   = lri->lri_sidx;
	soff    = lri->lri_soff;
	rsoff   = lstat->lst_rsoff;
	rseoff  = lstat->lst_rseoff;

	if (rsoff < 0)
		goto media_read;

	/*
	 * If the read offset doesn't fall within the read buffer range,
	 * then media read.
	 */
	if ((soff < rsoff) || (soff > rseoff))
		goto media_read;

	do {
		/* If this is not the start of log block. */
		if (lri->lri_roff != 0)
			break;

		/* Check if there's unconsumed data in rbuf. */
		nsecs = rseoff - rsoff + 1;
		nlpgs = (nsecs + nseclpg - 1) / nseclpg;

		/* No. of sectors in the last log page. */
		if (rbidx == nlpgs - 1) {
			nseclpg = nsecs % nseclpg;
			nseclpg = nseclpg > 0 ? nseclpg : MLOG_NSECLPG(lstat);
		}
		/* Remaining sectors in the active log page? */
		if (rsidx < nseclpg - 1) {
			++rsidx;
			break;
		}
		/* Remaining log pages in the read buffer? */
		if (rbidx >= nlpgs - 1)
			goto media_read;

		/* Free the active log page and move to next one. */
		mlog_free_rbuf(lstat, rbidx, rbidx);
		++rbidx;
		rsidx = 0;

		break;
	} while (0);

	/* Serve data from the read buffer. */
	*inbuf  = lstat->lst_rbuf[rbidx];
	*inbuf += rsidx * MLOG_SECSZ(lstat);

	lri->lri_rbidx = rbidx;
	lri->lri_sidx  = rsidx;

	return 0;

media_read:
	err = mlog_logblocks_load_media(mp, lri, inbuf);
	if (err) {
		mp_pr_err("mpool %s, objid 0x%lx, mlog new read failed",
			  err, mp->pds_name, (ulong)lri->lri_layout->eld_objid);

		return err;
	}

	return 0;
}

/**
 * mlog_loopback_load()
 *
 * Load log block referenced by lri into lstat, update lri if first read
 * from this log block, and return a pointer to the log block and a flag
 * indicating if lri references first record in log block.
 *
 * Note: lri can reference the log block currently accumulating in lstat
 *
 * Returns: 0 on sucess; merr_t otherwise
 * One of the possible errno values in merr_t:
 * ENOMSG - if at end of log -- NB: requires an API change to signal without
 */
static merr_t
mlog_logblock_load(
	struct mpool_descriptor *mp,
	struct mlog_read_iter   *lri,
	char                   **inbuf,
	bool                    *first)
{
	merr_t             err    = 0;
	struct mlog_stat  *lstat  = NULL;
	int                lbhlen = 0;

	*inbuf = NULL;
	*first = false;
	lstat  = (struct mlog_stat *)lri->lri_layout->eld_lstat;

	if (!lri->lri_valid || lri->lri_soff > lstat->lst_wsoff) {
		/* lri is invalid; prior checks should prevent this */
		err = merr(EINVAL);
		mp_pr_err("mpool %s, invalid offset %u %ld %ld",
			  err, mp->pds_name,
			  lri->lri_valid, lri->lri_soff, lstat->lst_wsoff);
	} else if ((lri->lri_soff == lstat->lst_wsoff) ||
			(lstat->lst_asoff > -1 &&
			lri->lri_soff >= lstat->lst_asoff &&
			lri->lri_soff <= lstat->lst_wsoff)) {
		/*
		 * lri refers to the currently accumulating log block
		 * in lstat
		 */
		u16 abidx;
		u16 sectsz;
		u8  asidx;
		u8  nseclpg;

		if (!lri->lri_roff)
			/*
			 * first read with handle from this log block
			 * note: log block header length guaranteed that
			 * of latest version
			 */
			lri->lri_roff = OMF_LOGBLOCK_HDR_PACKLEN;

		if (lri->lri_soff == lstat->lst_wsoff &&
				lri->lri_roff > lstat->lst_aoff) {
			/* lri is invalid; prior checks should prevent this */
			err = merr(EINVAL);
			mp_pr_err("mpool %s, invalid next offset %u %u",
				  err, mp->pds_name,
				  lri->lri_roff, lstat->lst_aoff);
			goto out;
		} else if (lri->lri_soff == lstat->lst_wsoff &&
				lri->lri_roff == lstat->lst_aoff) {
			/* hit end of log */
			err = merr(ENOMSG);
			goto out;
		} else if (lri->lri_roff == OMF_LOGBLOCK_HDR_PACKLEN)
			*first = true;

		sectsz  = MLOG_SECSZ(lstat);
		nseclpg = MLOG_NSECLPG(lstat);

		abidx = (lri->lri_soff - lstat->lst_asoff) / nseclpg;
		asidx = lri->lri_soff - ((nseclpg * abidx) + lstat->lst_asoff);

		*inbuf = &lstat->lst_abuf[abidx][asidx * sectsz];
	} else {
		/*
		 * lri refers to an existing log block; fetch it if
		 * not cached
		 */
		err = mlog_logblock_load_internal(mp, lri, inbuf);
		if (!err) {
			/*
			 * note: log block header length must be based
			 * on version since not guaranteed to be the latest
			 */
			lbhlen = omf_logblock_header_len_le(*inbuf);

			if (lbhlen < 0) {
				err = merr(ENODATA);
				mp_pr_err("mpool %s, getting header length failed %ld",
					  err, mp->pds_name, (long)lbhlen);
			} else {
				if (!lri->lri_roff)
					/*
					 * first read with handle from
					 * this log block
					 */
					lri->lri_roff = lbhlen;

				if (lri->lri_roff == lbhlen)
					*first = true;
			}
		}
	}

out:
	if (err) {
		*inbuf = NULL;
		*first = false;
	}

	return err;
}

/**
 * mlog_read_data_next_impl()
 * @mp:
 * @mlh:
 * @skip:
 * @buf:
 * @buflen:
 * @rdlen:
 *
 * Return:
 *   EOVERFLOW: the caller must retry with a larger receive buffer,
 *   the length of an adequate receive buffer is returned in "rdlen".
 */
static merr_t
mlog_read_data_next_impl(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	bool                     skip,
	char                    *buf,
	u64                      buflen,
	u64                     *rdlen)
{
	merr_t                         err = 0;
	struct ecio_layout_descriptor *layout;
	struct mlog_stat              *lstat;
	u64                            bufoff  = 0;
	u64                            midrec = 0;
	struct omf_logrec_descriptor   lrd;
	bool                           recfirst = false;
	char                          *inbuf = NULL;
	u32                            sectsz = 0;
	struct mlog_read_iter         *lri = NULL;
	bool                           skip_ser = false;

	layout = mlog2layout(mlh);
	if (!layout)
		return merr(EINVAL);

	if (!mlog_objid(layout->eld_objid))
		return merr(EINVAL);

	if (layout->eld_flags & MLOG_OF_SKIP_SER)
		skip_ser = true;
	/*
	 * need write lock because loading log block to read updates
	 * lstat; currently have no use case requiring support
	 * for concurrent readers.
	 */
	if (!skip_ser)
		pmd_obj_wrlock(mp, layout);

	lstat = (struct mlog_stat *)layout->eld_lstat;

	if (lstat) {
		sectsz = MLOG_SECSZ(lstat);
		lri    = &lstat->lst_citr;

		if (!lri->lri_valid) {
			if (!skip_ser)
				pmd_obj_wrunlock(mp, layout);

			err = merr(EINVAL);
			mp_pr_err("mpool %s, mlog 0x%lx, invalid iterator",
				  err, mp->pds_name, (ulong)layout->eld_objid);
			return err;
		}
	}

	if (!lstat || !lri) {
		err = merr(ENOENT);
		mp_pr_err("mpool %s, mlog 0x%lx, inconsistency: no mlog status",
			  err, mp->pds_name, (ulong)layout->eld_objid);
	} else if (lri->lri_gen != layout->eld_gen ||
		   lri->lri_soff > lstat->lst_wsoff ||
		   (lri->lri_soff == lstat->lst_wsoff && lri->lri_roff >
		    lstat->lst_aoff) || lri->lri_roff > sectsz) {

		err = merr(EINVAL);
		mp_pr_err("mpool %s, mlog 0x%lx, invalid arguments gen %lu %lu offsets %ld %ld %u %u %u",
			  err, mp->pds_name, (ulong)layout->eld_objid,
			  (ulong)lri->lri_gen, (ulong)layout->eld_gen,
			  lri->lri_soff, lstat->lst_wsoff, lri->lri_roff,
			  lstat->lst_aoff, sectsz);
	} else if (lri->lri_soff == lstat->lst_wsoff &&
		   lri->lri_roff == lstat->lst_aoff) {
		/* hit end of log - do not error count */
		err = merr(ENOMSG);
	}

	if (err) {
		if (!skip_ser)
			pmd_obj_wrunlock(mp, layout);
		if (merr_errno(err) == ENOMSG) {
			err = 0;
			if (rdlen)
				*rdlen = 0;
		}

		return err;
	}

	bufoff = 0;
	midrec = 0;

	while (true) {
		/*
		 * get log block referenced by lri which can be accumulating
		 * buffer
		 */
		err = mlog_logblock_load(mp, lri, &inbuf, &recfirst);
		if (err) {
			if (merr_errno(err) == ENOMSG) {
				if (!skip_ser)
					pmd_obj_wrunlock(mp, layout);
				err = 0;
				if (rdlen)
					*rdlen = 0;

				return err;
			}

			mp_pr_err("mpool %s, mlog 0x%lx, getting log block failed",
				  err, mp->pds_name, (ulong)layout->eld_objid);
			break;
		}

		if ((sectsz - lri->lri_roff) < OMF_LOGREC_DESC_PACKLEN) {
			/* no more records in current log block */
			if (lri->lri_soff < lstat->lst_wsoff) {

				/* move to next log block */
				lri->lri_soff = lri->lri_soff + 1;
				lri->lri_roff = 0;
				continue;
			} else {
				/*
				 * hit end of log; return EOF even in case
				 * of a partial data record which is a valid
				 * failure mode and must be ignored
				 */
				if (bufoff)
					err = merr(ENODATA);

				bufoff = 0;	/* force EOF on partials! */
				break;
			}
		}

		/* parse next record in log block */
		omf_logrec_desc_unpack_letoh(&lrd, &inbuf[lri->lri_roff]);

		if (logrec_type_datarec(lrd.olr_rtype)) {
			/* data record */
			if (lrd.olr_rtype == OMF_LOGREC_DATAFULL ||
			    lrd.olr_rtype == OMF_LOGREC_DATAFIRST) {
				if (midrec && !recfirst) {
					err = merr(ENODATA);

					/*
					 * can occur mid data rec only if
					 * is first rec in log block indicating
					 * partial data rec at end of last
					 * block which is a valid failure
					 * mode; otherwise is a logging error
					 */
					mp_pr_err("mpool %s, mlog 0x%lx, inconsistent 1 data record",
						  err, mp->pds_name,
						  (ulong)layout->eld_objid);
					break;
				}
				/*
				 * reset copy-out; set midrec which
				 * is needed for DATAFIRST
				 */
				bufoff = 0;
				midrec = 1;
			} else if (lrd.olr_rtype == OMF_LOGREC_DATAMID ||
				   lrd.olr_rtype == OMF_LOGREC_DATALAST) {
				if (!midrec) {
					err = merr(ENODATA);

					/*
					 * must occur mid data record;
					 * logging error
					 */
					mp_pr_err("mpool %s, mlog 0x%lx, inconsistent 2 data record",
						  err, mp->pds_name,
						  (ulong)layout->eld_objid);
					break;
				}
			}

			/*
			 * this is inside a loop, but it is invariant;
			 * (and it cannot be done until after the unpack)
			 *
			 * return the necessary length to caller
			 */
			if (buflen < lrd.olr_tlen) {
				if (rdlen)
					*rdlen = lrd.olr_tlen;

				err = merr(EOVERFLOW);
				break;
			}

			/* copy-out data */

			lri->lri_roff = lri->lri_roff +
					OMF_LOGREC_DESC_PACKLEN;

			if (!skip)
				memcpy(&buf[bufoff], &inbuf[lri->lri_roff],
				       lrd.olr_rlen);

			lri->lri_roff = lri->lri_roff + lrd.olr_rlen;
			bufoff = bufoff + lrd.olr_rlen;

			if (lrd.olr_rtype == OMF_LOGREC_DATAFULL ||
			    lrd.olr_rtype == OMF_LOGREC_DATALAST)
				break;
		} else {
			/*
			 * non data record; just skip unless midrec which
			 * is a logging error
			 */
			if (midrec) {
				err = merr(ENODATA);
				mp_pr_err("mpool %s, mlog 0x%lx, inconsistent non-data record",
					  err, mp->pds_name,
					  (ulong)layout->eld_objid);
				break;
			}
			if (lrd.olr_rtype == OMF_LOGREC_EOLB)
				lri->lri_roff = sectsz;
			else
				lri->lri_roff = lri->lri_roff +
						OMF_LOGREC_DESC_PACKLEN
						+ lrd.olr_rlen;
		}
	}
	if (!err && rdlen)
		*rdlen = bufoff;
	else if ((merr_errno(err) != EOVERFLOW) && (merr_errno(err) != ENOMEM))
		/* handle only remains valid if buffer too small */
		lri->lri_valid = 0;

	if (!skip_ser)
		pmd_obj_wrunlock(mp, layout);

	return err;
}

/**
 * mlog_read_data_next()
 *
 * Read next data record into buffer buf of length buflen bytes; log must
 * be open; skips non-data records (markers).
 *
 * Iterator lri must be re-init if returns any error except ENOMEM
 * in merr_t
 *
 * Returns:
 *   0 on success; merr_t with the following errno values on failure:
 *   EOVERFLOW if buflen is insufficient to hold data record; can retry
 *   errno otherwise
 *
 *   Bytes read on success in the ouput param rdlen (can be 0 if appended a
 *   zero-length data record)
 */
merr_t
mlog_read_data_next(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	char                    *buf,
	u64                      buflen,
	u64                     *rdlen)
{
	return mlog_read_data_next_impl(mp, mlh, false, buf, buflen, rdlen);
}

/**
 * mlog_seek_read_data_next()
 *
 * Read next data record into buffer buf of length buflen bytes after skipping
 * bytes as required; log must open; skips non-data records (markers).
 *
 * Iterator lri must be re-init if returns any error except ENOMEM
 * in merr_t
 *
 * Returns:
 *   0 on success; merr_t with the following errno values on failure:
 *   EOVERFLOW if buflen is insufficient to hold data record; can retry
 *   errno otherwise
 *
 *   Bytes read on success in the ouput param rdlen (can be 0 if appended a
 *   zero-length data record)
 */
merr_t
mlog_seek_read_data_next(
	struct mpool_descriptor *mp,
	struct mlog_descriptor  *mlh,
	u64                      seek,
	char                    *buf,
	u64                      buflen,
	u64                     *rdlen)
{
	merr_t err;

	if (seek > 0) {
		u64 skip;

		skip = 0;
		err = mlog_read_data_next_impl(mp, mlh, true, NULL,
					       seek, &skip);
		if (err)
			return err;

		if (skip != seek) {
			err = merr(ERANGE);
			*rdlen = skip;
			return err;
		}

		if (!buf || buflen == 0) {
			*rdlen = skip;

			return 0;
		}
	}

	return mlog_read_data_next_impl(mp, mlh, false, buf, buflen, rdlen);
}

/**
 * mlog_append_dmax()
 *
 * Max data record that can be appended to log in bytes; -1 if no room
 * for a 0 byte data record due to record descriptor length.
 */
s64
mlog_append_dmax(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout)
{
	struct mlog_stat      *lstat;

	u64    lbmax;
	u64    lbrest;
	u32    sectsz;
	u32    datalb;

	lstat = (struct mlog_stat *)layout->eld_lstat;

	sectsz = MLOG_SECSZ(lstat);
	datalb = MLOG_TOTSEC(lstat);

	if (lstat->lst_wsoff >= datalb)
		return -1; /* log already full */

	lbmax  = (sectsz - OMF_LOGBLOCK_HDR_PACKLEN - OMF_LOGREC_DESC_PACKLEN);
	lbrest = (datalb - lstat->lst_wsoff - 1) * lbmax;

	if ((sectsz - lstat->lst_aoff) < OMF_LOGREC_DESC_PACKLEN) {
		/* current log block cannot hold even a record descriptor */
		if (lbrest)
			return lbrest;

		return -1;
	}
	/*
	 * can start in current log block and spill over to others (if any)
	 */
	return sectsz - lstat->lst_aoff - OMF_LOGREC_DESC_PACKLEN + lbrest;
}

/**
 * mlog_user_fsetparms_init()
 *
 * Initialize the sector size and total number of sectors for user-space
 * mlogs
 */
static merr_t
mlog_user_fsetparms_init(
	struct ecio_layout_descriptor  *layout,
	struct mpool_mlog              *mlh,
	struct mlog_props_ex           *props)
{
	struct mlog_user   *mlusr;

	if (!layout)
		return merr(EINVAL);

	mlusr = kzalloc(sizeof(*mlusr), GFP_KERNEL);
	if (!mlusr)
		return merr(ENOMEM);

	mlusr->ml_mlh      = mlh;
	mlusr->ml_secshift = props->lpx_secshift;
	mlusr->ml_totsec   = props->lpx_totsec;

	layout->eld_mlo->mlo_mlog = mlusr;

	return 0;
}

/**
 * mlog_user_desc_alloc()
 *
 * Allocate a minimal mlog descriptor for user space mlogs support
 */
struct mlog_descriptor *
mlog_user_desc_alloc(
	struct mpool_descriptor    *mp,
	struct mlog_props_ex       *props,
	struct mpool_mlog          *mlh)
{
	struct ecio_layout_descriptor  *layout;
	struct mlog_props              *mlprop;
	struct mpool_uuid                 uuid;

	merr_t err;

	mlprop = &props->lpx_props;
	memcpy(uuid.uuid, mlprop->lpr_uuid, MPOOL_UUID_SIZE);

	layout = ecio_user_layout_alloc(mp, &uuid, mlprop->lpr_objid,
					mlprop->lpr_gen);
	if (!layout)
		return NULL;

	err = mlog_user_fsetparms_init(layout, mlh, props);
	if (err) {
		ecio_user_layout_free(layout);
		return NULL;
	}

	return layout2mlog(layout);
}

/**
 * mlog_user_desc_free()
 *
 * Free the mlog descriptor used for user space mlogs support
 */
void mlog_user_desc_free(struct mlog_descriptor *mlh)
{
	struct ecio_layout_descriptor  *layout;

	layout = mlog2layout(mlh);
	if (!layout)
		return;

	kfree(layout->eld_mlo->mlo_mlog);

	ecio_user_layout_free(layout);
}


/**
 * mlog_user_desc_set()
 *
 * Set the generation and state in the mlog descriptor
 */
merr_t
mlog_user_desc_set(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	u64                         gen,
	u8                          state)
{
	struct ecio_layout_descriptor  *layout;
	merr_t err;

	if (!mp || !mlh)
		return merr(EINVAL);

	layout = mlog2layout(mlh);
	if (!layout)
		return merr(EINVAL);

	pmd_obj_wrlock(mp, layout);

	err = ecio_user_layout_set(layout, gen, state);

	pmd_obj_wrunlock(mp, layout);

	return err;
}
