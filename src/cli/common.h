/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_COMMON_H
#define MPOOL_COMMON_H

#include <mpool/mpool.h>

#include <util/inttypes.h>
#include <util/param.h>

#define MPOOL_LIST_BUFSZ    (1024 * 1024)

struct subject_s;
struct verb_s;

typedef mpool_err_t verb_func_t(struct verb_s *v, int argc, char **argv);

typedef void vhelp_func_t(struct verb_s *, bool terse);
typedef void shelp_func_t(bool terse);
typedef void version_func_t(void);
typedef void usage_func_t(void);

struct verb_s {
	const char             *name;
	const char             *optstring;
	verb_func_t            *func;
	vhelp_func_t           *help;
	const struct xoption   *xoption;
	bool                    hidden;
};

struct subject_s {
	char           *name;
	struct verb_s  *verb;
	shelp_func_t   *help;
	usage_func_t   *usage;
	version_func_t *version;
};

struct help_s {
	const char *token;
	const char *shelp;
	const char *lhelp;
	const char *usage;
	const char *example;
};

void mpool_generic_sub_help(struct help_s *h, bool terse);

void
mpool_generic_verb_help(
	struct verb_s      *v,
	struct help_s      *h,
	bool               terse,
	struct param_inst  *pi,
	u32                 flag);

void flags_set_common(u32 *flags);

mpool_err_t get_media_classp(const char *str, void *dst, size_t dstsz);

mpool_err_t show_media_classp(char *str, size_t strsz, const void *val, size_t unused);

size_t show_lookup(const struct match_token *mt, char *str, size_t strsz, s32 token);

mpool_err_t get_status(const char *str, void *dst, size_t dstsz);

mpool_err_t show_status(char *str, size_t strsz, const void *val, size_t unused);

#endif
