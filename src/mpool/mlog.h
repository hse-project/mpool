/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MLOG_PRIV_H
#define MPOOL_MLOG_PRIV_H

#include <util/platform.h>
#include <util/rwsem.h>

#include "pd.h"

#define MB       (1024 * 1024)

struct mlog_stat;
struct ecio_layout_descriptor;

/*
 * enum ecio_layout_state - object state flags
 *
 * ECIO_LYT_NONE:      no flags set
 * ECIO_LYT_COMMITTED: object is committed to media
 * ECIO_LYT_REMOVED:   object logically removed (aborted or deleted)
 */
enum ecio_layout_state {
	ECIO_LYT_NONE       = 0,
	ECIO_LYT_COMMITTED  = 1,
	ECIO_LYT_REMOVED    = 2,
};

/*
 * struct ecio_layout_mlo - information used only by mlog objects.
 * "mlo" = mlog only
 * @mlo_lstat:   mlog status
 * @mlo_mlog:    Used only for user space mlogs
 * @mlo_layout:  back pointer to the layout
 * @mlo_uuid:    unique ID per mlog
 */
struct ecio_layout_mlo {
	struct mlog_stat              *mlo_lstat;
	struct mlog_user              *mlo_mlog;
	struct ecio_layout_descriptor *mlo_layout;
	struct mpool_uuid              mlo_uuid;
};

/*
 * Object layout descriptor (in-memory version)
 *
 * LOCKING:
 * + objid: constant; no locking required
 * + lstat: lstat and *lstat are protected by pmd_obj_*lock()
 * + refcnt and isdel: protected by mmi_reflock of object's MDC
 * + all other fields: see notes
 */
/**
 * struct ecio_layout_descriptor
 *
 * The size of this structure should stay <= 128 bytes.
 * It contains holes that can be used to add new fields/information.
 *
 * NOTE:
 * + committed object fields (other): to update hold pmd_obj_wrlock()
 *   AND
 *   compactlock for object's mdc; to read hold pmd_obj_*lock()
 *   See the comments associated with struct pmd_mdc_info for
 *   further details.
 *
 * @eld_rwlock:  implements pmd_obj_*lock() for this layout
 * @eld_state:   enum ecio_layout_state
 * @eld_flags:   enum mlog_open_flags for mlogs,
 *               enum mblock_layout_flags for mblocks
 * @eld_objid:   object id associated with layout
 * @eld_mlo:     info. specific to an mlog, NULL for mblocks.
 * @eld_gen:     object generation
 */
struct ecio_layout_descriptor {
	struct rw_semaphore             eld_rwlock;
	u8                              eld_state;
	u8                              eld_flags;
	u64                             eld_objid;
	struct ecio_layout_mlo         *eld_mlo;
	u64                             eld_gen;
};

/* Shortcuts */
#define eld_lstat   eld_mlo->mlo_lstat
#define eld_uuid    eld_mlo->mlo_uuid

/**
 * calc_io_len() -
 * @iov:
 * @iovcnt:
 *
 * Return: total bytes in iovec list.
 */
static inline u64 calc_io_len(struct iovec *iov, int iovcnt)
{
	int    i = 0;
	u64    rval = 0;

	for (i = 0; i < iovcnt; i++)
		rval += iov[i].iov_len;

	return rval;
};

static inline enum obj_type_omf pmd_objid_type(u64 objid)
{
	enum obj_type_omf otype;

	otype = objid_type(objid);
	if (!objtype_valid(otype))
		return OMF_OBJ_UNDEF;
	else
		return otype;
}

static inline bool objtype_user(enum obj_type_omf otype)
{
	return (otype == OMF_OBJ_MBLOCK || otype == OMF_OBJ_MLOG);
}

static inline u8 objid_slot(u64 objid)
{
	return (objid & 0xFF);
}

/* True if objid is an mpool user object (versus mpool metadata object). */
static inline bool pmd_objid_isuser(u64 objid)
{
	return objtype_user(objid_type(objid)) && objid_slot(objid);
}

/**
 * struct mlog_fsetparms -
 *
 * @mfp_totsec: Total number of log blocks in mlog
 * @mfp_secpga: Is sector size page-aligned?
 * @mfp_lpgsz:  Size of each page in read/append buffer
 * @mfp_npgmb:  No. of pages in 1 MiB buffer
 * @mfp_sectsz: Sector size obtained from PD prop
 * @mfp_nsecmb: No. of sectors/log blocks in 1 MiB buffer
 * @mfp_nsecpg: No. of sectors/log blocks per page
 */
struct mlog_fsetparms {
	u32    mfp_totsec;
	bool   mfp_secpga;
	u16    mfp_lpgsz;
	u16    mfp_nlpgmb;
	u16    mfp_sectsz;
	u16    mfp_nsecmb;
	u8     mfp_nseclpg;
};

/**
 * struct mlog_user -
 *
 * @ml_mlh:     Mlog handle in control plane
 * @mfp_totsec: Total number of log blocks in mlog
 * @mfp_secshift: Sector size (2 exponent) obtained from PD prop
 */
struct mlog_user {
	struct mpool_mlog *ml_mlh;
	u32             ml_totsec;
	u16             ml_secshift;
};

/*
 * struct mlog_read_iter -
 *
 * @lri_layout: Layout of log being read
 * @lri_soff:   Sector offset of next log block to read from
 * @lri_gen:    Log generation number at iterator initialization
 * @lri_roff:   Next offset in log block soff to read from
 * @lri_rbidx:  Read buffer page index currently reading from
 * @lri_sidx:   Log block index in lri_rbidx
 * @lri_valid:  1 if iterator is valid; 0 otherwise
 */
struct mlog_read_iter {
	struct ecio_layout_descriptor *lri_layout;
	off_t lri_soff;
	u64   lri_gen;
	u16   lri_roff;
	u16   lri_rbidx;
	u8    lri_sidx;
	u8    lri_valid;
};

/**
 * struct mlog_stat - mlog open status (referenced by associated
 * struct ecio_layout_descriptor)
 *
 * @lst_citr:    Current mlog read iterator
 * @lst_mfp:     Mlog flush set parameters
 * @lst_abuf:    Append buffer, max 1 MiB size
 * @lst_rbuf:    Read buffer, max 1 MiB size - immutable
 * @lst_rsoff:   LB offset of the 1st log block in lst_rbuf
 * @lst_rseoff:  LB offset of the last log block in lst_rbuf
 * @lst_asoff:   LB offset of the 1st log block in CFS
 * @lst_wsoff:   Offset of the accumulating log block
 * @lst_abdirty: true, if append buffer is dirty
 * @lst_pfsetid: Prev. fSetID of the first log block in CFS
 * @lst_cfsetid: Current fSetID of the CFS
 * @lst_cfssoff: Offset within the 1st log block from where CFS starts
 * @lst_aoff:    Next byte offset[0, sectsz) to fill in the current log block
 * @lst_abidx:   Index of current filling page in lst_abuf
 * @lst_csem:    enforce compaction semantics if true
 * @lst_cstart:  valid compaction start marker in log?
 * @lst_cend:    valid compaction end marker in log?
 */
struct mlog_stat {
	struct mlog_read_iter  lst_citr;
	struct mlog_fsetparms  lst_mfp;
	char  **lst_abuf;
	char  **lst_rbuf;
	off_t   lst_rsoff;
	off_t   lst_rseoff;
	off_t   lst_asoff;
	off_t   lst_wsoff;
	bool    lst_abdirty;
	u32     lst_pfsetid;
	u32     lst_cfsetid;
	u16     lst_cfssoff;
	u16     lst_aoff;
	u16     lst_abidx;
	u8      lst_csem;
	u8      lst_cstart;
	u8      lst_cend;
};

#define MLOG_TOTSEC(lstat)  ((lstat)->lst_mfp.mfp_totsec)
#define MLOG_LPGSZ(lstat)   ((lstat)->lst_mfp.mfp_lpgsz)
#define MLOG_NLPGMB(lstat)  ((lstat)->lst_mfp.mfp_nlpgmb)
#define MLOG_SECSZ(lstat)   ((lstat)->lst_mfp.mfp_sectsz)
#define MLOG_NSECMB(lstat)  ((lstat)->lst_mfp.mfp_nsecmb)
#define MLOG_NSECLPG(lstat) ((lstat)->lst_mfp.mfp_nseclpg)

#define IS_SECPGA(lstat)    ((lstat)->lst_mfp.mfp_secpga)
#define FORCE_4KA(lstat)    (!(IS_SECPGA(lstat)) && mlog_force_4ka)

static inline bool mlog_objid(u64 objid)
{
	return objid && pmd_objid_type(objid) == OMF_OBJ_MLOG;
}

s64
mlog_append_dmax(
	struct mpool_descriptor        *mp,
	struct ecio_layout_descriptor  *layout);

#endif
