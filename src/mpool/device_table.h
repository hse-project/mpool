/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_DEVICE_TABLE_H
#define MPOOL_DEVICE_TABLE_H

#include <mpctl/pd_props.h>

#include "mpctl.h"

/*
 * Well known models
 */
#define MODEL_FILE	  "File"
#define MODEL_GENERIC_SSD "Generic-SSD"
#define MODEL_GENERIC_HDD "Generic-HDD"
#define MODEL_GENERIC_NVDIMM_SECTOR "Generic-NVDIMM-sector"
#define MODEL_MICRON_SSD  "Micron-SSD"
#define MODEL_GENERIC_TEST  "Test-Device"
#define MODEL_VIRTUAL_DEV  "Virtual-Device"

struct dev_table_ent;

/**
 * devc_get_t - Prototype of function getting device properties.
 * @dpath: device path e.g. "/dev/nvme0n1" or
 * @sysfs_dpath: path of the device in sysfs: /sys/block/<device name>
 *	For ex: "/sys/block/nvme0n1". May be NULL.
 * @model
 * @ent: device table entry to which this function is hooked up.
 * @out: buffer containing the result.
 */
typedef merr_t (devp_get_t)(
	const char	     *dpath,
	const char	     *ppath,
	char		     *sysfs_dpath,
	char		     *model,
	struct dev_table_ent *ent,
	struct pd_prop	     *out);

/**
 * struct dev_table_ent - Device definition and how to get its properties.
 * @dev_model:    Content of the file /sys/block/<disk>/device/model
 *   or one of the generic values "Generic-SSD", "Generic-HDD", "File"
 * @devtype:      enum mp_pd_devtype
 * @dev_prop_get: function to get the properties of the device.
 */
struct dev_table_ent {
	char		   *dev_model;
	enum mp_pd_devtype  devtype;
	devp_get_t	   *dev_prop_get;
};

/**
 * sysfs_get_val_u64() - read and convert to u64 a value stored in the
 *	sysfs directory, under the device node. That is stored in:
 *	/sys/block/<device name><suffix>
 * @sysfs_dpath: /sys/block/<device name>
 * @suffix: path below the device.
 * @log_nofile: if true, the fact that the file is not present is logged.
 * @val: value returned
 */
merr_t
sysfs_get_val_u64(
	const char *sysfs_dpath,
	const char *suffix,
	bool        log_nofile,
	u64        *val);

/**
 * sysfs_get_val_str() - read a string stored in the
 *	sysfs directory, under the device node. That is the string stored in:
 *	/sys/block/<device name><suffix>
 * @sysfs_dpath: /sys/block/<device name>
 * @suffix: path below the device.
 * @log_nofile: if true, the fact that the file is not present is logged.
 * @str: string copied there as output.
 * @str_sz: size of buffer str.
 */
merr_t
sysfs_get_val_str(
	char  *sysfs_dpath,
	char  *suffix,
	bool  log_nofile,
	char  *str,
	size_t str_sz);

/**
 * sysfs_get_dpath() - from the device path, eg. /dev/sda
 *      return the path /sys/block/<device name>
 * @dpath: device path, should be of the form "/dev/<dev name>", eg. /dev/sdb
 *      where <dev name> is NOT the partition name but the device name.
 * @sysfs_dpath: device path in sysfs: /sys/block/<device name>
 * @sysfs_dpath_sz: size of buffer sysfs_dpath
 */
merr_t
sysfs_get_dpath(
	const char *dpath,
	char	   *sysfs_dpath,
	size_t	    sysfs_dpath_sz);

/**
 * sysfs_pd_disable_wbt() - disable PD write throttling
 * @ppath: partition path. e.g. /dev/nvme0n1p1, /dev/sdb1
 */
merr_t
sysfs_pd_disable_wbt(
	const char            *ppath);

/**
 * partname_to_diskname() -
 * @diskname: (output) Path of whole disk
 * @partname: Path of partition name
 */
merr_t
partname_to_diskname(
	char       *diskname,
	const char *partname,
	size_t      diskname_len);

/**
 * mpool_devinfo() -
 * @name:
 * @devpath:
 * @devpathsz:
 */
merr_t
mpool_devinfo(
	const char *name,
	char       *devpath,
	size_t      devpathsz);

#endif
