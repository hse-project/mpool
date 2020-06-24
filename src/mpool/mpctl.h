/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPCTL_H
#define MPOOL_MPCTL_H

#include "mpool_err.h"

struct mpool_devrpt;

/**
 * Current status of the mpool
 */
enum mp_status {
	MP_UNDEF    = 0,
	MP_OPTIMAL  = 1,
	MP_FAULTED  = 2,
	MP_STAT_INVALID,
};

/**
 * mp_mc_features - Drive features that participate in media classes
 *	definition.
 * These values are ored in a 64 bits fields.
 */
enum mp_mc_features {
	MP_MC_FEAT_MLOG_TGT   = 0x1,
	MP_MC_FEAT_MBLOCK_TGT = 0x2,
	MP_MC_FEAT_CHECKSUM   = 0x4,
};

/**
 * enum mp_pd_state_omf - Pool drive state on media
 *
 * @MP_PD_UNDEF:      undefined; should never occur
 * @MP_PD_ACTIVE:     drive is an active member of the pool
 * @MP_PD_REMOVING:   drive is being removed from the pool per request
 * @MP_PD_REBUILDING: drive declared failed and its data being rebuilt
 * @MP_PD_DEFUNCT:    drive is no longer an active member of the pool
 */
enum mp_pd_state_omf {
	MP_PD_UNDEF      = 0,
	MP_PD_ACTIVE     = 1,
	MP_PD_REMOVING   = 2,
	MP_PD_REBUILDING = 3,
	MP_PD_DEFUNCT    = 4,
};
_Static_assert((MP_PD_DEFUNCT < 256), "enum pd_state_omf must fit in uint8_t");

/**
 * enum mp_pd_cmd_opt - drive command options
 * @MP_PD_CMD_DISCARD:	     the device has TRIM/UNMAP command.
 * @MP_PD_CMD_SECTOR_UPDATABLE: the device can be read/written with sector
 *	granularity.
 * @MP_PD_CMD_DIF_ENABLED:   T10 DIF is used on this device.
 * @MP_PD_CMD_SED_ENABLED:   Self encrypting enabled
 * @MP_PD_CMD_DISCARD_ZERO:  the device supports discard_zero
 * @MP_PD_CMD_RDONLY:        activate mpool with PDs in RDONLY mode,
 *                           write/discard commands are No-OPs.
 * Defined as a bit vector so can combine.
 * Fields holding such a vector should uint64_t.
 */
enum mp_pd_cmd_opt {
	MP_PD_CMD_NONE             = 0,
	MP_PD_CMD_DISCARD          = 0x1,
	MP_PD_CMD_SECTOR_UPDATABLE = 0x2,
	MP_PD_CMD_DIF_ENABLED      = 0x4,
	MP_PD_CMD_SED_ENABLED      = 0x8,
	MP_PD_CMD_DISCARD_ZERO     = 0x10,
	MP_PD_CMD_RDONLY           = 0x20,
};

/**
 * Device types.
 * @MP_PD_DEV_TYPE_BLOCK_STREAM: Block device implementing streams.
 * @MP_PD_DEV_TYPE_BLOCK_STD:    Standard (non-streams) device (SSD, HDD).
 * @MP_PD_DEV_TYPE_FILE:      File in user space for UT.
 * @MP_PD_DEV_TYPE_MEM:	      Memory semantic device. Such as NVDIMM
 *			      direct access (raw or dax mode).
 * @MP_PD_DEV_TYPE_ZONE:      zone-like device, such as open channel SSD
 *			      and SMR HDD (using ZBC/ZAC).
 * @MP_PD_DEV_TYPE_BLOCK_NVDIMM: Standard (non-streams) NVDIMM in sector mode.
 */
enum mp_pd_devtype {
	MP_PD_DEV_TYPE_BLOCK_STREAM = 1,
	MP_PD_DEV_TYPE_BLOCK_STD,
	MP_PD_DEV_TYPE_FILE,
	MP_PD_DEV_TYPE_MEM,
	MP_PD_DEV_TYPE_ZONE,
	MP_PD_DEV_TYPE_BLOCK_NVDIMM,
	MP_PD_DEV_TYPE_LAST = MP_PD_DEV_TYPE_BLOCK_NVDIMM,
};

/**
 * Device physical interface.
 * @DEVICE_PHYS_IF_UNKNOWN: unknown or unsupported
 * @DEVICE_PHYS_IF_VIRTUAL: virtual interface (VM)
 * @DEVICE_PHYS_IF_NVDIMM:  PMEM interface to NVDIMM
 * @DEVICE_PHYS_IF_NVME:
 * @DEVICE_PHYS_IF_SAS:
 * @DEVICE_PHYS_IF_SATA:    SATA or ATA
 */
enum device_phys_if {
	DEVICE_PHYS_IF_UNKNOWN = 0,
	DEVICE_PHYS_IF_VIRTUAL,
	DEVICE_PHYS_IF_NVDIMM,
	DEVICE_PHYS_IF_NVME,
	DEVICE_PHYS_IF_SAS,
	DEVICE_PHYS_IF_SATA,
	DEVICE_PHYS_IF_TEST,
	DEVICE_PHYS_IF_LAST = DEVICE_PHYS_IF_TEST,
};

_Static_assert((DEVICE_PHYS_IF_LAST < 256), "enum device_phys_if must fit in uint8_t");

/**
 * mp_list_mpool_by_device() - Write a comma separated list of mpools to buf to
 *                             which the devices in devicev belong
 * @devicec: Number of devices
 * @devicev: Vector of device names
 * @buf: (output) List of mpools that contain the devices in devicev
 * @buf_len: Max size of buf
 */
merr_t mp_list_mpool_by_device(int devicec, char **devicev, char *buf, size_t buf_len);

/**
 * imp_dev_alloc_get_prop() - allocate an array of pd the properties, update it
 * and return it.
 *
 * @dcnt:
 * @devices:
 * @pd_prop: allocated by this function, must be freed by caller.
 */
merr_t imp_dev_alloc_get_prop(int dcnt, char **devices, struct pd_prop **pd_prop);

/**
 * mp_get_dev_prop() - collect the devices properties.
 *
 * @dcnt:
 * @devices:
 */
struct pd_prop *mp_get_dev_prop(int dcnt, char **devices);

#endif /* MPOOL_MPCTL_H */
