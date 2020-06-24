// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */
/*
 * Pool drive module with file backing.
 *
 * Defines functions for probing, reading, and writing drives in an mpool.
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

/* handle change in newer user space (fc28...) */
#ifndef IOV_MAX
#include <sys/uio.h>

#ifdef __IOV_MAX
#define IOV_MAX __IOV_MAX
#endif
#endif

#ifndef IOV_MAX
/* This is for IOV_MAX */
#define __need_IOV_MAX /* This way stdio_lim.h defines IOV_MAX */
#include <bits/stdio_lim.h>
#endif

#ifndef IOV_MAX
#error "Neither __IOV_MAX nor IOV_MAX is defined"
#endif

#include <util/platform.h>
#include <util/minmax.h>
#include <util/page.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <limits.h>
#include <linux/falloc.h>

#include "mpcore_defs.h"
#include "logging.h"

#include <mpool/mpool.h>

/*
 * pd API functions -- FILE versions of dparm ops
 */
void pd_file_init(struct pd_dev_parm *dparm, struct pd_prop *pd_prop)
{
	dparm->dpr_prop.pdp_devtype = PD_DEV_TYPE_FILE;
	dparm->dpr_prop = *pd_prop;
}

merr_t pd_file_open(const char *path, struct pd_dev_parm *dparm)
{
	struct pd_file_private *priv;
	int                     fd;

	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return merr(ENOMEM);

	fd = open(path, O_RDWR);
	if (fd == -1) {
		merr_t err = merr(errno);

		free(priv);
		return err;
	}

	priv->pfp_fd = fd;

	dparm->dpr_dev_private = priv;

	return 0;
}

merr_t pd_file_close(struct pd_dev_parm *dparm)
{
	struct pd_file_private *priv = dparm->dpr_dev_private;
	merr_t                  err = 0;
	int                     rc;

	if (!priv)
		return 0;

	fsync(priv->pfp_fd);

	rc = close(priv->pfp_fd);
	if (rc)
		err = merr(errno);

	dparm->dpr_dev_private = NULL;
	free(priv);

	return err;
}

/*
 * Write iov data to one or more consecutive virtual erase blocks on drive pd starting at
 * byte offset boff from block zoneaddr.
 *
 * Note: Only pd.status and pd.parm must be set; No other pd fields accessed.
 */
merr_t
pd_file_pwritev(
	struct mpool_dev_info  *pd,
	struct iovec           *iov,
	int                     iovcnt,
	u64                     zoneaddr,
	u64                     boff,
	int                     op_flags)
{
	struct pd_file_private *priv = pd->pdi_parm.dpr_dev_private;
	struct iovec           *iv_p = NULL;

	merr_t err = 0;
	u64    tiolen;
	u64    maxlen;
	u64    woff;
	u64    pd_len;
	u64    zonelen;
	int    ivc_cur;
	int    ivc_left;

	if (pd->pdi_parm.dpr_cmdopt & PD_CMD_RDONLY)
		return 0;

	pd_len = PD_LEN(&(pd->pdi_prop));
	zonelen = (u64)pd->pdi_zonepg << PAGE_SHIFT;
	woff = zoneaddr * zonelen + boff;

	if (woff >= pd_len) {
		err = merr(EINVAL);
		mpool_elog(MPOOL_ERR
			   "Writing on block device %s, offset 0x%lx 0x%lx 0x%lx beyond device end 0x%lx, @@e",
			   err, pd->pdi_name, (ulong)zoneaddr, (ulong)zonelen,
			   (ulong)boff, (ulong)pd_len);
		return err;
	}

	maxlen = pd_len - woff;
	tiolen = calc_io_len(iov, iovcnt);
	if (tiolen > maxlen) {
		err = merr(EINVAL);
		mpool_elog(MPOOL_ERR
			   "Writing on file %s, offset 0x%lx + length 0x%lx beyond device end 0x%lx, @@e",
			   err, pd->pdi_name, (ulong)woff, (ulong)tiolen, pd_len);
		return err;
	}

	/* The following loop is required to split the iovec into IOV_MAX chunks. */
	iv_p = iov;
	ivc_cur = 0;
	ivc_left = iovcnt;

	if (op_flags & REQ_PREFLUSH)
		fsync(priv->pfp_fd);

	while (ivc_left > 0) {
		ssize_t cc, iolen;

		ivc_cur = min_t(int, ivc_left, IOV_MAX);
		iolen = calc_io_len(iv_p, ivc_cur);

		cc = pwritev(priv->pfp_fd, iv_p, ivc_cur, woff);
		if (cc != iolen) {
			err = merr((-1 == cc) ? errno : EIO);
			mpool_elog(MPOOL_ERR
				   "Writing on file %s, pwritev failed %ld %ld %s, @@e", err,
				   pd->pdi_name, (long)cc, (long)iolen, strerror(merr_errno(err)));
			goto errout;
		}

		if (op_flags & REQ_FUA)
			fsync(priv->pfp_fd);

		woff += cc;
		iv_p += ivc_cur;
		ivc_left -= ivc_cur;
	}

errout:
	return err;
}

/*
 * Read iov data from one or more consecutive virtual
 * erase blocks on drive pd starting at byte offset boff
 * from block zoneaddr.
 *
 * Note: Only pd.status and pd.parm must be set; No other
 * pd fields accessed.
 */
merr_t
pd_file_preadv(
	struct mpool_dev_info  *pd,
	struct iovec           *iov,
	int                     iovcnt,
	u64                     zoneaddr,
	u64                     boff)
{
	struct pd_file_private *priv = pd->pdi_parm.dpr_dev_private;
	struct iovec           *iv_p = NULL;

	merr_t err = 0;
	u64    zonelen;
	u64    tiolen;
	u64    maxlen;
	u64    roff;
	int    ivc_cur;
	int    ivc_left;
	u64    pd_len;

	pd_len = PD_LEN(&(pd->pdi_prop));
	zonelen = (u64)pd->pdi_zonepg << PAGE_SHIFT;
	roff = zoneaddr*zonelen + boff;

	if (roff >= pd_len) {
		err = merr(EINVAL);
		mpool_elog(MPOOL_ERR
			   "File %s, read offset 0x%lx 0x%lx 0x%lx beyond device end 0x%lx, @@e",
			   err, pd->pdi_name, (ulong)zoneaddr, (ulong)zonelen,
			   (ulong)boff, (ulong)pd_len);
		return err;
	}

	tiolen = calc_io_len(iov, iovcnt);
	maxlen = pd_len - roff;
	if (tiolen > maxlen) {
		err = merr(EINVAL);
		mpool_elog(MPOOL_ERR
			   "File %s, read past device 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx, @@e",
			   err, pd->pdi_name, (ulong)zoneaddr, (ulong)zonelen,
			   (ulong)boff, (ulong)roff, (ulong)pd_len, (ulong)tiolen);
		return err;
	}

	/*
	 * The following loop is required to split the iovec in
	 * IOV_MAX chunks
	 */
	iv_p = iov;
	ivc_cur = 0;
	ivc_left = iovcnt;

	while (ivc_left) {
		ssize_t cc, iolen;

		ivc_cur = min_t(int, ivc_left, IOV_MAX);
		iolen = calc_io_len(iv_p, ivc_cur);

		cc = preadv(priv->pfp_fd, iv_p, ivc_cur, roff);
		if (cc != iolen) {
			err = merr((-1 == cc) ? errno : EIO);
			mpool_elog(MPOOL_ERR "File %s, preadv failed %ld %ld %s, @@e", err,
				   pd->pdi_name, (long)cc, (long)iolen, strerror(merr_errno(err)));
			goto errout;
		}

		roff += cc;
		iv_p += ivc_cur;
		ivc_left -= ivc_cur;
	}

errout:
	return err;
}
