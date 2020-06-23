// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#define _LARGEFILE64_SOURCE

#include <util/platform.h>
#include <util/minmax.h>
#include <util/page.h>
#include <util/string.h>

#include <mpool/mpool.h>

#include <device_table.h>
#include <dirent.h>
#include <linux/version.h>
#include <mpool_blkid/blkid.h>

#include "dev_cntlr.h"
#include "logging.h"

#include <assert.h>

#define MODEL_SZ      1024 /* Max size for the device model string */

static devp_get_t devtab_get_prop_file;
static devp_get_t devtab_get_prop_blk_micron;
static devp_get_t devtab_get_prop_generic_blk;

static struct dev_table_ent dev_table[] = {
	{MODEL_FILE, MP_PD_DEV_TYPE_FILE, devtab_get_prop_file},
	{MODEL_GENERIC_SSD, MP_PD_DEV_TYPE_BLOCK_STD, devtab_get_prop_generic_blk},
	{MODEL_GENERIC_HDD, MP_PD_DEV_TYPE_BLOCK_STD, devtab_get_prop_generic_blk},
	{MODEL_GENERIC_NVDIMM_SECTOR, MP_PD_DEV_TYPE_BLOCK_NVDIMM, devtab_get_prop_generic_blk},
	{MODEL_MICRON_SSD, MP_PD_DEV_TYPE_BLOCK_STD, devtab_get_prop_blk_micron},
	{MODEL_GENERIC_TEST, MP_PD_DEV_TYPE_BLOCK_STD, devtab_get_prop_generic_blk},
	{MODEL_VIRTUAL_DEV, MP_PD_DEV_TYPE_BLOCK_STD, devtab_get_prop_generic_blk},
};

/**
 * devtab_find_ent() - return the table entry corresponding to the model
 * @model: used to lookup the entries and find the corresponding one.
 *
 * Return: pointer on corresponding entry, NULL is no entry matches model.
 */
static struct dev_table_ent *devtab_find_ent(char *model)
{
	int i;

	for (i = 0; i < NELEM(dev_table); i++) {
		if (strcmp(model, dev_table[i].dev_model) == 0)
			return dev_table + i;
	}

	return NULL;
}

static u8 prop_file_sectsz = PAGE_SHIFT;

/**
 * isTestDevice() - return true if test device.
 * @x: "/dev/...."
 */
static bool isTestDevice(const char *x)
{
	return ((strncmp(x, "/dev/loop", strlen("/dev/loop")) == 0) ||
		(strncmp(x, "/dev/md", strlen("/dev/md")) == 0));
}

static bool isDeviceMapper(const char *dpath)
{
	char    rpath[PATH_MAX];
	char   *p;

	if (!realpath(dpath, rpath))
		return merr(errno);

	p = strrchr(rpath, '/');

	return !strncmp(++p, "dm", strlen("dm"));
}

/**
 * devtab_get_prop_file() - get the properties for the model "File" in the
 *	device table.
 * @dpath: not_used
 * @sysfs_dpath: not_used
 * @model: not used
 * @ent: entry in the device table.
 * @pd_prop: buffer containing the result.
 */
static merr_t
devtab_get_prop_file(
	const char           *dpath,
	const char           *ppath,
	char                 *sysfs_dpath,
	char                 *model,
	struct dev_table_ent *ent,
	struct pd_prop	     *pd_prop)
{
	off64_t	len;
	int	fd;
	merr_t	err;

	memset(pd_prop, 0, sizeof(*pd_prop));

	fd = open(dpath, O_RDONLY);
	if (-1 == fd) {
		err = merr(errno);
		mpool_elog(MPOOL_ERR "PD file %s props, open failed, @@e",
			 err, dpath);
		return err;
	}

	len = lseek64(fd, 0, SEEK_END);
	if (len < 0) {
		err = merr(errno);
		mpool_elog(MPOOL_ERR "PD file %s props, seek failed, @@e",
			 err, dpath);
		close(fd);
		return err;
	}

	close(fd);

	pd_prop->pdp_devsz = len;
	pd_prop->pdp_sectorsz = prop_file_sectsz;
	pd_prop->pdp_optiosz = 1 << prop_file_sectsz;
	strlcpy(pd_prop->pdp_didstr, ent->dev_model, sizeof(pd_prop->pdp_didstr));
	pd_prop->pdp_devtype = ent->devtype;
	assert(pd_prop->pdp_devtype == MP_PD_DEV_TYPE_FILE);
	pd_prop->pdp_mclassp = MP_MED_INVALID;
	pd_prop->pdp_cmdopt = MP_PD_CMD_SECTOR_UPDATABLE;

	return 0;
}

merr_t sysfs_get_val_u64(const char *sysfs_dpath, const char *suffix, bool log_nofile, u64 *val)
{
	char    path[PATH_MAX], line[64], *end;
	FILE   *fp;
	merr_t  err;

	strlcpy(path, sysfs_dpath, sizeof(path));
	strlcat(path, suffix, sizeof(path) - strlen(path) - 1);

	fp = fopen(path, "r");
	if (!fp) {
		err = merr(errno);
		if (log_nofile || (merr_errno(err) != ENOENT))
			mpool_elog(MPOOL_ERR "%s open(%s) failed", err, __func__, path);
		return err;
	}

	if (!fgets(line, sizeof(line), fp)) {
		err = merr(EIO);
		mpool_elog(MPOOL_ERR "%s read(%s) failed", err, __func__, path);
		fclose(fp);
		return err;
	}

	fclose(fp);

	end = NULL;
	errno = 0;

	*val = strtoul(line, &end, 0);
	if (errno || end == line) {
		err = merr(errno ?: EINVAL);
		if (end && *end == '\n')
			*end = '\000';
		mpool_elog(MPOOL_ERR "%s %s strtoul(%s) failed", err, __func__, path, line);
		return err;
	}

	return 0;
}

merr_t sysfs_get_val_str(char *sysfs_dpath, char *suffix, bool log_nofile, char *str, size_t strsz)
{
	char    path[PATH_MAX];
	merr_t  err;
	FILE   *fp;

	if (strsz == 0)
		return merr(EINVAL);

	str[0] = '\000';

	strlcpy(path, sysfs_dpath, sizeof(path));
	strncat(path, suffix, sizeof(path) - strlen(path) - 1);

	fp = fopen(path, "r");
	if (!fp) {
		err = merr(errno);
		if (log_nofile || (merr_errno(err) != ENOENT))
			mpool_elog(MPOOL_ERR "%s open(%s) failed", err, __func__, path);
		return err;
	}

	if (!fgets(str, strsz, fp)) {
		err = merr(EIO);
		mpool_elog(MPOOL_ERR "%s read(%s) failed", err, __func__, path);
		fclose(fp);
		return err;
	}

	fclose(fp);

	/* Remove leading and trailing whitespace */
	strimpull(str);

	if (!strlen(str)) {
		err = merr(EIO);
		mpool_elog(MPOOL_ERR "%s read(%s) empty", err, __func__, path);
		return err;
	}

	return 0;
}

/**
 * devtab_get_prop_generic_blk() - get the PD properties for a generic HDD or
 *	SSD.
 * @dpath: device path e.g. "/dev/nvme0n1" or
 * @sysfs_dpath: path of the device in sysfs: /sys/block/<device name>
 *	For ex: "/sys/block/nvme0n1". May be NULL.
 * @model:
 * @ent: entry in the device table.
 * @pd_prop: buffer containing the result.
 */
static merr_t
devtab_get_prop_generic_blk(
	const char           *dpath,
	const char           *ppath,
	char                 *sysfs_dpath,
	char                 *model,
	struct dev_table_ent *ent,
	struct pd_prop	     *pd_prop)
{
	u32     sz;
	u64     val;
	char    suffix[PATH_MAX];
	merr_t  err;

	if (!sysfs_dpath || !dpath) {
		err = merr(EINVAL);
		mpool_elog(MPOOL_ERR
			   "Getting PD properties failed due to invalid path pointers, sysfs_dpath %p, dpath %p, @@e",
			   err, sysfs_dpath, dpath);
		return err;
	}

	memset(pd_prop, 0, sizeof(*pd_prop));
	pd_prop->pdp_mclassp = MP_MED_INVALID;
	strlcpy(pd_prop->pdp_didstr, model, sizeof(pd_prop->pdp_didstr));
	pd_prop->pdp_devtype = ent->devtype;

	err = sysfs_get_val_u64(sysfs_dpath, "/queue/discard_granularity", 1, &val);
	if (err) {
		mpool_elog(MPOOL_ERR "Getting discard granularity for device %s failed, @@e",
			   err, dpath);
		return err;
	}
	if (val)
		pd_prop->pdp_cmdopt |= MP_PD_CMD_DISCARD;
	pd_prop->pdp_discard_granularity = val;

	err = sysfs_get_val_u64(sysfs_dpath, "/queue/discard_zeroes_data", 1, &val);
	if (err) {
		mpool_elog(MPOOL_ERR "Getting if discard zeroes data for device %s failed, @@e",
			   err, dpath);
		return err;
	}
	if (val)
		pd_prop->pdp_cmdopt |= MP_PD_CMD_DISCARD_ZERO;

	pd_prop->pdp_cmdopt |= MP_PD_CMD_SECTOR_UPDATABLE;

	err = generic_get_awsz(dpath, &sz);
	if (err)
		return err;

	if (!powerof2(sz)) {
		mse_log(MPOOL_ERR "AWU size %u for %s not a power of 2", sz, dpath);
		return merr(EINVAL);
	}

	pd_prop->pdp_sectorsz = __builtin_ctz(sz);

	err = generic_get_optiosz(dpath, &sz);
	if (err)
		return err;
	pd_prop->pdp_optiosz = sz;

	/*
	 * Get the number of sectors
	 */
	if (ppath) {
		/*
		 * Get partition size from <sysfs_dpath>/<pname>/size
		 * e.g. /sys/block/sda/sda1/size
		 */
		char   *pname;

		pname = strrchr(ppath, '/');
		if (pname == NULL) {
			err = merr(EBADF);
			mpool_elog(MPOOL_ERR "Getting partition name from partition %s failed, @@e",
				   err, ppath);
			return err;
		}
		pname++;
		strlcpy(suffix, "/", sizeof(suffix));
		strncat(suffix, pname, sizeof(suffix) - strlen(suffix) - 1);
		strncat(suffix, "/size", sizeof(suffix) - strlen(suffix) - 1);
	} else {
		/*
		 * Get device size from <sysfs_dpath>/size
		 * e.g. /sys/block/sda/size
		 */
		strlcpy(suffix, "/size", sizeof(suffix));
	}

	err = sysfs_get_val_u64(sysfs_dpath, suffix, 1, &val);
	if (err) {
		mpool_elog(MPOOL_ERR "Getting %s size failed @@e", err, dpath);
		return err;
	}

	val *= 512;
	pd_prop->pdp_devsz = val;

	return 0;
}

/**
 * devtab_get_prop_blk_micron() - get the PD properties for a Micron SSD.
 *
 * There is no Micron drive specific handling at this point.
 *
 */
static merr_t
devtab_get_prop_blk_micron(
	const char           *dpath,
	const char           *ppath,
	char                 *sysfs_dpath,
	char                 *model,
	struct dev_table_ent *ent,
	struct pd_prop	     *pd_prop)
{
	return devtab_get_prop_generic_blk(dpath, ppath, sysfs_dpath, model, ent, pd_prop);
}

merr_t device_is_full_device(const char *path)
{
	char           sysfs_dpath[PATH_MAX]; /* /sys/block/<device name> */
	size_t         sysfs_dpath_sz = sizeof(sysfs_dpath);
	char           dpath[PATH_MAX];
	char           errbuf[128];
	struct stat    st;
	int            rc;
	merr_t         err;

	rc = stat(path, &st);
	if (rc) {
		err = merr(errno);
		(void)strerror_r(merr_errno(err), errbuf, sizeof(errbuf));
		mse_log(MPOOL_ERR "Getting device properties, getting file %s status failed %s",
			path, errbuf);
		return err;
	}

	if (!S_ISBLK(st.st_mode)) {
		/* zone devices not yet supported. */
		err = merr(ENOTBLK);
		mpool_elog(MPOOL_ERR
			   "Getting device %s properties, not a file nor a block device, @@e",
			   err, path);
		return err;
	}

	err = partname_to_diskname(dpath, path, sizeof(dpath));
	if (err) {
		mpool_elog(MPOOL_ERR "Getting device path of partition %s failed, @@e",
			   err, path);
		return err;
	}

	err = sysfs_get_dpath(dpath, sysfs_dpath, sysfs_dpath_sz);
	if (err)
		return err;

	rc = stat(sysfs_dpath, &st);
	if (rc) {
		err = merr(ENOTBLK);
		mpool_elog(MPOOL_ERR "Device %s is not whole block device, @@e", err, path);
		return err;
	}

	return 0;
}

merr_t sysfs_get_dpath(const char *dpath, char *sysfs_dpath, size_t sysfs_dpath_sz)
{
	char  *dname; /* device name */
	char   rpath[PATH_MAX];
	merr_t err;

	if (!realpath(dpath, rpath))
		return merr(errno);

	dname = strrchr(rpath, '/');
	if (dname == NULL) {
		err = merr(EBADF);
		mpool_elog(MPOOL_ERR "Getting device path %s failed, @@e", err, dpath);
		return err;
	}
	dname++;

	strlcpy(sysfs_dpath, "/sys/block/", sysfs_dpath_sz);
	strncat(sysfs_dpath, dname, sysfs_dpath_sz - strlen(sysfs_dpath) - 1);

	return 0;
}

/**
 * sysfs_is_scsi() - return true if the device is a scsi device
 * @hctl: string after the last "/" in the directory name pointed to by the
 *	symbolic link /sys/block/<device>/device
 *	If a scsi device, it should be of the form:
 *	<host number>:<channel number>:<target number>:<lun>
 *	For example:
 *	readlink /sys/block/sda/device
 *	../../../8:0:0:0
 *	hctl points on the "8".
 * @host: in the case the device is scsi, return the host number.
 */
static bool sysfs_is_scsi(char *hctl, u32 *host)
{
	u32 channel;
	u32 target;
	u32 lun;

	if (sscanf(hctl, "%u:%u:%u:%u", host, &channel, &target, &lun) != 4)
		return false;

	return true;
}

/**
 * sysfs_device_phys_if() - determine the physical interface used to
 *	communicate to the device
 * @sysfs_dpath:
 * @dpath: device path
 * @phys_if: output
 *
 * Return: different from 0 if allocation failure or inadequate buffers sizes.
 *	An unknown or unsupported device interface is not an error.
 *	If the interface is unknown or unsupported, phys_if is set to
 *	DEVICE_PHYS_IF_VIRTUAL and no error is returned.
 */
static merr_t
sysfs_device_phys_if(char *sysfs_dpath, const char *dpath, enum device_phys_if *phys_if)
{
	char    lpath[PATH_MAX];
	char    rpath[PATH_MAX];
	u32     host;
	char   *first;

	/* Set the default to a virtual interface. */
	*phys_if = DEVICE_PHYS_IF_VIRTUAL;

	/* Copy /sys/block/<device>/device in lpath */
	strlcpy(lpath, sysfs_dpath, sizeof(lpath));
	strncat(lpath, "/device", sizeof(lpath) - strlen(lpath) - 1);

	if (!realpath(lpath, rpath)) {
		mse_log(MPOOL_DEBUG "Cannot determine interface for %s, using \"virtual\"", lpath);
		return 0;
	}

	first = strrchr(rpath, '/');
	if (!first) {
		mse_log(MPOOL_DEBUG "Cannot determine interface for %s, using \"virtual\"", rpath);
		return 0;
	}

	first++;
	if (!strncmp(first, "virt", strlen("virt"))) {
		*phys_if = DEVICE_PHYS_IF_VIRTUAL;
		return 0;
	} else if (!strncmp(first, "nvme", strlen("nvme"))) {
		*phys_if = DEVICE_PHYS_IF_NVME;
		return 0;
	} else if (!strncmp(first, "btt", strlen("btt"))) {
		/* NVDIMM region type pmem, label access mode 'sector' */
		/* /dev/pmem<region number>s */
		*phys_if = DEVICE_PHYS_IF_NVDIMM;
		return 0;
	}

	if (sysfs_is_scsi(first, &host)) {
		/* SCSI interface, determine if SAS or SATA */
		*phys_if = get_dev_interface(dpath);
		return 0;
	}

	mse_log(MPOOL_DEBUG "Device discovery falling back to virtual interface for %s", first);

	return 0;
}

/*
 * is_micron_drive() - decide if a drive is a Micron SSD based on its model
 * string parsed from sysfs
 *
 * Micron SSD's model could start with
 * 1) MTFD: Micron Technology Flash Drive
 * 2) Micron/MICRON
 * Or it just use the actual model, for example S600 series SAS devices
 * S630DC/S650DC
 */
static bool is_micron_ssd(char *model, size_t sz)
{
	return ((sz > 4) && strncmp(model, "MTFD", 4) == 0) ||
		((sz >= 6) && strncmp(model, "Micron", 6) == 0) ||
		((sz >= 6) && strncmp(model, "MICRON", 6) == 0) ||
		((sz >= 6) && strncmp(model, "S630DC", 6) == 0) ||
		((sz >= 6) && strncmp(model, "S650DC", 6) == 0);
}

/**
 * dev_get_prop() - get the device (PD) properties.
 * @dpath: device path e.g. "/dev/nvme1n1"
 * @ppath: partition path e.g. "/dev/nvme1n1p1"
 * @pd_prop:
 */
static merr_t dev_get_prop(const char *dpath, const char *ppath, struct pd_prop *pd_prop)
{
	struct dev_table_ent *dev_ent;
	enum device_phys_if   phys_if = DEVICE_PHYS_IF_UNKNOWN;
	merr_t                err;
	char                  sysfs_dpath[PATH_MAX]; /* /sys/block/<dev_name> */
	char                  model[MODEL_SZ];
	u64                   hdd;

	/* Get "/sys/block/<device name>" in sysfs_dpath. */
	err = sysfs_get_dpath(dpath, sysfs_dpath, sizeof(sysfs_dpath));
	if (err)
		return err;

	/*
	 * Get the model string.
	 *
	 * It may the case that the file
	 * /sys/block/<device name>/device/model
	 * does not exist, and it is legit. That happens for example for
	 * the virtual drives. In that case, we pick one of the generic entries.
	 */
	if (isTestDevice(dpath)) {
		strcpy(model, "");
		phys_if = DEVICE_PHYS_IF_TEST;
		hdd = 0;
	} else if (isDeviceMapper(dpath)) {
		strlcpy(model, MODEL_VIRTUAL_DEV, MODEL_SZ);
		phys_if = DEVICE_PHYS_IF_VIRTUAL;
		hdd = 0;
	} else {
		err = sysfs_get_val_str(sysfs_dpath, "/device/model", 0, model, sizeof(model));
		if (err && (merr_errno(err) != ENOENT))
			return err;

		if (err == 0) {
			/* "model" file is present. */
			dev_ent = devtab_find_ent(model);
			if (dev_ent != NULL) {
				/*
				 * The device table contains an entry
				 * specifically for this model. Use it.
				 */
				err = dev_ent->dev_prop_get(dpath, ppath, sysfs_dpath, model,
							    dev_ent, pd_prop);
				return err;
			}
		} else
			/* No "model" file in sysfs. */
			strcpy(model, "");

		/*
		 * Get the type of physical interface the device is using.
		 */
		err = sysfs_device_phys_if(sysfs_dpath, dpath, &phys_if);
		if (phys_if == DEVICE_PHYS_IF_UNKNOWN) {
			err = merr(ENOENT);
			mpool_elog(MPOOL_DEBUG "Getting device %s physical interface failed, @@e",
				   err, dpath);
			return err;
		}

		/* HDD? */
		err = sysfs_get_val_u64(sysfs_dpath, "/queue/rotational", 1, &hdd);
		if (err != 0) {
			mpool_elog(MPOOL_ERR
				   "Getting device %s properties failed, can't get if rotational device @@e",
				   err, dpath);
			return err;
		}
	}

	/*
	 * Get a generic entry.
	 */
	if (hdd)
		dev_ent = devtab_find_ent(MODEL_GENERIC_HDD);
	else if (phys_if == DEVICE_PHYS_IF_NVDIMM)
		dev_ent = devtab_find_ent(MODEL_GENERIC_NVDIMM_SECTOR);
	else if (phys_if == DEVICE_PHYS_IF_TEST)
		dev_ent = devtab_find_ent(MODEL_GENERIC_TEST);
	else if (is_micron_ssd(model, MODEL_SZ))
		dev_ent = devtab_find_ent(MODEL_MICRON_SSD);
	else if (phys_if == DEVICE_PHYS_IF_VIRTUAL)
		dev_ent = devtab_find_ent(MODEL_VIRTUAL_DEV);
	else
		dev_ent = devtab_find_ent(MODEL_GENERIC_SSD);

	if (dev_ent == NULL) {
		err = merr(ENOTBLK);
		mpool_elog(MPOOL_ERR
			   "Getting device %s properties failed, no entry in the device table for generic %s, @@e",
			   err, dpath, hdd ? "hdd" : "ssd");
		return err;
	}

	err = dev_ent->dev_prop_get(dpath, ppath, sysfs_dpath, model, dev_ent, pd_prop);
	pd_prop->pdp_phys_if = phys_if;

	return err;
}

merr_t imp_dev_get_prop(const char *path, struct pd_prop *pd_prop)
{
	struct dev_table_ent   *dev_ent;
	struct stat	        st;

	char   errbuf[128];
	merr_t err;
	int    rc;
	char   dpath[PATH_MAX];

	rc = stat(path, &st);
	if (rc) {
		err = merr(errno);
		(void)strerror_r(merr_errno(err), errbuf, sizeof(errbuf));
		mpool_elog(MPOOL_ERR
			   "Getting device properties, getting file %s status failed %s, @@e",
			   err, path, errbuf);
		return err;
	}

	if (S_ISREG(st.st_mode)) {
		dev_ent = devtab_find_ent(MODEL_FILE);
		err = dev_ent->dev_prop_get(path, NULL, NULL, NULL, dev_ent, pd_prop);
		return err;
	}

	if (!S_ISBLK(st.st_mode)) {
		/* zone devices not yet supported. */
		err = merr(ENOTBLK);
		mpool_elog(MPOOL_ERR
			   "Getting device %s properties, not a file nor a block device, @@e",
			   err, path);
		return err;
	}

	err = partname_to_diskname(dpath, path, sizeof(dpath));
	if (err) {
		mpool_elog(MPOOL_ERR "Getting device path of partition %s failed, @@e",
			   err, path);
		return err;
	}

	if (!strncmp(dpath, path, PATH_MAX))
		path = NULL;

	/* Get device properties */
	err = dev_get_prop(dpath, path, pd_prop);
	if (err) {
		mpool_elog(MPOOL_ERR "Getting device %s properties failed, @@e", err, dpath);
		return err;
	}

	return err;
}

merr_t imp_dev_alloc_get_prop(int dcnt, char **devices, struct pd_prop **pd_prop)
{
	struct pd_prop *pdp;
	merr_t		err = 0;
	int		i;

	*pd_prop = malloc(dcnt*sizeof(struct pd_prop));
	if (*pd_prop == NULL) {
		err = merr(ENOMEM);
		mpool_elog(MPOOL_ERR
			   "Getting file %s properties, memory allocation (%d) failure, @@e",
			   err, devices[0], dcnt);
		return err;
	}

	pdp = *pd_prop;
	for (i = 0; i < dcnt; i++, pdp++) {
		err = imp_dev_get_prop(devices[i], pdp);
		if (err) {
			free(*pd_prop);
			*pd_prop = NULL;
			return err;
		}
	}

	return 0;
}

merr_t sysfs_pd_disable_wbt(const char *path)
{
	char    sysfs_path[NAME_MAX + 1];
	char    dpath[PATH_MAX];
	FILE   *fp;
	char   *dname;
	merr_t  err;
	int     n;

	err = partname_to_diskname(dpath, path, sizeof(dpath));
	if (err)
		return err;

	/* This needs to be the device mapper name for lv. */
	dname = strrchr(dpath, '/');

	/* Sysfs file path */
	n = snprintf(sysfs_path, sizeof(sysfs_path), "/sys/block%s/queue/wbt_lat_usec", dname);
	if (n >= sizeof(sysfs_path))
		return merr(ENAMETOOLONG);

	/* This sysfs may not exist for some linux versions,
	 * so don't return an error in that case...
	 */
	fp = fopen(sysfs_path, "wb");
	if (!fp)
		return 0;

	/* Write a zero into the file to turn off write throtting */
	fprintf(fp, "0");
	fclose(fp);

	mse_log(MPOOL_INFO "Turned off write throttling on %s", dpath);

	return 0;
}

merr_t partname_to_diskname(char *diskname, const char *partname, size_t diskname_len)
{
	struct stat   st;
	char          devname[32];
	dev_t         disk = 0;
	char         *devpath;

	if (!partname)
		return merr(EINVAL);

	if (stat(partname, &st))
		return merr(errno);

	if (!S_ISBLK(st.st_mode))
		return merr(ENOTBLK);

	/* Get the whole disk's devno from the partition */
	if (blkid_devno_to_wholedisk(st.st_rdev, devname, sizeof(devname), &disk))
		return merr(ENXIO);

	if (st.st_rdev == disk) {
		strlcpy(diskname, partname, diskname_len);
		return 0;
	}

	/* Get the whole disk's name from its devno */
	devpath = blkid_devno_to_devname(disk);
	if (devpath == NULL)
		return merr(EINVAL);

	strlcpy(diskname, devpath, diskname_len);
	free(devpath);

	return 0;
}

merr_t mpool_devinfo(const char *name, char *devpath, size_t devpathsz)
{
	char        sysfs_dpath[PATH_MAX];
	char        dm[PATH_MAX];
	char       *p;
	merr_t      err;
	size_t      n;

	err = sysfs_get_dpath(name, sysfs_dpath, sizeof(sysfs_dpath));
	if (err)
		return err;

	/* Fetch device name from sysfs for device-mapper devices.
	 */
	err = sysfs_get_val_str(sysfs_dpath, "/dm/name", 0, dm, sizeof(dm));
	if (err && merr_errno(err) != ENOENT)
		return err;

	if (err) {
		strlcpy(devpath, name, devpathsz);
		return 0;
	}

	/* Special handling for dm devices.
	 */
	n = strlcpy(devpath, "/dev/", devpathsz);
	if (n >= devpathsz)
		return merr(ENOSPC);

	devpath += strlen(devpath);

	if (n + strlen(dm) >= devpathsz)
		return merr(ENOSPC);

	p = dm;
	while (*p) {
		if (*p == '-' && *(p + 1) != '-')
			*p = '/';

		if (*p == '-')
			++p;

		*devpath++ = *p++;
	}
	*devpath = '\000';

	return 0;
}
