// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

/*
 * Defines APIs related to device controller
 */

#include <sys/ioctl.h>
#include <util/uuid.h>
#include <util/page.h>
#include <util/minmax.h>

#include <mpool/mpool_devrpt.h>

#include "device_table.h"
#include "dev_cntlr.h"
#include "logging.h"

/**
 * generic_trim_device():
 * @dev:
 * @rcode:
 */
merr_t
generic_trim_device(
	const char     *dev,
	enum mpool_rc  *rcode)
{
	u64            range[2];
	merr_t         err = 0;
	struct stat    stats;
	int            fd;
	char           sysfs_dpath[PATH_MAX]; /* /sys/block/<dev_name> */
	u64            maxd_bytes;
	u64            grand_bytes;
	u64            dev_sz_bytes;
	unsigned long  cmd;

	fd = open(dev, O_WRONLY | O_CLOEXEC);
	if (-1 == fd) {
		*rcode = MPOOL_RC_OPEN;
		return merr(errno);
	}

	if (fstat(fd, &stats)) {
		err = merr(errno);
		*rcode = MPOOL_RC_STAT;
		close(fd);
		return err;
	}

	/* Get "/sys/block/<device name>" in sysfs_dpath. */
	err = sysfs_get_dpath(dev, sysfs_dpath, sizeof(sysfs_dpath));
	if (err)
		goto exit;

	/*
	 * Get the device size
	 */
	err = sysfs_get_val_u64(sysfs_dpath, "/size", 0, &dev_sz_bytes);
	if (err)
		goto exit;
	dev_sz_bytes *= 512; /* Always 512 bytes units  for "size" */

	/*
	 * Get the maximum size that can be discarded in one command.
	 */
	err = sysfs_get_val_u64(sysfs_dpath, "/queue/discard_max_bytes",
				0, &maxd_bytes);
	if (err)
		goto exit;

	err = sysfs_get_val_u64(sysfs_dpath, "/queue/discard_granularity",
				0, &grand_bytes);
	if (err)
		goto exit;

	if (maxd_bytes == 0 || grand_bytes == 0)
		goto exit;
	/*
	 * Round down max discard to a granularity multiple.
	 */
	range[1] = (maxd_bytes / grand_bytes) * grand_bytes;
	if (range[1] == 0) {
		mse_log(MPOOL_INFO
			"Discard parameters inconsistent for device %s, 0x%lx 0x%lx",
			dev, maxd_bytes, grand_bytes);
		goto exit;
	}

	cmd = BLKSECDISCARD;
	for (range[0] = 0; range[0] < dev_sz_bytes; range[0] += range[1]) {
		int    rc;

		/* Don't pass end of device. */
		if (range[0] + range[1] > dev_sz_bytes) {
			range[1] = dev_sz_bytes - range[0];
			/* Adjust to granularity. */
			range[1] = (range[1] / grand_bytes) * grand_bytes;
			if (range[1] == 0)
				break;
		}

		rc = ioctl(fd, cmd, &range);
		if (rc && cmd == BLKSECDISCARD) {
			cmd = BLKDISCARD;
			rc = ioctl(fd, cmd, &range);
		}

		if (rc) {
			err = merr(errno);
			mpool_elog(MPOOL_INFO
				   "Failed to trim device %s cmd %lu range 0x%lx 0x%lx, @@e",
				   err, dev, cmd, range[0], range[1]);
			goto exit;
		}
	}

exit:
	close(fd);

	return 0;
}

/**
 * generic_get_awsz() - Get atomic write size of a generic device
 * @dev:
 * @datasz:
 */
merr_t
generic_get_awsz(
	const char *dev,
	u32        *datasz)
{
	char   sysfs_dpath[PATH_MAX];
	u64    awsz = 0;
	int    fd, rc;

	if (!dev || !datasz)
		return merr(EINVAL);

	fd = open(dev, O_RDONLY);
	if (-1 == fd)
		return merr(errno);

	*datasz = 1 << PAGE_SHIFT;

	rc = ioctl(fd, BLKPBSZGET, &awsz);

	close(fd);

	if (rc || !awsz) {
		merr_t  err;

		err = sysfs_get_dpath(dev, sysfs_dpath, sizeof(sysfs_dpath));
		if (err)
			return 0;

		/* Get the awsz */
		err = sysfs_get_val_u64(sysfs_dpath,
					"/queue/physical_block_size", 0, &awsz);
		if (err)
			return 0;
	}

	if (awsz)
		*datasz = awsz;

	return 0;
}

/**
 * generic_get_optiosz() - Get optimal IO size of a generic device
 * @dev:
 * @iosz:
 */
merr_t
generic_get_optiosz(
	const char *dev,
	u32        *iosz)
{
	char   sysfs_dpath[PATH_MAX];
	u64    sz = 0;
	int    fd, rc;

	if (!dev || !iosz)
		return merr(EINVAL);

	fd = open(dev, O_RDONLY);
	if (-1 == fd)
		return merr(errno);

	*iosz = 32 << PAGE_SHIFT; /* 128K default */

	rc = ioctl(fd, BLKIOOPT, &sz);

	close(fd);

	if (rc || !sz) {
		merr_t  err;

		err = sysfs_get_dpath(dev, sysfs_dpath, sizeof(sysfs_dpath));
		if (err)
			return 0;

		err = sysfs_get_val_u64(sysfs_dpath, "/queue/optimal_io_size",
					0, &sz);
		if (err)
			return 0;
	}

	if (sz)
		*iosz = min_t(u32, *iosz, sz);

	return 0;
}

/*
 * Device interface table
 */
static struct dev_interface dev_interface_table[] = {
	{ DEVICE_PHYS_IF_UNKNOWN, ""},
	{ DEVICE_PHYS_IF_VIRTUAL, "/dev/vd"},
	{ DEVICE_PHYS_IF_VIRTUAL, "/dev/dm"},
	{ DEVICE_PHYS_IF_NVDIMM, "/dev/pmem"},
	{ DEVICE_PHYS_IF_NVME, "/dev/nvme"},
	{ DEVICE_PHYS_IF_SAS, "/dev/sd"},
	{ DEVICE_PHYS_IF_SATA, "/dev/sd"},
	{ DEVICE_PHYS_IF_TEST, "/dev/loop"},
	{ DEVICE_PHYS_IF_TEST, "/dev/md"},
};

enum device_phys_if
get_dev_interface(const char *path)
{
	struct dev_interface   *cur, *first;
	enum device_phys_if     unknown;

	int    len;
	char   rpath[PATH_MAX];

	unknown = DEVICE_PHYS_IF_UNKNOWN;

	if (!realpath(path, rpath))
		return unknown;

	cur   = &dev_interface_table[NELEM(dev_interface_table) - 1];
	first = &dev_interface_table[0];

	while (cur != first) {
		len = strlen(cur->di_prefix);
		if (strncmp(rpath, cur->di_prefix, len) == 0)
			break;
		cur--;
	}

	return cur == first ? unknown : cur->di_type;
}
