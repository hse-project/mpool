// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <mpctl/impool.h>

#include "../mpool/discover.h"
#include "../mpool/device_table.h"

#include <util/string.h>

#include <sys/ioctl.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>

#include "mpool.h"
#include "ui_common.h"
#include "yaml.h"
#include "ls.h"

static
const match_table_t
devtype_table = {
	{ MP_PD_DEV_TYPE_BLOCK_STREAM, "stream" },
	{ MP_PD_DEV_TYPE_BLOCK_STD, "stdblk" },
	{ MP_PD_DEV_TYPE_FILE, "file" },
	{ MP_PD_DEV_TYPE_MEM, "nvdimm memory semantics" },
	{ MP_PD_DEV_TYPE_ZONE, "allocation units" },
	{ MP_PD_DEV_TYPE_BLOCK_NVDIMM, "nvdimm sector mode" },
	{ -1, NULL }
};

merr_t show_devtype(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u8, val))
		return merr(EINVAL);

	n = show_lookup(devtype_table, str, strsz, *(const u8 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

static merr_t show_pct(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u32, val))
		return merr(EINVAL);

	n = snprintf(str, strsz, "%d%%", *(const u32 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

static void
mpool_list_yaml_mclass(
	int                         mcxc,
	struct mpool_mclass_xprops *mcxv,
	int                         verbosity,
	struct yaml_context        *yc)
{
	u32     allocu, sectorsz;
	char    value[32];
	int     i;

	yaml_start_element_type(yc, "media_classes");

	for (i = 0; i < mcxc; ++i, ++mcxv) {
		show_media_classp(value, sizeof(value), &mcxv->mc_mclass, 0);
		yaml_start_element(yc, "mclass_name", value);

		/* Total */
		space_to_string(mcxv->mc_usage.mpu_total, value, sizeof(value));
		yaml_element_field(yc, "total_space", value);
		if (verbosity > 0) {
			show_u64_dec(value, sizeof(value), &mcxv->mc_usage.mpu_total, 0);
			yaml_element_field(yc, "total_space_bytes", value);
		}

		/* Usable */
		space_to_string(mcxv->mc_usage.mpu_usable, value, sizeof(value));
		yaml_element_field(yc, "usable_space", value);
		if (verbosity > 0) {
			show_u64_dec(value, sizeof(value), &mcxv->mc_usage.mpu_usable, 0);
			yaml_element_field(yc, "usable_space_bytes", value);
		}

		/* Used */
		space_to_string(mcxv->mc_usage.mpu_used, value, sizeof(value));
		yaml_element_field(yc, "allocated_space", value);
		if (verbosity > 0) {
			show_u64_dec(value, sizeof(value), &mcxv->mc_usage.mpu_used, 0);
			yaml_element_field(yc, "allocated_space_bytes", value);
		}

		/* Available */
		space_to_string(mcxv->mc_usage.mpu_fusable, value, sizeof(value));
		yaml_element_field(yc, "avail_space", value);
		if (verbosity > 0) {
			show_u64_dec(value, sizeof(value), &mcxv->mc_usage.mpu_fusable, 0);
			yaml_element_field(yc, "avail_space_bytes", value);
		}

		if (verbosity < 2) {
			yaml_end_element(yc);
			continue;
		}

		show_pct(value, sizeof(value), &mcxv->mc_spare, 0);
		yaml_element_field(yc, "spare", value);

		show_devtype(value, sizeof(value), &mcxv->mc_devtype, 0);
		yaml_element_field(yc, "dev_type", value);

		yaml_element_bool(yc, "mlog_target", mcxv->mc_features & MP_MC_FEAT_MLOG_TGT);

		allocu = mcxv->mc_zonepg << PAGE_SHIFT;

		space_to_string((u64)allocu, value, sizeof(value));
		yaml_element_field(yc, "mblock_size", value);

		show_u32_dec(value, sizeof(value), &allocu, 0);
		yaml_element_field(yc, "mblock_size_bytes", value);

		sectorsz = 1 << mcxv->mc_sectorsz;

		space_to_string((u64)sectorsz, value, sizeof(value));
		yaml_element_field(yc, "sector_size", value);

		show_u32_dec(value, sizeof(value), &sectorsz, 0);
		yaml_element_field(yc, "sector_size_bytes", value);

		show_u16_dec(value, sizeof(value), &mcxv->mc_uacnt, 0);
		yaml_element_field(yc, "unavail_dev", value);

		yaml_end_element(yc);
	}

	yaml_end_element_type(yc);
}

static void
mpool_list_yaml_usage(
	const struct mpool_usage   *usage,
	int                         verbosity,
	struct yaml_context        *yc)
{
	char    value[32];
	u64     space;

	if (verbosity > 0) {

		/* Total */
		space = usage->mpu_total;
		space_to_string(space, value, sizeof(value));
		yaml_element_field(yc, "total_space", value);

		show_u64_dec(value, sizeof(value), &space, 0);
		yaml_element_field(yc, "total_space_bytes", value);

		/* Usable */
		space = usage->mpu_usable;
		space_to_string(space, value, sizeof(value));
		yaml_element_field(yc, "usable_space", value);

		show_u64_dec(value, sizeof(value), &space, 0);
		yaml_element_field(yc, "usable_space_bytes", value);

		/* Used */
		space = usage->mpu_used;
		space_to_string(space, value, sizeof(value));
		yaml_element_field(yc, "allocated_space", value);

		show_u64_dec(value, sizeof(value), &space, 0);
		yaml_element_field(yc, "allocated_space_bytes", value);

		/* Available */
		space = usage->mpu_fusable;
		space_to_string(space, value, sizeof(value));
		yaml_element_field(yc, "avail_space", value);

		show_u64_dec(value, sizeof(value), &space, 0);
		yaml_element_field(yc, "avail_space_bytes", value);
	}

	if (verbosity > 1) {

		/* Spare */
		space = usage->mpu_spare;
		space_to_string(space, value, sizeof(value));
		yaml_element_field(yc, "spare_space", value);

		show_u64_dec(value, sizeof(value), &space, 0);
		yaml_element_field(yc, "spare_space_bytes", value);

		/* Free spare */
		space = usage->mpu_fspare;
		space_to_string(space, value, sizeof(value));
		yaml_element_field(yc, "avail_spare", value);

		show_u64_dec(value, sizeof(value), &space, 0);
		yaml_element_field(yc, "avail_spare_bytes", value);

		show_u32_dec(value, sizeof(value), &usage->mpu_mblock_cnt, 0);
		yaml_element_field(yc, "mblock_count", value);

		show_u64_dec(value, sizeof(value), &usage->mpu_mblock_alen, 0);
		yaml_element_field(yc, "mblock_alloc_bytes", value);

		show_u64_dec(value, sizeof(value), &usage->mpu_mblock_wlen, 0);
		yaml_element_field(yc, "mblock_written_bytes", value);

		show_u32_dec(value, sizeof(value), &usage->mpu_mlog_cnt, 0);
		yaml_element_field(yc, "mlog_count", value);

		show_u64_dec(value, sizeof(value), &usage->mpu_mlog_alen, 0);
		yaml_element_field(yc, "mlog_alloc_bytes", value);

		show_u64_dec(value, sizeof(value), &usage->mpu_alen, 0);
		yaml_element_field(yc, "object_alloc_bytes", value);

		show_u64_dec(value, sizeof(value), &usage->mpu_wlen, 0);
		yaml_element_field(yc, "object_written_bytes", value);
	}
}

static void
mpool_ls_list_yaml(
	struct mpioc_prop      *props,
	int                     verbosity,
	struct yaml_context    *yc)

{
	const struct mpool_xprops  *xprops;
	const struct mpool_params  *params;
	mpool_err_t                 err;

	char    uuidstr[MPOOL_UUID_SIZE * 3];
	char    errbuf[128];
	char    value[128];
	u64     space;
	int     i;

	xprops = &props->pr_xprops;
	params = &xprops->ppx_params;

	yaml_start_element(yc, "name", params->mp_name);
	yaml_element_bool(yc, "active", params->mp_stat != MP_UNDEF);

	uuid_unparse(params->mp_poolid, uuidstr);
	yaml_element_field(yc, "UUID", uuidstr);

	yaml_start_element_type(yc, "devices");

	for (i = 0; i < MP_MED_NUMBER; ++i) {
		char    devpath[PATH_MAX], fqdn[PATH_MAX];
		char   *fmt = "/dev/%s";
		merr_t  err;

		if (!xprops->ppx_pd_namev[i][0])
			continue;

		if (xprops->ppx_pd_namev[i][0] == '/')
			fmt = "%s";

		snprintf(fqdn, sizeof(fqdn), fmt, xprops->ppx_pd_namev[i]);

		err = mpool_devinfo(fqdn, devpath, sizeof(devpath));
		if (err) {
			mpool_strinfo(err, errbuf, sizeof(errbuf));
			yaml_field_fmt(yc, "error", "\"mpool_devinfo %s %s\"",
				       xprops->ppx_pd_namev[i], errbuf);
			yaml_end_element(yc);
			continue;
		}

		yaml_start_element(yc, "path", devpath);

		if (verbosity > 0) {
			if (xprops->ppx_pd_mclassv[i] < MP_MED_NUMBER) {
				enum mp_media_classp    mclassp;

				mclassp = xprops->ppx_pd_mclassv[i];
				show_media_classp(value, sizeof(value), &mclassp, 0);
				yaml_element_field(yc, "media_class", value);
			}
		}

		yaml_end_element(yc);
	}

	yaml_end_element_type(yc);

	if (params->mp_stat == MP_UNDEF || verbosity < 1) {
		yaml_end_element(yc);
		return;
	}

	err = show_uid(value, sizeof(value), &params->mp_uid, 0);
	if (err || co.co_noresolve)
		snprintf(value, sizeof(value), "%u", params->mp_uid);
	yaml_element_field(yc, "uid", value);

	err = show_gid(value, sizeof(value), &params->mp_gid, 0);
	if (err || co.co_noresolve)
		snprintf(value, sizeof(value), "%u", params->mp_gid);
	yaml_element_field(yc, "gid", value);

	yaml_field_fmt(yc, "mode", "0%02o", params->mp_mode);
	yaml_field_fmt(yc, "label", "%s", params->mp_label);

	mpool_list_yaml_usage(&props->pr_usage, verbosity, yc);

	space = params->mp_stat;
	show_status(value, sizeof(value), &space, 0);
	yaml_element_field(yc, "health", value);

	mpool_list_yaml_mclass(props->pr_mcxc, props->pr_mcxv, verbosity, yc);

	yaml_end_element(yc);
}

static void
mpool_ls_list_tab(
	struct mpioc_prop  *props,
	int                 verbosity,
	bool               *headers,
	bool                parsable,
	int                 mpwidth,
	int                 labwidth,
	char               *obuf,
	size_t              obufsz,
	size_t             *obufoff)
{
	static const char  suffixtab[] = "\0kmgtpezy";

	const char *labelstr = "-";
	const char *stp;

	const struct mpool_xprops  *xprops;
	const struct mpool_params  *params;
	const struct mpool_usage   *usage;

	double  total, used, usable, free;
	char    totalstr[32], usedstr[32];
	char    usablestr[32], freestr[32];
	char    capstr[32], statstr[32];
	double  capacity = 0;
	u32     status;
	char   *fmt;
	int     width;

	xprops = &props->pr_xprops;
	params = &xprops->ppx_params;
	usage = &props->pr_usage;

	width = parsable ? 16 : 7;

	if (*headers) {
		*headers = false;

		snprintf_append(obuf, obufsz, obufoff, "%-*s %*s %*s %*s %9s %*s %9s\n",
			mpwidth, "MPOOL", width, "TOTAL", width, "USED", width, "AVAIL",
			"CAPACITY", labwidth, "LABEL", "HEALTH");
	}

	stp = suffixtab;
	total = usage->mpu_total;
	while (!parsable && total >= 1024) {
		total /= 1024;
		++stp;
	}
	fmt = (total < 10) ? "%.2lf%c" : "%4.0lf%c";
	snprintf(totalstr, sizeof(totalstr), fmt, total, *stp);

	stp = suffixtab;
	usable = usage->mpu_usable;
	while (!parsable && usable >= 1024) {
		usable /= 1024;
		++stp;
	}
	fmt = (usable < 10) ? "%.2lf%c" : "%4.0lf%c";
	snprintf(usablestr, sizeof(usablestr), fmt, usable, *stp);

	stp = suffixtab;
	used = usage->mpu_used;
	while (!parsable && used >= 1024) {
		used /= 1024;
		++stp;
	}
	fmt = (used < 10) ? "%.2lf%c" : "%4.0lf%c";
	snprintf(usedstr, sizeof(usedstr), fmt, used, *stp);

	stp = suffixtab;
	free = usage->mpu_usable - usage->mpu_used;
	while (!parsable && free >= 1024) {
		free /= 1024;
		++stp;
	}
	fmt = (free < 10) ? "%.2lf%c" : "%4.0lf%c";
	snprintf(freestr, sizeof(freestr), fmt, free, *stp);

	if (usage->mpu_total > 0) {
		capacity = usage->mpu_used * 100;
		capacity /= usage->mpu_usable;
	}
	snprintf(capstr, sizeof(capstr), "%.2lf%c", capacity, parsable ? '\0' : '%');

	status = params->mp_stat;
	show_status(statstr, sizeof(statstr), &status, 0);

	if (params->mp_label[0])
		labelstr = params->mp_label;

	snprintf_append(obuf, obufsz, obufoff, "%-*s %*s %*s %*s %9s %*s %9s\n",
		mpwidth, params->mp_name, width, totalstr, width, usedstr, width, freestr,
		capstr, labwidth, labelstr, statstr);
}

uint64_t
mpool_ls_list(
	int                     argc,
	char                  **argv,
	uint32_t                flags,
	int                     verbosity,
	bool                    headers,
	bool                    parsable,
	bool                    yaml,
	char                   *obuf,
	size_t                  obufsz,
	struct mpool_devrpt    *ei)
{
	struct mpioc_prop  *propv, *props;
	struct mpioc_list   ls;
	struct imp_entry   *entryv;

	int     noffline, nappended, nmatched;
	bool    argmatchv[argc + 1];
	size_t  obufoff = 0;
	int     entryc;
	int     labwidth = 6;
	int     mpwidth = 6;
	int     fd, rc, i, j;
	merr_t  err;

	struct yaml_context yc = {
		.yaml_emit = yaml_print_and_rewind,
		.yaml_buf_sz = obufsz,
		.yaml_buf = obuf,
	};

	mpool_devrpt_init(ei);

	if (!obuf)
		return merr(EINVAL);

	entryv = NULL;
	entryc = 0;

	err = imp_entries_get(NULL, NULL, NULL, NULL, &entryv, &entryc);
	if (err)
		return err;

	propv = calloc(entryc + 1024, sizeof(*propv));
	if (!propv)
		return merr(ENOMEM);

	memset(&ls, 0, sizeof(ls));
	ls.ls_listv = propv;
	ls.ls_listc = entryc + 1024;
	ls.ls_cmd = MPIOC_LIST_CMD_PROP_LIST;

	fd = open(MPC_DEV_CTLPATH, O_RDONLY);
	if (-1 == fd) {
		err = merr(errno);
		mpool_devrpt(ei, MPOOL_RC_OPEN, -1, MPC_DEV_CTLPATH);
		free(propv);
		return err;
	}

	rc = ioctl(fd, MPIOC_PROP_GET, &ls);
	if (rc) {
		err = merr(errno);
		free(propv);
		close(fd);
		return err;
	}

	close(fd);

	for (i = 0; i < argc; ++i)
		argmatchv[i] = false;

	noffline = (ls.ls_listc < entryc) ? entryc : 0;
	nappended = 0;

	/* Append inactive/offline mpools to the properties list.
	 */
	for (i = 0; i < noffline; ++i) {
		struct mpool_xprops    *xprops;
		char                   *mpname = NULL;

		for (props = propv, j = 0; j < ls.ls_listc; ++j, ++props) {
			mpname = props->pr_xprops.ppx_params.mp_name;

			if (!strcmp(entryv[i].mp_name, mpname))
				break;
		}

		if (j < ls.ls_listc)
			continue;

		for (; j < entryc; ++j, ++props) {
			mpname = props->pr_xprops.ppx_params.mp_name;
			if (!mpname[0])
				break;

			if (!strcmp(entryv[i].mp_name, mpname))
				break;
		}

		if (j >= entryc)
			continue;

		xprops = &props->pr_xprops;

		if (!mpname[0]) {
			strlcpy(mpname, entryv[i].mp_name,
				sizeof(props->pr_xprops.ppx_params.mp_name));

			memcpy(xprops->ppx_params.mp_poolid, &entryv[i].mp_uuid, MPOOL_UUID_SIZE);

			for (j = 0; j < MP_MED_NUMBER; ++j)
				xprops->ppx_pd_mclassv[j] = MP_MED_INVALID;

			++nappended;
		}

		for (j = 0; j < MP_MED_NUMBER; ++j) {
			if (!xprops->ppx_pd_namev[j][0]) {
				strcpy(xprops->ppx_pd_namev[j], entryv[i].mp_path);
				break;
			}
		}
	}

	/* Find the max mpool name and label widths, and mark
	 * unwanted entries so that we can ignore them.
	 */
	ls.ls_listc += nappended;
	nmatched = 0;

	for (props = propv, i = 0; i < ls.ls_listc; ++i, ++props) {
		const char *mpname;
		bool        match = (argc < 1);
		size_t      len;

		mpname = props->pr_xprops.ppx_params.mp_name;

		for (j = 0; j < argc; ++j) {
			if (strcmp(argv[j], mpname) == 0) {
				argmatchv[j] = true;
				match = true;
				++nmatched;
			}
		}

		if (!match) {
			props->pr_rsvd1 = -1;
			continue;
		}

		len = strlen(mpname);
		if (len > mpwidth)
			mpwidth = len;

		len = strlen(props->pr_xprops.ppx_params.mp_label);
		if (len >= labwidth)
			labwidth = len + 1;
	}

	if (yaml)
		yaml_start_element_type(&yc, "mpools");

	for (props = propv, i = 0; i < ls.ls_listc; ++i, ++props) {
		if (props->pr_rsvd1)
			continue;

		if (yaml)
			mpool_ls_list_yaml(props, verbosity, &yc);
		else
			mpool_ls_list_tab(props, verbosity, &headers, parsable, mpwidth, labwidth,
					  obuf, obufsz, &obufoff);
	}

	if (yaml)
		yaml_end_element_type(&yc);

	for (i = 0; i < argc; ++i) {
		if (argmatchv[i])
			continue;

		fprintf(co.co_fp, "%s: mpool %s not found\n", progname, argv[i]);
	}

	free(propv);
	free(entryv);

	return (argc > 0 && nmatched < 1) ? merr(EINVAL) : 0;
}

void
mpool_list_help(
	struct verb_s  *v,
	bool            terse)
{
	struct help_s  h = {
		.token = "list",
		.shelp = "List all active and inactive mpools",
		.lhelp = "List properties of all or specified mpools",
		.usage = "[<mpname> ...]",

		.example = "%*s %s\n%*s %s -Y mp1 mp2 mp3\n",
	};

	mpool_generic_verb_help(v, &h, terse, NULL, 0);
}

merr_t
mpool_list_func(
	struct verb_s   *v,
	int             argc,
	char           **argv)
{
	struct mpool_devrpt   ei;

	char    errbuf[128];
	char   *buf;
	u32     flags;
	merr_t  err;

	flags = 0;
	flags_set_common(&flags);

	if (co.co_dry_run)
		return 0;

	buf = calloc(1, MPOOL_LIST_BUFSZ);
	if (!buf)
		return merr(ENOMEM);

	err = mpool_ls_list(argc, argv, flags, co.co_verbose, !co.co_noheadings,
			    co.co_nosuffix, co.co_yaml, buf, MPOOL_LIST_BUFSZ, &ei);

	if (err)
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf), "list mpools", "", &ei);
	else
		printf("%s", buf);

	free(buf);

	return err;
}
