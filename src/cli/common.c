// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <util/string.h>

#include <mpool/mpool.h>
#include <mpool/mpool_ioctl.h>

#include "../mpool/mpctl.h"

#include "common.h"
#include "mpool.h"

merr_t
split_mp_ds(
	char     *path,
	char    **mpool,
	char    **dataset,
	char    **rest)
{
	char *mp = 0;
	char *ds = 0;

	if (!path || !mpool || !dataset || !rest)
		return merr(EINVAL);

	*mpool = *dataset = *rest = 0;
	if (path[0])
		mp = strsep(&path, "/");

	if (mp[0] && path && path[0])
		ds = strsep(&path, "/");

	if (path)
		*rest = path;

	*mpool   = mp;
	*dataset = ds;

	return 0;
}

void
mpool_generic_sub_help(
	struct help_s  *h,
	bool           terse)
{
	if (terse)
		fprintf(co.co_fp, "  %-16s  %s\n", h->token, h->shelp);
	else
		fprintf(co.co_fp, "\n%s:", "Commands");
}

void
mpool_generic_verb_help(
	struct verb_s      *v,
	struct help_s      *h,
	bool                terse,
	struct param_inst  *pi,
	u32                 flag)
{
	if (terse) {
		fprintf(co.co_fp, "\n  %-12s  %s", h->token, h->shelp);
		return;
	}

	fprintf(co.co_fp, "usage: %s %s%s %s%s\n",
		progname, h->token,
		v ? " [options]" : "",
		h->usage,
		pi ? " [param=value ...]" : "");

	if (h->lhelp)
		fprintf(co.co_fp, "\n  %s\n", h->lhelp);

	if (v)
		xgetopt_usage(v->optstring, v->xoption);

	if (pi)
		show_default_params(pi, flag);

	if (h->example) {
		if (co.co_verbose) {
			int width = strlen(progname) + 2;

			fprintf(co.co_fp, "\nExamples:\n");
			fprintf(co.co_fp, h->example,
				width, progname, h->token,
				width, progname, h->token,
				width, progname, h->token);
		} else {
			fprintf(co.co_fp, "\nUse -hv for more detail\n");
		}
	}

	fprintf(co.co_fp, "\n");
}

void
flags_set_common(
	u32 *flags)
{
	if (co.co_force)
		*flags |= (1u << MP_FLAGS_FORCE);

	if (co.co_resize)
		*flags |= (1u << MP_FLAGS_RESIZE);

	*flags |= (1u << MP_FLAGS_PERMIT_META_CONV);
}

/* MEDIA CLASS */
static
const match_table_t
media_classp_table = {
	{ MP_MED_CAPACITY, "CAPACITY" },
	{ MP_MED_STAGING,  "STAGING" },
	{ -1, NULL }
};

merr_t
get_media_classp(
	const char *str,
	void       *dst,
	size_t      dstsz)
{
	int ret;
	substring_t s;

	if (PARAM_GET_INVALID(u8, dst, dstsz))
		return merr(EINVAL);

	ret = match_token(str, media_classp_table, &s);
	if (ret == -1)
		return merr(EINVAL);

	*(u8 *)dst = ret;

	return 0;
}

merr_t
show_media_classp(
	char       *str,
	size_t      strsz,
	const void *val,
	size_t      unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u8, val))
		return merr(EINVAL);

	n = show_lookup(media_classp_table, str, strsz, *(const u8 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

size_t
show_lookup(
	const struct match_token *mt,
	char       *str,
	size_t      strsz,
	s32         token)
{
	for (; mt->pattern; ++mt) {
		if (mt->token == token)
			return strlcpy(str, mt->pattern, strsz);
	}

	memset(str, 0, strsz);
	return -1;
}

static
const match_table_t
status_table = {
	{ MP_OPTIMAL, "optimal" },
	{ MP_FAULTED, "faulted" },
	{ MP_UNDEF, "offline" },
	{ -1, NULL }
};

merr_t
get_status(
	const char *str,
	void       *dst,
	size_t      dstsz)
{
	int ret;
	substring_t s;

	if (PARAM_GET_INVALID(u32, dst, dstsz))
		return merr(EINVAL);

	ret = match_token(str, status_table, &s);
	if ((ret == -1) || (ret > MP_FAULTED))
		return merr(EINVAL);

	*(u32 *)dst = ret;

	return 0;
}

merr_t
show_status(
	char       *str,
	size_t      strsz,
	const void *val,
	size_t      unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u32, val))
		return merr(EINVAL);

	n = show_lookup(status_table, str, strsz, *(const u32 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}
