/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/**
 * DOC: mlog module
 *
 * Defines functions for writing, reading, and managing the lifecycle of mlogs.
 *
 */

#ifndef MPOOL_MLOG_H
#define MPOOL_MLOG_H

#include <mpool/mpool.h>
#include <mpool/mpool_ioctl.h>

/*
 * Opaque handles for clients
 */
struct mpool_descriptor;
struct mlog_descriptor;
struct mpool_mlog;
struct mpool_obj_layout;

/*
 * mlog API functions
 */

/*
 * Error codes: all mlog fns can return one or more of:
 * -EINVAL = invalid fn args
 * -ENOENT = log not open or logid not found
 * -EFBIG = log full
 * -EMSGSIZE = cstart w/o cend indicating a crash during compaction
 * -ENODATA = malformed or corrupted log
 * -EIO = unable to read/write log on media
 * -ENOMEM = insufficient room in copy-out buffer
 * -EBUSY = log is in erasing state; wait or retry erase
 */

/**
 * mlog_open()
 *
 * Open committed log, validate contents, and return its generation number;
 * if log is already open just returns gen; if csem is true enforces compaction
 * semantics so that open fails if valid cstart/cend markers are not present.
 * @mp:
 * @mlh:
 * @flags:
 * @gen: output
 *
 * Returns: 0 if successful, mpool_err_t otherwise
 */
mpool_err_t
mlog_open(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	u8                          flags,
	u64                        *gen);

mpool_err_t mlog_close(struct mpool_descriptor *mp, struct mlog_descriptor *mlh);

mpool_err_t mlog_flush(struct mpool_descriptor *mp, struct mlog_descriptor *mlh);

mpool_err_t
mlog_gen(struct mpool_descriptor *mp, struct mlog_descriptor *mlh, u64 *gen);

mpool_err_t
mlog_empty(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	bool                       *empty);

mpool_err_t
mlog_len(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	u64                        *len);

mpool_err_t
mlog_append_cstart(struct mpool_descriptor *mp, struct mlog_descriptor *mlh);

mpool_err_t
mlog_append_cend(struct mpool_descriptor *mp, struct mlog_descriptor *mlh);

mpool_err_t
mlog_append_data(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	char                       *buf,
	u64                         buflen,
	int                         sync);

mpool_err_t
mlog_append_datav(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	struct iovec               *iov,
	u64                         buflen,
	int                         sync);

mpool_err_t
mlog_read_data_init(struct mpool_descriptor *mp, struct mlog_descriptor *mlh);

/**
 * mlog_read_data_next()
 * @mp:
 * @mlh:
 * @buf:
 * @buflen:
 * @rdlen:
 *
 * Returns:
 *   If merr_errno(return value) is EOVERFLOW, then "buf" is too small to
 *   hold the read data. Can be retried with a bigger receive buffer whose
 *   size is returned in rdlen.
 */
mpool_err_t
mlog_read_data_next(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	char                       *buf,
	u64                         buflen,
	u64                        *rdlen);

mpool_err_t
mlog_seek_read_data_next(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	u64                         seek,
	char                       *buf,
	u64                         buflen,
	u64                        *rdlen);

/*
 * Used for user-space mlogs support
 */
struct mlog_descriptor *
mlog_user_desc_alloc(
	struct mpool_descriptor    *mp,
	struct mlog_props_ex       *props,
	struct mpool_mlog          *mlh);

void mlog_user_desc_free(struct mlog_descriptor *mlh);

mpool_err_t
mlog_user_desc_set(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	u64                         gen,
	u8                          state);

mpool_err_t
mlog_stat_reinit(struct mpool_descriptor *mp, struct mlog_descriptor *mlh);

mpool_err_t
mlog_layout_get(
	struct mpool_descriptor    *mp,
	struct mlog_descriptor     *mlh,
	struct mpool_obj_layout    *lyt);

#endif
