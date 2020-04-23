// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#define _GNU_SOURCE

#include <util/platform.h>
#include <util/string.h>

#include <mpool/mpool.h>

#include <util/parse_num.h>
#include <util/param.h>

#include <mpctl/impool.h>
#include <mpctl/pd_props.h>
#include <mpool.h>
#include "mpool_ui.h"
#include <ui_common.h>

#include "common.h"

#include "../mpool/device_table.h"
#include "../mpool/discover.h"

#include "yaml.h"

#include <sysexits.h>
#include <assert.h>
#include <pwd.h>

static const char *fmt_insufficient =
	"%s: insufficient arguments for mandatory parameters, use -h for help\n";

static const char *fmt_extraneous =
	"%s: extraneous argument `%s' detected, use -h for help\n";

static char stgdev[128];

static void
mpool_params_defaults(struct mpool_params *params)
{
	const char *path = "/sys/module/mpool/parameters/mpc_default_";
	mpool_err_t err;
	u64 val;

	memset(params, 0, sizeof(*params));
	params->mp_uid = MPOOL_UID_INVALID;
	params->mp_gid = MPOOL_GID_INVALID;
	params->mp_mode = MPOOL_MODE_INVALID;

	err = sysfs_get_val_u64(path, "uid", false, &val);
	if (!err)
		params->mp_uid = val;

	err = sysfs_get_val_u64(path, "gid", false, &val);
	if (!err)
		params->mp_gid = val;

	err = sysfs_get_val_u64(path, "mode", false, &val);
	if (!err)
		params->mp_mode = val;

	params->mp_spare_cap =  MPOOL_SPARES_DEFAULT;
	params->mp_spare_stg =  MPOOL_SPARES_DEFAULT;
	params->mp_mclassp  =  MPOOL_MCLASS_DEFAULT;
	params->mp_ra_pages_max = MPOOL_RA_PAGES_MAX;
	params->mp_mdc0cap = 0;
	params->mp_mdcncap = 0;
	params->mp_mdcnum  = MPOOL_MDCNUM_DEFAULT;
	params->mp_mblocksz[MP_MED_STAGING] = 32;
	params->mp_mblocksz[MP_MED_CAPACITY] = 32;
	strcpy(params->mp_label, MPOOL_LABEL_DEFAULT);
}

static merr_t
mpool_prepare(
	char   **devices,
	int      dcnt)
{
	struct mpool_devrpt devrpt;

	char    errbuf[NFUI_ERRBUFSZ];
	int     i, j;
	merr_t  err = 0;

	mpool_devrpt_init(&devrpt);

	if (!devices)
		return merr(EINVAL);

	for (i = 0; i < dcnt ; i++) {
		err = device_is_full_device(devices[i]);
		if (err && !co.co_dry_run) {
			fprintf(co.co_fp, "%s is not a full device name\n",
				devices[i]);
			return err;
		}
		for (j = i + 1; j < dcnt; j++) {
			if (!strcmp(devices[j], devices[i])) {
				fprintf(co.co_fp,
				"Device %s is repeated in argument list.\n",
				devices[i]);
				return merr(EINVAL);
			}
		}

		if (!co.co_dry_run) {
			err = mp_sb_magic_check(devices[i], &devrpt);
			if ((merr_errno(err) == EBUSY) && co.co_force) {
				bool activated;
				char mp_name[MPOOL_NAME_LEN_MAX + 1];

				/*
				 * The device belongs to an mpool.
				 * Check if that mpool is activated.
				 */
				err = mp_dev_activated(devices[i], &activated,
					mp_name, sizeof(mp_name));
				if (err)
					goto exit;

				if (activated) {
					mpool_devrpt(
						&devrpt, MPCTL_RC_DEVACTIVATED,
						i, NULL);
					err = merr(EBUSY);
					goto exit;
				}

				fprintf(co.co_fp,
					"WARNING: mpool %s might now be unusable\n",
					mp_name);

			} else if (err) {
				goto exit;
			}
		}
	}

	if (!co.co_dry_run) {
		if (co.co_discard) {
			err = mp_trim_device(dcnt, devices, &devrpt);
			if (err)
				goto exit;
		}

		/*
		 * Normally at this point the super blocks are gone.
		 * It is because this drive was not used by a mpool before,
		 * or it was part of a destroyed mpool or because of the
		 * drive formatting done above erased them.
		 * However there is a corner case for which at that point
		 * the super blocks may still be there:
		 * Before the prepare, the mpool was not destroyed but instead
		 * the partition was removed (for example a system clobber),
		 * and so the formatting done above did not erase them.
		 */
		if (co.co_force) {
			size_t  pool_lst_len = dcnt * (1 + MPOOL_NAME_LEN_MAX);
			char   *pool_lst;

			pool_lst = calloc(1, pool_lst_len);
			if (!pool_lst) {
				err = merr(ENOMEM);
				goto exit;
			}

			/* Erase the superblocks.
			 */
			err = mp_sb_erase(dcnt, devices, &devrpt, pool_lst,
					  pool_lst_len);
			if (err) {
				free(pool_lst);
				goto exit;
			}
			free(pool_lst);

		} else {
			/*
			 * Require the force option if the super blocks
			 * are still there.
			 */
			for (i = 0; i < dcnt; i++) {
				err = mp_sb_magic_check(devices[i], &devrpt);
				if (err)
					break;
			}
		}
	}

exit:
	if (err) {
		struct mpool_devrpt ei = devrpt;
		const char         *device = NULL;

		if (ei.mdr_off != -1 && ei.mdr_rcode != MPOOL_RC_ERRMSG) {
			device = devices[ei.mdr_off];
			strlcpy(ei.mdr_msg, device, sizeof(ei.mdr_msg));
		}

		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 "prepare device", device, &ei);
	}

	return err;
}

#define PARAM_INST_MBSZ(_val, _name, _msg)			\
	{ { _name"=%s", sizeof(u32), 1, 64 + 1,	\
	   get_u32, show_u32, check_u32 },			\
	   (void *)&(_val), (_msg), PARAM_FLAG_TUNABLE }

/**
 * mpool create <mpool> <device>
 */

static struct mpool_params params;

static struct param_inst
create_paramsv[] = {
	PARAM_INST_UID(params.mp_uid, "uid", "spec file user ID"),
	PARAM_INST_GID(params.mp_gid, "gid", "spec file group ID"),
	PARAM_INST_MODE(params.mp_mode, "mode", "spec file mode bits"),
	PARAM_INST_STRING(params.mp_label, sizeof(params.mp_label),
			  "label", "limited ascii text"),
	PARAM_INST_MBSZ(params.mp_mblocksz[MP_MED_CAPACITY],
			"capsz", "capacity device mblock size"),
	PARAM_INST_MBSZ(params.mp_mblocksz[MP_MED_STAGING],
			"stgsz", "staging device mblock size"),
	PARAM_INST_U16_ADV(params.mp_mdc0cap, "mdc0cap",
			   "MDC0 capacity in MiB"),
	PARAM_INST_U16_ADV(params.mp_mdcncap, "mdcncap",
			   "MDCN capacity in MiB"),
	PARAM_INST_U16_ADV(params.mp_mdcnum, "mdcnum",
			   "Number of mpool internal MDCs"),
	PARAM_INST_STRING(stgdev, sizeof(stgdev),
			  "stgdev", "staging device"),
	PARAM_INST_END
};

void
mpool_create_help(
	struct verb_s  *v,
	bool            terse)
{
	struct help_s  h = {
		.token = "create",
		.shelp = "Create and activate a new mpool",
		.lhelp = "Create and activate <mpname> on <device>",
		.usage = "<mpname> <device>",

		.example =
		"%*s %s mp1 /dev/nvme0n1\n"
		"%*s %s mp1 /dev/vg1/lvbig capsz=16 stgdev=/dev/vg2/lvfast\n"
	};

	mpool_params_defaults(&params);
	mpool_generic_verb_help(v, &h, terse, create_paramsv, 0);
}

merr_t
mpool_create_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	struct mpool_devrpt     ei = { };
	struct pd_prop          props;
	const char             *mpname;

	char    errbuf[NFUI_ERRBUFSZ];
	int     argind = 0;
	u32     flags = 0;
	merr_t  err;
	size_t  sz;

	mpool_params_init(&params);
	flags_set_common(&flags);
	mpool_devrpt_init(&ei);

	err = process_params(argc, argv, create_paramsv, &argind, 0);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		fprintf(co.co_fp, "%s: unable to convert `%s': %s\n",
			progname, argv[argind], errbuf);
		return err;
	}

	argc -= argind;
	argv += argind;

	if (argc < 2) {
		fprintf(co.co_fp, fmt_insufficient, progname);
		return merr(EINVAL);
	} else if (argc > 2) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[2]);
		return merr(EINVAL);
	}

	if (params.mp_mblocksz[MP_MED_STAGING] && !stgdev[0]) {
		fprintf(co.co_fp, "%s: `stgsz' specified without `stgdev', did you mean 'capsz'?\n",
			progname);
		return merr(EINVAL);
	}

	mpname = argv[0];

	err = imp_dev_get_prop(argv[1], &props);
	if (err) {
		enum mpool_rc rcode = MPCTL_RC_INVALDEV;

		if (mpool_errno(err) == EACCES)
			rcode = MPOOL_RC_OPEN;

		mpool_devrpt(&ei, rcode, -1, argv[1]);
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 "create mpool", mpname, &ei);
		return err;
	}

	sz = params.mp_mblocksz[MP_MED_CAPACITY];
	if (sz > 0 && (!powerof2(sz) ||
		       sz < MPOOL_MBSIZE_MB_MIN ||
		       sz > MPOOL_MBSIZE_MB_MAX)) {
		fprintf(co.co_fp, "%s: capsz must be power-of-2 in [1..64]\n",
			progname);
		return merr(EINVAL);
	}

	sz = params.mp_mblocksz[MP_MED_STAGING];
	if (sz > 0 && (!powerof2(sz) ||
		       sz < MPOOL_MBSIZE_MB_MIN ||
		       sz > MPOOL_MBSIZE_MB_MAX)) {
		fprintf(co.co_fp, "%s: stgsz must be power-of-2 in [1..64]\n",
			progname);
		return merr(EINVAL);
	}

	if (!co.co_dry_run) {
		u16    mdc0cap;
		u16    mdcncap;
		u16    mdcnum;

		err = mpool_prepare(argv + 1, 1);
		if (err)
			goto errout;

		mdc0cap = params.mp_mdc0cap;
		mdcncap = params.mp_mdcncap;
		if ((mdc0cap != 0 && !powerof2(mdc0cap)) ||
		    (mdcncap != 0 && !powerof2(mdcncap))) {
			err = merr(EINVAL);
			mpool_devrpt(&ei, MPOOL_RC_ERRMSG, -1,
				     "mdc0cap/mdcncap must be power-of-2");
			emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
				 "create mpool", mpname, &ei);
			goto errout;
		}

		if (mdc0cap > MPOOL_MDC0CAP_MB_MAX) {
			params.mp_mdc0cap = MPOOL_MDC0CAP_MB_MAX;
			if (co.co_verbose)
				fprintf(co.co_fp, "mdc0cap capped to max %u\n",
					MPOOL_MDC0CAP_MB_MAX);
		}

		if (mdcncap > MPOOL_MDCNCAP_MB_MAX) {
			params.mp_mdcncap = MPOOL_MDCNCAP_MB_MAX;
			if (co.co_verbose)
				fprintf(co.co_fp, "mdcncap capped to max %u\n",
					MPOOL_MDCNCAP_MB_MAX);
		}

		mdcnum = params.mp_mdcnum;
		if (mdcnum > MPOOL_MDCNUM_MAX) {
			params.mp_mdcnum = MPOOL_MDCNUM_MAX;
			if (co.co_verbose)
				fprintf(co.co_fp, "mdcnum capped to max %u\n",
					MPOOL_MDCNUM_MAX);
		}

		err = mpool_create(mpname, argv[1], &params, flags, &ei);
		if (err) {
			emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
				 "create mpool", mpname, &ei);
			goto errout;
		}

		if (stgdev[0]) {
			err = mpool_mclass_add(mpname, stgdev,
					       MP_MED_STAGING,
					       &params, flags, &ei);
			if (err) {
				emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
					 "create mpool", mpname, &ei);
				flags |= (1u << MP_FLAGS_FORCE);
				mpool_destroy(mpname, flags, &ei);
				goto errout;
			}
		}

		if (co.co_verbose)
			fprintf(co.co_fp, "mpool %s created\n", mpname);
	}

errout:

	return err;
}

/**
 * mpool add <mpool> <device>
 */

static struct mpool_params aparams;

static struct param_inst
add_paramsv[] = {
	PARAM_INST_STRING(stgdev, sizeof(stgdev),
			  "stgdev", "staging device"),
	PARAM_INST_MBSZ(aparams.mp_mblocksz[MP_MED_STAGING],
			"stgsz", "staging device mblock size"),
	PARAM_INST_END
};

void
mpool_add_help(
	struct verb_s  *v,
	bool            terse)
{
	struct help_s  h = {
		.token = "add",
		.shelp = "Add a staging device to an existing activated mpool",
		.lhelp = "Add <device> to activated mpool <mpname>",
		.usage = "<mpname> stgdev=<device>",

		.example =
		"%*s %s mp1 stgdev=/dev/vg1/lv1 stgsz=8\n"
		"%*s %s mp1 stgdev=/dev/nvme0n1\n",

	};

	mpool_params_defaults(&aparams);
	mpool_generic_verb_help(v, &h, terse, add_paramsv, 0);
}

merr_t
mpool_add_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	struct mpool_devrpt     ei = { };
	struct pd_prop          props;
	const char             *mpname;

	char    errbuf[NFUI_ERRBUFSZ];
	int     argind = 0;
	u32     flags = 0;
	merr_t  err;
	size_t  sz;

	mpool_params_init(&aparams);
	flags_set_common(&flags);
	mpool_devrpt_init(&ei);

	err = process_params(argc, argv, add_paramsv, &argind, 0);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		fprintf(co.co_fp, "%s: unable to convert `%s': %s\n",
			progname, argv[argind], errbuf);
		return err;
	}

	argc -= argind;
	argv += argind;

	if (argc < 1) {
		fprintf(co.co_fp, fmt_insufficient, progname);
		return merr(EINVAL);
	} else if (argc > 1) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[1]);
		return merr(EINVAL);
	}

	sz = params.mp_mblocksz[MP_MED_STAGING];
	if (sz > 0 && (!powerof2(sz) ||
		       sz < MPOOL_MBSIZE_MB_MIN ||
		       sz > MPOOL_MBSIZE_MB_MAX)) {
		fprintf(co.co_fp, "%s: stgsz must be power-of-2 in [1..64]\n",
			progname);
		return merr(EINVAL);
	}

	mpname = argv[0];

	err = imp_dev_get_prop(stgdev, &props);
	if (err) {
		enum mpool_rc rcode = MPCTL_RC_INVALDEV;

		if (mpool_errno(err) == EACCES)
			rcode = MPOOL_RC_OPEN;

		mpool_devrpt(&ei, rcode, -1, stgdev);
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 "add device to mpool", mpname, &ei);
		return err;
	}

	if (!co.co_dry_run) {
		char *v[] = { stgdev, NULL };

		err = mpool_prepare(v, 1);
		if (err)
			return err;

		err = mpool_mclass_add(mpname, stgdev,
				       MP_MED_STAGING,
				       &aparams, flags, &ei);
		if (err)
			emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
				 "add device to mpool", mpname, &ei);

		if (!err && co.co_verbose)
			fprintf(co.co_fp, "added %s to mpool %s\n",
				stgdev, mpname);
	}

	return err;
}

/**
 * mpool destroy <mpool>
 */

void
mpool_destroy_help(
	struct verb_s  *v,
	bool            terse)
{
	struct help_s  h = {
		.token = "destroy",
		.shelp = "Deactivate and destroy an existing mpool",
		.lhelp = "Deactivate and destroy an mpool by "
			 "<mpname> or <UUID>",
		.usage = "{<mpname> | <UUID>}",

		.example =
		"%*s %s mp1\n"
		"%*s %s c02c1dd6-f4a2-4d41-a4ef-3459cad90dbe\n",
	};

	mpool_generic_verb_help(v, &h, terse, NULL, 0);
}

merr_t
mpool_destroy_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	struct mpool_devrpt ei = { };
	const char         *mpname;

	char    errbuf[NFUI_ERRBUFSZ];
	merr_t  err = 0;
	u32     flags = 0;

	flags_set_common(&flags);

	if (argc < 1) {
		fprintf(co.co_fp, fmt_insufficient, progname);
		return merr(EINVAL);
	} else if (argc > 1) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[1]);
		return merr(EINVAL);
	}

	mpname = argv[0];

	if (co.co_dry_run)
		return 0;

	err = mpool_destroy(mpname, flags, &ei);
	emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
		 "destroy mpool", mpname, &ei);

	if (!err && co.co_verbose)
		fprintf(co.co_fp, "mpool %s destroyed\n", mpname);

	return err;
}

void
mpool_scan_help(
	struct verb_s  *v,
	bool            terse)
{
	struct help_s  h = {
		.token = "scan",
		.shelp = "Scan mpools on the system",
		.lhelp = "Scan and activate/deactivate all the mpools",
		.usage = "",

		.example =
		"%*s %s\n"
		"%*s %s --activate\n"
		"%*s %s --deactivate\n",
	};

	mpool_generic_verb_help(v, &h, terse, NULL, 0);
}

merr_t
mpool_scan_func(
	struct verb_s   *v,
	int             argc,
	char           **argv)
{
	struct mpool_devrpt ei = { };
	struct mp_props    *allv, *actv;

	char        uuidstr[MPOOL_UUID_SIZE * 3];
	char        errbuf[128];
	int         allc, actc, i, j;
	uint32_t    flags, nactive;
	char       *buf;
	merr_t      err;

	if (argc > 0) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[0]);
		return merr(EINVAL);
	}

	if (co.co_activate && co.co_deactivate)
		abort(); /* malformed xoption excludes? */

	flags = 0;
	flags_set_common(&flags);

	if (!(co.co_activate || co.co_deactivate)) {
		buf = calloc(1, MPOOL_LIST_BUFSZ);
		if (!buf)
			return merr(ENOMEM);

		err = mpool_ls_list(argc, argv, flags,
				    co.co_verbose, !co.co_noheadings,
				    co.co_nosuffix, co.co_yaml,
				    buf, MPOOL_LIST_BUFSZ, &ei);

		if (err)
			emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
				 "scan mpools", "", &ei);
		else
			printf("%s", buf);

		free(buf);

		return err;
	}

	err = mpool_scan(&allc, &allv, &ei);
	if (err) {
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 co.co_activate ? "activate mpools" : "deactivate mpools",
			 "", &ei);
		return err;
	}

	err = mpool_list(&actc, &actv, &ei);
	if (err) {
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 co.co_activate ? "activate mpools" : "deactivate mpools",
			 "", &ei);
		free(allv);
		return err;
	}

	nactive = actc;

	for (i = 0; i < allc; ++i) {
		bool match = false;

		for (j = 0; j < actc; ++j) {
			match = !strcmp(allv[i].mp_name, actv[j].mp_name);
			if (match)
				break;
		}

		if (match && co.co_activate)
			continue;

		if (!match && co.co_deactivate)
			continue;

		if (co.co_verbose > 0) {
			uuid_unparse(*(uuid_t *)&allv[i].mp_poolid, uuidstr);

			printf("%sctivating mpool %s  %s\n",
				co.co_activate ? "A" : "Dea",
				allv[i].mp_name, uuidstr);
		}

		if (co.co_dry_run)
			continue;

		if (co.co_activate)
			err = mpool_activate(allv[i].mp_name, NULL, 0, NULL);
		else
			err = mpool_deactivate(allv[i].mp_name, 0, NULL);

		if (err) {
			printf("Unable to %sactivate mpool %s: %s\n",
			       co.co_activate ? "" : "de",
			       allv[i].mp_name,
			       mpool_strinfo(err, errbuf, sizeof(errbuf)));
			continue;
		}

		nactive += co.co_activate ? 1 : -1;
	}

	printf("%u mpools now active\n", nactive);

	free(actv);
	free(allv);

	return 0;
}

/**
 * mpool activate <mpool>
 */

static struct mpool_params act_params;

static struct param_inst
activate_paramsv[] = {
	PARAM_INST_UID(act_params.mp_uid, "uid", "spec file user ID"),
	PARAM_INST_GID(act_params.mp_gid, "gid", "spec file group ID"),
	PARAM_INST_MODE(act_params.mp_mode, "mode", "spec file mode bits"),
	PARAM_INST_U16_ADV(act_params.mp_mdcnum, "mdcnum",
			   "Number of mpool internal MDCs"),
	PARAM_INST_STRING(act_params.mp_label, sizeof(act_params.mp_label),
			  "label", "limited ascii text"),
	PARAM_INST_END
};

void
mpool_activate_help(
	struct verb_s  *v,
	bool            terse)
{
	struct help_s  h = {
		.token = "activate",
		.shelp = "Activate an inactive mpool",
		.lhelp = "Activate an mpool by <mpname> or <UUID>",
		.usage = "{<mpname> | <UUID>}",

		.example =
		"%*s %s mp1\n"
		"%*s %s c02c1dd6-f4a2-4d41-a4ef-3459cad90dbe\n",
	};

	mpool_params_defaults(&act_params);
	mpool_generic_verb_help(v, &h, terse, activate_paramsv, 0);
}

merr_t
mpool_activate_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	struct mpool_devrpt ei = { };
	const char         *mpname;

	char    errbuf[NFUI_ERRBUFSZ];
	merr_t  err = 0;
	u32     flags = 0;
	int     argind = 0;
	u16     mdcnum;

	mpool_params_init(&act_params);
	flags_set_common(&flags);

	err = process_params(argc, argv, activate_paramsv, &argind, 0);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		fprintf(co.co_fp, "%s: unable to convert `%s': %s\n",
			progname, argv[argind], errbuf);
		return err;
	}

	argc -= argind;
	argv += argind;

	if (argc < 1) {
		fprintf(co.co_fp, fmt_insufficient, progname);
		return merr(EINVAL);
	} else if (argc > 1) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[1]);
		return merr(EINVAL);
	}

	mpname = argv[0];

	if (co.co_dry_run)
		return err;

	mdcnum = act_params.mp_mdcnum;
	if (mdcnum > MPOOL_MDCNUM_MAX) {
		act_params.mp_mdcnum = MPOOL_MDCNUM_MAX;
		if (co.co_verbose)
			fprintf(co.co_fp, "mdcnum capped to max %u\n",
				MPOOL_MDCNUM_MAX);
	}

	err = mpool_activate(mpname, &act_params, flags, &ei);
	if (err) {
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 "activate mpool", mpname, &ei);
		return err;
	}

	if (co.co_verbose)
		fprintf(co.co_fp, "mpool %s now active\n", mpname);

	return 0;
}

#define PARAM_INST_RA(_val, _name, _msg)			\
	{ { _name"=%s", sizeof(u32), 0, MPOOL_RA_PAGES_MAX + 1,	\
	   get_u32, show_u32, check_u32 },			\
	   (void *)&(_val), (_msg), PARAM_FLAG_TUNABLE }

/**
 * mpool set [mpool] [--verbose]
 */
static struct mpool_params sparams;

static
struct param_inst set_paramsv[] = {
	PARAM_INST_UID(sparams.mp_uid, "uid", "spec file user ID"),
	PARAM_INST_GID(sparams.mp_gid, "gid", "spec file group ID"),
	PARAM_INST_MODE(sparams.mp_mode, "mode", "spec file mode bits"),
	PARAM_INST_STRING(sparams.mp_label, sizeof(sparams.mp_label),
			  "label", "limited ascii text"),
	PARAM_INST_RA(sparams.mp_ra_pages_max,
		      "ra", "Max readahead pages"),
	PARAM_INST_PCT(sparams.mp_spare_cap, "spare_pct_capacity",
		       "Spare percent for CAPACITY media class"),
	PARAM_INST_PCT(sparams.mp_spare_stg, "spare_pct_staging",
		       "Spare percent for STAGING media class"),
	PARAM_INST_END
};

void
mpool_set_help(
	struct verb_s  *v,
	bool            terse)
{
	struct help_s  h = {
		.token   = "set",
		.shelp   = "Set mpool config parameters",
		.lhelp   = "Set mpool config parameters by <mpname>",
		.usage   = "<mpname>",

		.example =
		"%*s %s mp1 uid=root\n"
		"%*s %s mp1 spare_pct_capacity=10\n",
	};

	mpool_params_defaults(&sparams);
	mpool_generic_verb_help(v, &h, terse, set_paramsv, 0);
}

merr_t
mpool_set_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	struct mp_errinfo   ei = { };
	struct mpool       *ds;
	const char         *mpname;

	char    errbuf[NFUI_ERRBUFSZ];
	int     argind = 0;
	merr_t  err;

	mpool_params_init(&sparams);

	err = process_params(argc, argv, set_paramsv, &argind, 0);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		fprintf(co.co_fp, "%s: unable to convert `%s': %s\n",
			progname, argv[argind], errbuf);
		return err;
	}

	argc -= argind;
	argv += argind;

	if (argc < 1) {
		fprintf(co.co_fp, fmt_insufficient, progname);
		return merr(EINVAL);
	} else if (argc > 1) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[1]);
		return merr(EINVAL);
	}

	mpname = argv[0];

	if (co.co_dry_run)
		return err;

	err = mpool_open(mpname, 0, &ds, &ei);
	if (err) {
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 "set parameter for mpool", mpname, &ei);
		return err;
	}

	err = mpool_params_set(ds, &sparams, &ei);
	if (err)
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 "set parameter for mpool", mpname, &ei);
	else if (co.co_verbose)
		fprintf(co.co_fp, "parameters set for mpool %s\n", mpname);

	mpool_close(ds);

	return err;
}

void
mpool_get_help(
	struct verb_s  *v,
	bool            terse)
{
	struct help_s  h = {
		.token = "get",
		.shelp = "Get mpool config parameters",
		.lhelp = "Get config parameters of all or specified mpools",
		.usage = "[<mpname> ...]",

		.example = "%*s %s -v mp1 mp2 mp3\n",
	};

	mpool_generic_verb_help(v, &h, terse, NULL, 0);
}

merr_t
mpool_get_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	struct mpool_params    *paramsv, *params;
	struct mp_errinfo       ei;

	char    uidstr[32], gidstr[32];
	char    errbuf[128];
	int     nmatched = 0;
	int     uidwidth = 6;
	int     gidwidth = 6;
	int     labwidth = 6;
	int     mpwidth = 6;
	bool    headers;
	int     paramsc;
	merr_t  err;
	int     i, j;

	headers = !co.co_noheadings;

	err = mpool_list(&paramsc, &paramsv, &ei);
	if (err) {
		emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
			 "get mpool config params", "", &ei);
		return merr(EINVAL);
	}

	for (params = paramsv, i = 0; i < paramsc; ++i, ++params) {
		bool    match = (argc < 1);
		size_t  len;

		params = paramsv + i;

		for (j = 0; j < argc; ++j) {
			if (strcmp(argv[j], params->mp_name) == 0) {
				match = true;
				++nmatched;
			}
		}

		if (!match) {
			params->mp_name[0] = '\000';
			continue;
		}

		len = strlen(params->mp_name);
		if (len > mpwidth)
			mpwidth = len;

		len = strlen(params->mp_label);
		if (len >= labwidth)
			labwidth = len + 1;

		err = show_uid(uidstr, sizeof(uidstr), &params->mp_uid, 0);
		if (err || co.co_noresolve)
			snprintf(uidstr, sizeof(uidstr), "%u", params->mp_uid);
		len = strlen(uidstr);
		if (len >= uidwidth)
			uidwidth = len + 1;

		err = show_gid(gidstr, sizeof(gidstr), &params->mp_gid, 0);
		if (err || co.co_noresolve)
			snprintf(gidstr, sizeof(gidstr), "%u", params->mp_gid);
		len = strlen(gidstr);
		if (len >= gidwidth)
			gidwidth = len + 1;

		++nmatched;
	}

	for (params = paramsv, i = 0; i < paramsc; ++i, ++params) {
		if (!params->mp_name[0])
			continue;

		if (headers) {
			printf("%-*s %*s %*s %*s  %4s %4s %6s %5s %5s %5s",
			       mpwidth, "MPOOL",
			       labwidth, "LABEL",
			       uidwidth, "UID",
			       gidwidth, "GID",
			       "MODE", "RA",
			       "STGSZ", "CAPSZ",
			       "STGSP", "CAPSP");


			if (co.co_mutest > 0)
				printf(" %6s", "RMDCSZ");

			if (co.co_verbose > 0)
				printf(" %4s %s", "VMA", "TYPE");

			printf("\n");
			headers = false;
		}

		err = show_uid(uidstr, sizeof(uidstr), &params->mp_uid, 0);
		if (err || co.co_noresolve)
			snprintf(uidstr, sizeof(uidstr), "%u", params->mp_uid);

		err = show_gid(gidstr, sizeof(gidstr), &params->mp_gid, 0);
		if (err || co.co_noresolve)
			snprintf(gidstr, sizeof(gidstr), "%u", params->mp_gid);

		printf("%-*s %*s %*s %*s  %04o %4u %6u %5u %5u %5u",
		       mpwidth, params->mp_name,
		       labwidth, params->mp_label,
		       uidwidth, uidstr,
		       gidwidth, gidstr,
		       params->mp_mode,
		       params->mp_ra_pages_max,
		       params->mp_mblocksz[MP_MED_STAGING],
		       params->mp_mblocksz[MP_MED_CAPACITY],
		       params->mp_spare_stg, params->mp_spare_cap);

		if (co.co_mutest > 0)
			printf(" %6lu", params->mp_mdc_captgt >> 20);

		if (co.co_verbose > 0) {
			char uuidstr[64];

			uuid_unparse(*(uuid_t *)&params->mp_utype, uuidstr);
			printf(" %4u %s", params->mp_vma_size_max, uuidstr);
		}

		printf("\n");
	}

	free(paramsv);

	return (argc > 0 && nmatched < 1) ? merr(EINVAL) : 0;
}

/**
 * mpool deactivate <mpool>
 */
void
mpool_deactivate_help(
	struct verb_s   *v,
	bool             terse)
{
	struct help_s  h = {
		.token = "deactivate",
		.shelp = "Deactivate an active mpool",
		.lhelp = "Deactivate an mpool by <mpname> or <UUID>",
		.usage = "{<mpname> | <UUID>}",

		.example =
		"%*s %s mp1\n"
		"%*s %s c02c1dd6-f4a2-4d41-a4ef-3459cad90dbe\n",
	};

	mpool_generic_verb_help(v, &h, terse, NULL, 0);
}

merr_t
mpool_deactivate_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	struct mpool_devrpt ei = { };
	const char         *mpname;

	char    errbuf[NFUI_ERRBUFSZ];
	merr_t  err = 0;
	u32     flags = 0;

	flags_set_common(&flags);

	if (argc < 1) {
		fprintf(co.co_fp, fmt_insufficient, progname);
		return merr(EINVAL);
	} else if (argc > 1) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[1]);
		return merr(EINVAL);
	}

	mpname = argv[0];

	if (co.co_dry_run)
		return err;

	err = mpool_deactivate(mpname, flags, &ei);
	emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
		 "deactivate mpool", mpname, &ei);

	if (!err && co.co_verbose)
		fprintf(co.co_fp, "mpool %s now inactive\n", mpname);

	return err;
}


/**
 * mpool rename
 */
void
mpool_rename_help(
	struct verb_s   *v,
	bool             terse)
{
	struct help_s  h = {
		.token = "rename",
		.shelp = "Rename an inactive mpool",
		.lhelp = "Rename an inactive mpool from "
			"<oldmpname> or <oldUUID> to <newmpname>",
		.usage = "{<oldmpname> | <oldUUID>} <newmpname>",

		.example = "%*s %s mpold mpnew\n",
	};

	mpool_generic_verb_help(v, &h, terse, NULL, 0);
}

merr_t
mpool_rename_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	struct mpool_devrpt ei = { };
	const char         *oldmp, *newmp;

	char        errbuf[NFUI_ERRBUFSZ];
	merr_t      err = 0;
	uint32_t    flags = 0;

	flags_set_common(&flags);

	if (argc < 2) {
		fprintf(co.co_fp, fmt_insufficient, progname);
		return merr(EINVAL);
	} else if (argc > 2) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[2]);
		return merr(EINVAL);
	}

	oldmp = argv[0];
	newmp = argv[1];

	if (co.co_dry_run)
		return err;

	err = mpool_rename(oldmp, newmp, flags, &ei);
	emit_err(co.co_fp, err, errbuf, sizeof(errbuf),
		 "rename mpool", oldmp, &ei);

	if (!err && co.co_verbose)
		fprintf(co.co_fp, "Renamed mpool name from \"%s\" to \"%s\"\n",
			oldmp, newmp);

	return err;
}

/**
 * mpool version
 */
void
mpool_version_help(
	struct verb_s   *v,
	bool             terse)
{
	struct help_s  h = {
		.token   = "version",
		.shelp   = "Show mpool version",
		.lhelp   = "Show mpool version",
		.usage   = "",
	};

	mpool_generic_verb_help(v, &h, terse, NULL, 0);
}

merr_t
mpool_version_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	if (argc > 1) {
		fprintf(co.co_fp, fmt_extraneous, progname, argv[1]);
		return merr(EINVAL);
	}

	fprintf(co.co_fp, "version: %s\n", mpool_version);

	return 0;
}

/**
 * mpool test
 */
struct test {
	int     val64flg;
	int     uidflg;
	int     strflg;
	int64_t val64;
	uid_t   uid;
	char    str[32];
};

static struct test test;

const struct xoption
mpool_test_xoptionv[] = {
	{ 'h', "help",       NULL, "Show help",
	  &co.co_help, },
	{ 'i', "int64",       "u", "Specify an int64_t",
	  &test.val64flg, false, &test.val64, sizeof(test.val64), get_s64 },
	{ 'u', "uid",         "i", "Specify a uid",
	  &test.uidflg, false, &test.uid, sizeof(test.uid), get_uid },
	{ 's', NULL,         NULL, "Specify a string",
	  &test.strflg, false, &test.str, sizeof(test.str), get_string },
	{ 'T', "mutest",     NULL, "Enable mutest mode",
	  &co.co_mutest, },
	{ 'v', "verbose",    NULL, "Increase verbosity",
	  &co.co_verbose, },
	{ -1 }
};

void
mpool_test_help(
	struct verb_s   *v,
	bool             terse)
{
	struct help_s  h = {
		.token   = "test",
		.shelp   = "Test option parser",
		.lhelp   = "Test option parser",
		.usage   = "",
	};

	mpool_generic_verb_help(v, &h, terse, NULL, 0);
}

merr_t
mpool_test_func(
	struct verb_s   *v,
	int              argc,
	char           **argv)
{
	printf("%d %d %d %ld %d %u %d %s\n",
	       argc, co.co_verbose,
	       test.val64flg, test.val64,
	       test.uidflg, test.uid,
	       test.strflg, test.str ?: "");

	return 0;
}

void
mpool_help(
	bool terse)
{
	struct help_s  h = {
		.token = "mpool",
		.shelp = "Create and manage storage media device pools",
	};

	mpool_generic_sub_help(&h, terse);
}

void
mpool_usage(void)
{
	fprintf(co.co_fp, "usage: %s <command> [options] [args]\n", progname);
}

static struct verb_s
mpool_verb[] = {
	{ "activate",   "hrTv",     mpool_activate_func, mpool_activate_help, },
	{ "add",        "DfhTv",    mpool_add_func,      mpool_add_help, },
	{ "create",     "DfhTv",    mpool_create_func,   mpool_create_help, },
	{ "deactivate", "hTv",    mpool_deactivate_func, mpool_deactivate_help,},
	{ "destroy",    "fhTv",     mpool_destroy_func,  mpool_destroy_help, },
	{ "get",        "HhNTv",    mpool_get_func,      mpool_get_help, },
	{ "list",       "HhNpTvY",  mpool_list_func,     mpool_list_help, },
	{ "rename",     "fhTv",     mpool_rename_func,   mpool_rename_help,},
	{ "scan",       "adHhNTvY", mpool_scan_func,     mpool_scan_help, },
	{ "set",        "hTv",      mpool_set_func,      mpool_set_help, },
	{ "version",    "hTv",      mpool_version_func,  mpool_version_help, },
	{ "test",       "adhiusTv", mpool_test_func,     mpool_test_help,
	  .xoption = mpool_test_xoptionv, .hidden = true, },
	{ NULL },
};

struct subject_s mpool_ui = {
	.name  = "mpool",
	.verb  = mpool_verb,
	.help  = mpool_help,
	.usage = mpool_usage,
};
