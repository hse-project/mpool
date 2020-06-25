/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_SB_PRIV_H
#define MPOOL_MPOOL_SB_PRIV_H

#include <util/platform.h>

struct mpool_dev_info;

/*
 * Drives have 2 superblocks.
 * + sb0 at byte offset 0
 * + sb1 at byte offset SB_AREA_SZ
 *
 * Read: sb0 is the authoritative copy, other copies are not used.
 */
/* Number of superblock per Physical Device. */
#define SB_SB_COUNT        2

/* Size in byte of the area occupied by a superblock. The superblock itself
 * may be smaller, but always starts at the beginning of its area.
 */
#define SB_AREA_SZ         (4096ULL)

/* Size in byte of an area located after the superblock areas. */
#define MDC0MD_AREA_SZ     (4096ULL)

/*
 * sb API functions
 */

/**
 * sb_magic_check() - check for sb magic value
 * @pd: struct mpool_dev_info *
 *
 * Determine if the mpool magic value exists in at least one place where
 * expected on drive pd.  Does NOT imply drive has a valid superblock.
 *
 * Note: only pd.status and pd.parm must be set; no other pd fields accessed.
 *
 * Return: 1 if found, 0 if not found, -(errno) if error reading
 */
int sb_magic_check(struct mpool_dev_info *pd);

/**
 * sb_erase() - erase superblock
 * @pd: struct mpool_dev_info *
 *
 * Erase superblock on drive pd.
 *
 * Note: only pd.status and pd.parm must be set; no other pd fields accessed.
 *
 * Return: 0 if successful; merr_t otherwise
 */
merr_t sb_erase(struct mpool_dev_info *pd);

#endif /* MPOOL_MPOOL_SB_PRIV_H */
