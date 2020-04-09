/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef DEV_CNTLR_H
#define DEV_CNTLR_H

/*
 * Declaration of APIs and structures related to device controller
 */

#include <sys/types.h>
#include <util/platform.h>
#include <mpool/mpool_ioctl.h>
#include <mpctl/pd_props.h>

#include "mpctl.h"

/*
 * struct dev_interface
 * @di_type: Controller type: NVME, SATA, SAS ...
 * @di_prefix: device path prefix
 */
struct dev_interface {
	enum device_phys_if             di_type;
	char                           *di_prefix;
};

/**
 * get_dev_interface() -
 * @path: device path
 */
enum device_phys_if
get_dev_interface(
	const char *path);

merr_t
generic_trim_device(
	const char     *dev,
	enum mpool_rc  *rcode);

merr_t
generic_get_awsz(
	const char *dev,
	u32        *datasz);

merr_t
generic_get_optiosz(
	const char *dev,
	u32        *iosz);

#endif /* DEV_CNTLR_H */
