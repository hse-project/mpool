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
	u32    mfp_lpgsz;
	u16    mfp_nlpgmb;
	u16    mfp_sectsz;
	u16    mfp_nsecmb;
	u16    mfp_nseclpg;
};

/**
 * struct mlog_user -
 *
 * @ml_mlh:     Mlog handle in control plane
 * @mfp_totsec: Total number of log blocks in mlog
 * @mfp_secshift: Sector size (2 exponent) obtained from PD prop
 */
struct mlog_user {
	struct mpool_mlog  *ml_mlh;
	u32                 ml_totsec;
	u16                 ml_secshift;
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
	struct pmd_layout  *lri_layout;
	off_t               lri_soff;
	u64                 lri_gen;
	u16                 lri_roff;
	u16                 lri_rbidx;
	u16                 lri_sidx;
	u8                  lri_valid;
};

/**
 * struct mlog_stat - mlog open status (referenced by associated
 * struct pmd_layout)
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
	struct mlog_read_iter    lst_citr;
	struct mlog_fsetparms    lst_mfp;
	char                   **lst_abuf;
	char                   **lst_rbuf;
	void                    *lst_freebufs;
	off_t                    lst_rsoff;
	off_t                    lst_rseoff;
	off_t                    lst_asoff;
	off_t                    lst_wsoff;
	bool                     lst_abdirty;
	u32                      lst_pfsetid;
	u32                      lst_cfsetid;
	u16                      lst_cfssoff;
	u16                      lst_aoff;
	u16                      lst_abidx;
	u8                       lst_csem;
	u8                       lst_cstart;
	u8                       lst_cend;
};

#define MLOG_TOTSEC(lstat)  ((lstat)->lst_mfp.mfp_totsec)
#define MLOG_LPGSZ(lstat)   ((lstat)->lst_mfp.mfp_lpgsz)
#define MLOG_NLPGMB(lstat)  ((lstat)->lst_mfp.mfp_nlpgmb)
#define MLOG_SECSZ(lstat)   ((lstat)->lst_mfp.mfp_sectsz)
#define MLOG_NSECMB(lstat)  ((lstat)->lst_mfp.mfp_nsecmb)
#define MLOG_NSECLPG(lstat) ((lstat)->lst_mfp.mfp_nseclpg)

#define IS_SECPGA(lstat)    ((lstat)->lst_mfp.mfp_secpga)

/*
 * enum pmd_layout_state - object state flags
 *
 * PMD_LYT_NONE:      no flags set
 * PMD_LYT_COMMITTED: object is committed to media
 * PMD_LYT_REMOVED:   object logically removed (aborted or deleted)
 */
enum pmd_layout_state {
	PMD_LYT_NONE       = 0,
	PMD_LYT_COMMITTED  = 1,
	PMD_LYT_REMOVED    = 2,
};

/*
 * struct pmd_layout_mlpriv - information used only by mlog objects.
 * @mlp_uuid:    unique ID per mlog
 * @mlp_lstat:   mlog status
 * @mlp_mlog:    mlog user data
 */
struct pmd_layout_mlpriv {
	struct mpool_uuid   mlp_uuid;
	struct mlog_stat    mlp_lstat;
	struct mlog_user    mlp_mlog;
};


/**
 * struct pmd_layout
 *
 * @eld_rwlock:  implements pmd_obj_*lock() for this layout
 * @eld_mlpriv:  mlog private data
 * @eld_objid:   object id associated with layout
 * @eld_gen:     object generation
 * @eld_state:   enum pmd_layout_state
 * @eld_flags:   enum mlog_open_flags for mlogs
 */
struct pmd_layout {
	struct rw_semaphore        eld_rwlock;
	struct pmd_layout_mlpriv   eld_mlpriv;
	u64                        eld_objid;
	u64                        eld_gen;
	u8                         eld_state;
	u8                         eld_flags;
};

/* Shortcuts */
#define eld_lstat   eld_mlpriv.mlp_lstat
#define eld_uuid    eld_mlpriv.mlp_uuid

static inline bool mlog_objid(u64 objid)
{
	return objid && pmd_objid_type(objid) == OMF_OBJ_MLOG;
}

#endif /* MPOOL_MLOG_PRIV_H */
