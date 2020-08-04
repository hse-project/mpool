/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_DEVRPT_H
#define MPOOL_DEVRPT_H

#ifndef __KERNEL__
#include <stdint.h>
#endif

/**
 * enum mpool_rcode_type: Reason failed to put a device path into service
 * @MPOOL_RC_NONE:    no problem encountered
 * @MPOOL_RC_ERRMSG:  problem encountered, the error message is in mdr_errmsg
 *
 * @MPOOL_RC_OPEN:    unable to open device path
 * @MPOOL_RC_PARM:    unable to query or set parms or parms invalid
 * @MPOOL_RC_MAGIC:   device has magic value and needs to be erased member
 * @MPOOL_RC_STAT:    device state or status does not permit operation
 * @MPOOL_RC_ENOMEM:  no system memory available
 *
 * @MPCTL_RC_DEVRW:   Unable to read/write device
 * @MPCTL_RC_NOTACTIVATED: The mpool is not deactivated
 */
enum mpool_rc {
	MPOOL_RC_NONE    = 0,
	MPOOL_RC_ERRMSG  = 1,

	/* Mpool Core values */
	MPOOL_RC_OPEN    = 2,
	MPOOL_RC_PARM    = 3,
	MPOOL_RC_MAGIC   = 4,
	MPOOL_RC_STAT    = 5,
	MPOOL_RC_ENOMEM  = 6,

	/* MPCTL values */
	MPCTL_RC_DEVRW        = 1001,
	MPCTL_RC_NOTACTIVATED = 1002,
	MPCTL_RC_DEVACTIVATED = 1003,
	MPCTL_RC_MP_NODEV     = 1004,
	MPCTL_RC_INVALDEV     = 1005,
	MPCTL_RC_MPEXIST      = 1006,
	MPCTL_RC_ENTNAM_INV   = 1007,
};

#define MPOOL_DEVRPT_SZ     120


/**
 * struct mpool_devrpt - Device access failure report
 * @mdr_rcode: reason could not put device into service
 * @mdr_off:   offset of drive path in function call argument
 * @mdr_msg:   Only relevant/valid if mdr_code is MPOOL_ERRMSG
 *	If mdr_code is MPOOL_ERRMSG, this is the error message to be
 *	displayed.
 *
 * Device access failure report
 *
 * NOTE:
 * + rcode = NONE and off = -1 if fn return value is not device specific
 */
struct mpool_devrpt {
	uint32_t    mdr_rcode; /* enum mpool_rc */
	int32_t     mdr_off;
	char        mdr_msg[MPOOL_DEVRPT_SZ];
};

void mpool_devrpt_init(struct mpool_devrpt *devrpt);

const char *mpool_devrpt_strerror(enum mpool_rc rcode);

/**
 * mpool_devrpt() - Update the error report with an error code
 *	that will be used to select a predefined error message.
 * @devrpt: may be NULL, in that case nothing is done.
 * @rcode:
 * @off:
 */
void mpool_devrpt(struct mpool_devrpt *devrpt, enum mpool_rc rcode, int off, const char *fmt, ...);

#endif
