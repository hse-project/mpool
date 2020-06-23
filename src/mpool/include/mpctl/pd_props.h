/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_PD_PROPS_H
#define MPOOL_PD_PROPS_H

#include <stdint.h>

#include <mpool/mpool.h>
#include <mpool/mpool_ioctl.h>

/**
 * imp_dev_get_prop() - get the device properties.
 *
 * @path:
 * @pd_prop:
 */
mpool_err_t imp_dev_get_prop(const char *path, struct pd_prop *pd_prop);

/**
 * device_is_full_device() - Determine if path points to full device
 * @path:  char *, path to device file, i.e. /dev/vdc
 *
 * Returns ESUCCESS if the path is to a device that is an entire device,
 * i.e. not a partition.
 */
mpool_err_t device_is_full_device(const char *path);

#endif
