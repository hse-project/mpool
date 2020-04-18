/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_MPCORE_H
#define MPOOL_MPOOL_MPCORE_H

/*
 * DOC: Module info
 *
 * Media pool (mpool) manager module.
 *
 * Defines functions to create and maintain mpools comprising multiple drives
 * in multiple media classes used for storing mblocks and mlogs.
 */

#include <util/uuid.h>
#include <util/inttypes.h>

#include <mpool/mpool.h>
#include <mpctl/pd_props.h>

#define MPOOL_OP_READ  0
#define MPOOL_OP_WRITE 1
#define PD_DEV_ID_PDUNAVAILABLE "DID_PDUNAVAILABLE"

/* Returns PD length in bytes. */
#define PD_LEN(_pd_prop) ((_pd_prop)->pdp_devsz)

/* Returns PD sector size (exponent, power of 2) */
#define PD_SECTORSZ(_pd_prop) ((_pd_prop)->pdp_sectorsz)

/* Return PD sector size mask */
#define PD_SECTORMASK(_pd_prop) \
	((uint64_t)(1 << PD_SECTORSZ(&pd->pdi_prop)) - 1)

/*
 * Maximum number of classes supported.
 */
#define MPOOL_MCLASS_MAX       3
#define MPOOL_DRIVES_MAX       3
#define MPOOL_COUNT_MAX        128

/* opaque handle for clients */
struct mpool_descriptor;
struct mp_obj_descriptor; /* for any mpool object: mblock, mlog, etc... */

/**
 * enum mpool_status -
 * @MPOOL_STAT_UNDEF:
 * @MPOOL_STAT_OPTIMAL:
 * @MPOOL_STAT_FAULTED:
 */
enum mpool_status {
	MPOOL_STAT_UNDEF    = 0,
	MPOOL_STAT_OPTIMAL  = 1,
	MPOOL_STAT_FAULTED  = 2,
	MPOOL_STAT_LAST = MPOOL_STAT_FAULTED,
};

_Static_assert((MPOOL_STAT_LAST < 256), "enum mpool_status must fit in u8");

/* Checksum types */
enum mp_cksum_type {
	MP_CK_UNDEF  = 0,
	MP_CK_NONE   = 1,
	MP_CK_DIF    = 2,
	MP_CK_NUMBER,
	MP_CK_INVALID = MP_CK_NUMBER
};

/**
 * enum prx_pd_status - Transient drive status.
 * @PRX_STAT_UNDEF:       undefined; should never occur
 * @PRX_STAT_ONLINE:      drive is responding to I/O requests
 * @PRX_STAT_SUSPECT:     drive is failing some I/O requests
 * @PRX_STAT_OFFLINE:     drive declared non-responsive to I/O requests
 * @PRX_STAT_UNAVAIL:     drive path not provided or open failed when
 *                        mpool was opened
 *
 * Transient drive status, these are stored as atomic_t variable
 * values
 */
enum prx_pd_status {
	PRX_STAT_UNDEF      = 0,
	PRX_STAT_ONLINE     = 1,
	PRX_STAT_SUSPECT    = 2,
	PRX_STAT_OFFLINE    = 3,
	PRX_STAT_UNAVAIL    = 4
};

_Static_assert((PRX_STAT_UNAVAIL < 256),
			"enum prx_pd_status must fit in uint8_t");

/**
 * enum pd_state_omf - Pool drive state on media
 *
 * @OMF_PD_UNDEF:      undefined; should never occur
 * @OMF_PD_ACTIVE:     drive is an active member of the pool
 * @OMF_PD_REMOVING:   drive is being removed from the pool per request
 * @OMF_PD_REBUILDING: drive declared failed and its data being rebuilt
 * @OMF_PD_DEFUNCT:    drive is no longer an active member of the pool
 */
enum pd_state_omf {
	OMF_PD_UNDEF      = 0,
	OMF_PD_ACTIVE     = 1,
	OMF_PD_REMOVING   = 2,
	OMF_PD_REBUILDING = 3,
	OMF_PD_DEFUNCT    = 4,
};
_Static_assert((OMF_PD_DEFUNCT < 256), "enum pd_state_omf must fit in uint8_t");

/**
 * Device types.
 * @PD_DEV_TYPE_BLOCK_STREAM: Block device implementing streams.
 * @PD_DEV_TYPE_BLOCK_STD:    Standard (non-streams) device (SSD, HDD).
 * @PD_DEV_TYPE_FILE:      File in user space for UT.
 * @PD_DEV_TYPE_MEM:	      Memory semantic device. Such as NVDIMM
 *			      direct access (raw or dax mode).
 * @PD_DEV_TYPE_ZONE:	      zone-like device, such as open channel SSD
 *			      and SMR HDD (using ZBC/ZAC).
 * @PD_DEV_TYPE_BLOCK_NVDIMM: Standard (non-streams) NVDIMM in sector mode.
 */
enum pd_devtype {
	PD_DEV_TYPE_BLOCK_STREAM = 1,
	PD_DEV_TYPE_BLOCK_STD,
	PD_DEV_TYPE_FILE,
	PD_DEV_TYPE_MEM,
	PD_DEV_TYPE_ZONE,
	PD_DEV_TYPE_BLOCK_NVDIMM,
	PD_DEV_TYPE_LAST = PD_DEV_TYPE_BLOCK_NVDIMM,
};

/**
 * enum pd_cmd_opt - drive command options
 * @PD_CMD_DISCARD:	     the device has TRIM/UNMAP command.
 * @PD_CMD_SECTOR_UPDATABLE: the device can be read/written with sector
 *	granularity.
 * @PD_CMD_DIF_ENABLED:   T10 DIF is used on this device.
 * @PD_CMD_SED_ENABLED:   Self encrypting enabled
 * @PD_CMD_DISCARD_ZERO:  the device supports discard_zero
 * @PD_CMD_RDONLY:        activate mpool with PDs in RDONLY mode, write/discard
 *                        commands are No-OPs.
 * Defined as a bit vector so can combine.
 * Fields holding such a vector should uint64_t.
 */
enum pd_cmd_opt {
	PD_CMD_NONE             = 0,
	PD_CMD_DISCARD          = 0x1,
	PD_CMD_SECTOR_UPDATABLE = 0x2,
	PD_CMD_DIF_ENABLED      = 0x4,
	PD_CMD_SED_ENABLED      = 0x8,
	PD_CMD_DISCARD_ZERO     = 0x10,
	PD_CMD_RDONLY           = 0x20,
};

/*
 * mpool API functions
 */

/**
 * mpool_sb_erase() - erase all superblocks on the specified paths
 * @dcnt: Number of paths
 * @dpaths: Vector of path names
 * @pd: pool drive properties
 * @devrpt: Device error report
 */
mpool_err_t
mpool_sb_erase(
	int                   dcnt,
	char                **dpaths,
	struct pd_prop       *pd,
	struct mpool_devrpt  *devrpt);

/**
 * mpool_sb_magic_check() -
 * @dpath: device path
 * @pd_prop: PD properties
 * @devrpt:
 * Return mpool_err_t, if either failed to read superblock or superblock
 * has MPOOL magic code
 */
mpool_err_t
mpool_sb_magic_check(
	char                   *dpath,
	struct pd_prop         *pd_prop,
	struct mpool_devrpt    *devrpt);

/**
 * mpool_user_desc_alloc() - Allocate a minimal mpool descriptor for user
 * space mlogs support
 * @mpname:
 */
struct mpool_descriptor *mpool_user_desc_alloc(char *mpname);

/**
 * mpool_user_desc_free() - Free the mpool descriptor used for user space mlog
 * support
 * @mp:
 */
void mpool_user_desc_free(struct mpool_descriptor *mp);

static inline enum mp_media_classp
mpool_mc_first_get(enum mp_media_classp mclassp)
{
	return (mclassp < MP_MED_BEST_EFFORT) ? mclassp :
		mclassp - MP_MED_BEST_EFFORT;
}

static inline bool mpool_mc_isbe(enum mp_media_classp mclassp)
{
	return mclassp >= MP_MED_BEST_EFFORT &&
		mclassp < MP_MED_BEST_EFFORT + MP_MED_NUMBER;
}

static inline bool mpool_mc_isvalid(enum mp_media_classp mclassp)
{
	return (mclassp >= 0 &&
		(mclassp < MP_MED_NUMBER || mpool_mc_isbe(mclassp)));
}

#endif /* MPOOL_MPOOL_MPCORE_H */
