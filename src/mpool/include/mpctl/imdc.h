/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_IMDC_PRIV_H
#define MPOOL_MPOOL_IMDC_PRIV_H

#include <util/mutex.h>

#include <mpool/mpool_ioctl.h>

#define MPC_MDC_MAGIC           0xFEEDFEED
#define MPC_NO_MAGIC            0xFADEFADE

struct mpool;
struct mpool_mlog;

/**
 * struct mpool_mdc: MDC handle
 *
 * @mdc_mp:     mpool handle (user client) or mpool desc (kernel client)
 * @mdc_logh1:  mlog 1 handle
 * @mdc_logh2:  mlog 2 handle
 * @mdc_alogh:  active mlog handle
 * @mdc_lock:   mdc mutex
 * @mdc_mpname: mpool name
 * @mdc_valid:  is the handle valid?
 * @mdc_magic:  MDC handle magic
 * @mdc_flags:	MDC flags
 *
 * Ordering:
 *     mdc handle lock (mdc_lock)
 *     mlog handle lock (ml_lock)
 *     mpool handle lock
 *     mpool core locks
 */
struct mpool_mdc {
	struct mpool       *mdc_mp;
	struct mpool_mlog  *mdc_logh1;
	struct mpool_mlog  *mdc_logh2;
	struct mpool_mlog  *mdc_alogh;
	struct mutex        mdc_lock;
	char                mdc_mpname[MPOOL_NAME_LEN_MAX];
	int                 mdc_valid;
	int                 mdc_magic;
	u8                  mdc_flags;
};

#endif /* MPOOL_MPOOL_IMDC_PRIV_H */
