/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_IMBLOCK_PRIV_H
#define MPOOL_MPOOL_IMBLOCK_PRIV_H

#include <util/base.h>

#include <mpool/mpool.h>

struct mpool;
struct mpool_obj_layout;

/**
 * mpool_mblock_getprops_ex() - Get extended properties of an mblock
 *
 * @ds:       dataset handle
 * @mbh:      mblock handle
 * @props_ex: extended mblock properties (output)
 *
 * Return:
 *   %0 on success, <%0 on error
 */
mpool_err_t
mpool_mblock_getprops_ex(
	struct mpool              *ds,
	u64                        mbh,
	struct mblock_props_ex    *props_ex,
	struct mpool_obj_layout   *layout);

#endif /* MPOOL_MPOOL_IMBLOCK_PRIV_H */
