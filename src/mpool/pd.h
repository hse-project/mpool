/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_PD_H
#define MPOOL_MPOOL_PD_H

#include <util/platform.h>
#include <mpctl/pd_props.h>

#include "omf_if.h"

#ifndef REQ_PREFLUSH
#define REQ_PREFLUSH  0x01
#endif

#ifndef REQ_FUA
#define REQ_FUA       0x02
#endif

struct mpool_dev_info;
struct pd_dev_parm;

/*
 * Common defs
 */

struct pd_file_private {
	int pfp_fd;
};

/**
 * struct pd_dev_parm -
 * @dpr_prop:		drive properties including zone parameters
 * @dpr_ops:            drive operations vector dispatch
 * @dpr_dev_private:    private info for implementation
 *
 */
struct pd_dev_parm {
	struct pd_prop	         dpr_prop;
	const struct pd_dev_ops *dpr_ops;
	void		        *dpr_dev_private;
};

/* Shortcuts */
#define dpr_zonepg     dpr_prop.pdp_zparam.dvb_zonepg
#define dpr_zonetot        dpr_prop.pdp_zparam.dvb_zonetot
#define dpr_cmdopt        dpr_prop.pdp_cmdopt

/*
 * pd file API functions - device dependent operations
 */

void pd_file_init(struct pd_dev_parm *dparm, struct pd_prop *pd_prop);

/**
 * pd_file_open() -
 * @path:
 * @dparm:
 *
 * Return:
 */
merr_t pd_file_open(const char *path, struct pd_dev_parm *dparm);

/**
 * pd_file_pwritev() -
 * @pd:
 * @iov:
 * @iovcnt:
 * @zoneaddr:
 * @boff: offset in bytes from the start of "zoneaddr". May be larger than
 *        a zone.
 * @op_flags:
 *
 * Return:
 */
merr_t
pd_file_pwritev(
	struct mpool_dev_info  *pd,
	struct iovec           *iov,
	int                     iovcnt,
	u64                     zoneaddr,
	u64                     boff,
	int                     op_flags);

/**
 * pd_file_preadv() -
 * @pd:
 * @iov:
 * @iovcnt:
 * @zoneaddr: target zone for this I/O
 * @boff:    byte offset into the target zone
 *
 * Return:
 */
merr_t
pd_file_preadv(
	struct mpool_dev_info  *pd,
	struct iovec           *iov,
	int                     iovcnt,
	u64                     zoneaddr,
	u64                     boff);

/**
 * pd_file_close() -
 * @dparm:
 *
 * Return:
 */
merr_t pd_file_close(struct pd_dev_parm *dparm);

#endif /* MPOOL_MPOOL_PD_H */
