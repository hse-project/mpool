/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_IMLOG_PRIV_H
#define MPOOL_MPOOL_IMLOG_PRIV_H

#include <util/base.h>

#include <mpool/mpool_ioctl.h>

#include "mpool_err.h"

struct mpool_descriptor;
struct mpool;
struct mpool_mlog;
struct mpool_obj_layout;

/**
 * mpool_mlog_xprops_get() - Get extended properties of an mlog
 *
 * @ds:    dataset handle
 * @mlh:   mlog handle
 * @props_ex: extended mlog properties (output)
 *
 * Return:
 *   %0 on success, <%0 on error
 */
merr_t
mpool_mlog_xprops_get(
	struct mpool               *ds,
	struct mpool_mlog          *mlh,
	struct mlog_props_ex       *props_ex);

/**
 * mpool_mlog_append_cstart() - Apppend a CSTART record
 *
 * @ds:    dataset handle
 * @mlh:   mlog handle
 *
 * Return:
 *   %0 on success, <%0 on error
 */
merr_t
mpool_mlog_append_cstart(
	struct mpool       *ds,
	struct mpool_mlog  *mlh);

/**
 * mpool_mlog_append_cend() - Apppend a CEND record
 *
 * @ds:    dataset handle
 * @mlh:   mlog handle
 *
 * Return:
 *   %0 on success, <%0 on error
 */
merr_t
mpool_mlog_append_cend(
	struct mpool       *ds,
	struct mpool_mlog  *mlh);

/**
 * mpool_mlog_gen() - Return mlog generation number
 *
 * @ds:    dataset handle
 * @mlh:   mlog handle
 * @gen:   generation no. (output)
 *
 * Return:
 *   %0 on success, <%0 on error
 */
merr_t
mpool_mlog_gen(
	struct mpool      *ds,
	struct mpool_mlog *mlh,
	u64               *gen);

/**
 * mpool_mlog_rw() - raw mpctl interface used for mlog IO
 *
 * @mlh:   mlog handle
 * @iov:   iovec
 * @iovc:  iov count
 * @off:   offset
 * @rw:    MPOOL_OP_READ or MPOOL_OP_WRITE
 *
 * Return:
 *   %0 on success, <%0 on error
 */
merr_t
mpool_mlog_rw(
	struct mpool_mlog  *mlh,
	struct iovec       *iov,
	int                 iovc,
	size_t              off,
	u8                  rw);

#endif /* MPOOL_MPOOL_IMLOG_PRIV_H */
