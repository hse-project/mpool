/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_IMP_PRIV_H
#define MPOOL_MPOOL_IMP_PRIV_H

/**
 * mpool_name_get() - Get mpool name from the specified mpool handle
 *
 * @mp:       mpool handle
 * @mpname:   output buffer to store the mpool name
 * @mplen:    buffer len
 */
merr_t mpool_name_get(struct mpool *mp, char *mpname, size_t mplen);

#endif /* MPOOL_MPOOL_IMP_PRIV_H */
