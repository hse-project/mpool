/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPCTL_IMPOOL_H
#define MPCTL_IMPOOL_H

#include <stdbool.h>

#include <util/platform.h>
#include <util/atomic.h>
#include <util/mutex.h>
#include <util/page.h>

#include <mpool/mpool.h>
#include <mpool/mpool_ioctl.h>
#include <mpctl/pd_props.h>

#include <mpcore/mpcore.h>
#include <mpcore/mlog.h>
#include <mpool_version.h>

struct mpool_devrpt;
enum mp_status;

/* Magics for API handles */
#define MPC_MPOOL_MAGIC         0x21122112
#define MPC_MLOG_MAGIC          0xBADCAFE
#define MPC_NO_MAGIC            0xFADEFADE

/*
 * Mpctl Mlog Data Structures
 */

/*
 * This will be scaled down to something like 256 after KVDB consolidates to a
 * total of 2 MDCs.
 */
#define MAX_OPEN_MLOGS     516

/**
 * struct mpool_mlog:
 *
 * @ml_lock:   Lock to protect concurrent operations on an mlog
 * @ml_mpdesc: Minimal mpool descriptor initialized for user-space mlogs
 * @ml_mldesc: Minimal mlog descriptor initialized for user-space mlogs
 * @ml_objid:  Object ID
 * @ml_magic:  Magic no., initialized by get and reset by put
 * @ml_mpfd:   mpool fd
 * @ml_idx:    Index where this handle is stored in mpool lookup map
 * @ml_flags:  Mlog flags
 *
 * Ordering:
 *     mlog handle lock (ml_lock)
 *     mpool handle lock
 *     mpool core locks
 */
struct mpool_mlog {
	struct mutex                ml_lock;
	struct mpool               *ml_mp;
	struct mpool_descriptor    *ml_mpdesc;
	struct mlog_descriptor     *ml_mldesc;
	u64                         ml_objid;
	int                         ml_magic;
	int                         ml_mpfd;
	u16                         ml_idx;
	u8                          ml_flags;
};

/*
 * struct mp_mloghmap:
 * Used for user-space lookup from object ID to mlog handle
 *
 * @mlm_objid:     Object ID
 * @mlm_hdl:       Mlog handle
 * @mlm_refcnt:    tracks gets and put to know when to release the handle
 */
struct mp_mloghmap {
	u64                 mlm_objid;
	struct mpool_mlog  *mlm_hdl;
	int                 mlm_refcnt;
};

/**
 * struct mpool:
 * @mp_mlmap:  fixed size map from object ID to mlog handle
 * @mp_magic:
 * @mp_fd:
 * @mp_flags:
 * @mp_name: Mpool name
 * @mp_mlidx: next free index in mp_mlmap
 * @mp_mltot:  total occupied slots in mp_mlmap
 * @mp_lock:
 */
struct mpool {
	struct mp_mloghmap   mp_mlmap[MAX_OPEN_MLOGS];
	int                  mp_magic;
	int                  mp_fd;
	int                  mp_flags;
	char                 mp_name[MPOOL_NAMESZ_MAX]; /* mpool name */
	u16                  mp_mlidx;
	u16                  mp_mltot;
	struct mutex         mp_lock;
};

/**
 * mp_sb_erase() - Erase the mpool superblocks on the specified
 *                 paths(partitions)
 * @devicec: Number of devices
 * @devicev: Vector of device names
 * @devrpt: Device error report
 * @pools: (output) Will be populated with a list of the affected mpools
 * @pools_len: Max size of the buffer 'pools'
 */
mpool_err_t
mp_sb_erase(
	int                   devicec,
	char                **devicev,
	struct mpool_devrpt  *devrpt,
	char                 *pools,
	size_t                pools_len);

/**
 * mp_sb_magic_check() -
 * @device
 * @devrpt
 * Return mpool_err_t, if either can't read superblock, or superblock
 * contains MPOOL magic value
 */
mpool_err_t mp_sb_magic_check(char *device, struct mpool_devrpt *devrpt);

/**
 * mp_trim_device() -
 * @devicec: Number of devices
 * @devicev: Vector of device names
 * @devrpt: Device error report
 *
 * Format list of drives as per mpool's requirement
 */
mpool_err_t mp_trim_device(int devicec, char **devicev, struct mpool_devrpt *devrpt);

/**
 * mp_dev_activated() - check if a device belongs to a activated mpool.
 * @devpath: device path
 * @activated: output, true if the device belongs to a activated mpool.
 * @mp_name: if not NULL the mpool name is copied there.
 * @mp_name_sz:
 */
uint64_t mp_dev_activated(char *devpath, bool *activated, char *mp_name, size_t mp_name_sz);

#endif /* MPCTL_IMPOOL_H */
