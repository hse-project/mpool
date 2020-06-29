// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <util/platform.h>
#include <util/param.h>
#include <util/alloc.h>
#include <util/page.h>
#include <util/string.h>
#include <util/minmax.h>
#include <util/parse_num.h>
#include <util/printbuf.h>

#include <sysexits.h>
#include <assert.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>

#define merr(_errnum)   (_errnum)

struct common_opts co;

static bool is_mutest_enabled(void)
{
	char *mutest;

	if (co.co_mutest)
		return true;

	mutest = getenv("MICRON_TEST_ONLY");

	return mutest ? atoi(mutest) : false;
}

static int excludes(const struct xoption *given, const struct xoption *xoptionv)
{
	const struct xoption *opt;

	if (!given || !given->optexcl || !xoptionv)
		return 0;

	for (opt = xoptionv; opt->optopt > 0; ++opt) {
		if (!strchr(given->optexcl, opt->optopt))
			continue;

		if (opt->optflag && *opt->optflag)
			return opt->optopt;
	}

	return 0;
}

int xgetopt(int argc, char **argv, const char *optstring, const struct xoption *xoptionv)
{
	const struct xoption   *opt;
	struct option           longopts[32];
	mpool_err_t             err;

	char    optstringbuf[32 * 3 + 4];
	char   *progname, *pc, *pos;
	char    errbuf[128];
	int     n;

	co.co_mutest = is_mutest_enabled();
	co.co_fp = stderr;
	optind = 0;

	if (argc < 1 || !argv || !optstring || !xoptionv)
		return 0;

	pc = optstringbuf;
	if (*optstring == '+')
		*pc++ = *optstring++;

	pc = strcpy(pc, ":W;");
	pc += strlen(pc);
	n = 0;

	/* Build optstring and longopts for getopt_long().
	 */
	for (opt = xoptionv; *optstring && opt->optopt > 0; ++opt) {
		int arg = no_argument;

		assert(isprint(opt->optopt));

		pos = strchr(optstring, opt->optopt);
		if (!pos)
			continue;

		*pc++ = *pos;

		if (opt->optval) {
			arg = required_argument;
			*pc++ = ':';
		}

		if (pos == optstring)
			++optstring;

		if (opt->optlong) {
			longopts[n].name = opt->optlong;
			longopts[n].has_arg = arg;
			longopts[n].flag = NULL;
			longopts[n].val = opt->optopt;
			++n;
		}
	}

	memset(longopts + n, 0, sizeof(longopts[0]));
	*pc = '\000';

	optstring = optstringbuf;

	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];

	while (1) {
		int longind = -1;
		char name[32];
		int c, x;

		c = getopt_long(argc, argv, optstring, longopts, &longind);
		if (c == -1)
			break;

		if (longind >= 0)
			snprintf(name, sizeof(name), "--%s", longopts[longind].name);
		else if (optopt || (c && c != '?'))
			snprintf(name, sizeof(name), "-%c", optopt ?: c);
		else
			strlcpy(name, argv[optind - 1], sizeof(name));

		for (opt = xoptionv; opt->optopt > 0; ++opt)
			if (opt->optopt == c)
				break;

		x = excludes(opt, xoptionv);
		if (x) {
			fprintf(co.co_fp, "%s: option -%c excludes -%c, use -h for help\n",
				progname, x, opt->optopt);
			return EX_USAGE;
		}

		if (opt->optflag)
			(*opt->optflag)++;

		if (opt->optval && opt->optcvt) {
			err = opt->optcvt(optarg, opt->optval, opt->optvalsz);
			if (err) {
				mpool_strinfo(err, errbuf, sizeof(errbuf));
				fprintf(co.co_fp, "%s: unable to convert `%s %s': %s\n",
					progname, name, optarg, errbuf);
				return EX_USAGE;
			}
		}

		switch (c) {
		case ':':
			fprintf(co.co_fp, "%s: option %s requires an argument, use -h for help\n",
				progname, name);
			return EX_USAGE;

		case '?':
			fprintf(co.co_fp, "%s: invalid option %s, use -h for help\n",
				progname, name);
			return -1;

		case 0:
			if (longopts[longind].flag)
				break;
			/* FALLTHROUGH */

		default:
			if (opt->optflag)
				break;

			fprintf(co.co_fp, "%s: unhandled option %s, use -h for help\n",
				progname, name);
			return EX_USAGE;
		}
	}

	if (!co.co_mutest && co.co_log) {
		fprintf(co.co_fp, "%s: invalid option %s, use -h for help\n",
			progname, co.co_log ? "--log|-L" : "bad option");

		return EX_USAGE;
	}

	return 0;
}

void xgetopt_usage(const char *optstring, const struct xoption *xoptionv)
{
	const char *hdr = "\nOptions:\n";
	const char *valname = "ARG";
	const struct xoption *opt;
	int     width = 0;
	size_t  len;

	if (!optstring || !xoptionv)
		return;

	for (opt = xoptionv; opt->optopt > 0; ++opt) {
		if (!strchr(optstring, opt->optopt))
			continue;

		len = opt->optlong ? strlen(opt->optlong) + 4 : 0;
		if (opt->optval)
			len += strlen(valname) + 1;
		if (len > width)
			width = len;
	}

	for (opt = xoptionv; opt->optopt > 0; ++opt) {
		char argbuf[32], *fmt;
		char excludes[32];

		if (!strchr(optstring, opt->optopt))
			continue;

		if (opt->opthidden && !co.co_mutest)
			continue;

		snprintf(excludes, sizeof(excludes), opt->optexcl ? " (excludes -%s)" : "",
			 opt->optexcl);

		fmt = opt->optlong ? ", --%s" : "";
		if (opt->optval)
			fmt = opt->optlong ? ", --%s=%s" : " %s%s";
		snprintf(argbuf, sizeof(argbuf), fmt, opt->optlong ?: "", valname);

		fprintf(co.co_fp, "%s  -%c%-*s    %s%s\n",
			hdr, opt->optopt, width, argbuf, opt->optdesc, excludes);

		hdr = "";
	}
}

mpool_err_t get_u8(const char *src, void *dst, size_t dstsz)
{
	if (PARAM_GET_INVALID(u8, dst, dstsz))
		return merr(EINVAL);

	return parse_u8(src, dst);
}

mpool_err_t show_u8(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u8, val))
		return merr(EINVAL);

	n = snprintf(str, strsz, "0x%hhx", *(const u8 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t check_u8(uintptr_t min, uintptr_t max, void *val)
{
	if (*(u8 *)val < (u8)min || *(u8 *)val >= (u8)max)
		return merr(ERANGE);

	return 0;
}


mpool_err_t get_u16(const char *src, void *dst, size_t dstsz)
{
	if (PARAM_GET_INVALID(u16, dst, dstsz))
		return merr(EINVAL);

	return parse_u16(src, dst);
}

mpool_err_t show_u16(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u16, val))
		return merr(EINVAL);

	n = snprintf(str, strsz, "0x%hx", *(const u16 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t show_u16_dec(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u16, val))
		return merr(EINVAL);

	n = snprintf(str, strsz, "%hu", *(const u16 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t check_u16(uintptr_t min, uintptr_t max, void *val)
{
	if (*(u16 *)val < (u16)min || *(u16 *)val >= (u16)max)
		return merr(ERANGE);

	return 0;
}


mpool_err_t get_u32(const char *src, void *dst, size_t dstsz)
{
	if (PARAM_GET_INVALID(u32, dst, dstsz))
		return merr(EINVAL);

	return parse_u32(src, dst);
}

mpool_err_t show_u32(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u32, val))
		return merr(EINVAL);

	n = snprintf(str, strsz, "0x%x", *(const u32 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}


mpool_err_t check_u32(uintptr_t min, uintptr_t max, void *val)
{
	if (*(u32 *)val < (u32)min || *(u32 *)val >= (u32)max)
		return merr(ERANGE);

	return 0;
}

mpool_err_t show_u32_dec(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u32, val))
		return merr(EINVAL);

	n = snprintf(str, strsz, "%u", *(const u32 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t get_u32_size(const char *src, void *dst, size_t dstsz)
{
	u64 v;
	mpool_err_t err;

	if (PARAM_GET_INVALID(u32, dst, dstsz))
		return merr(EINVAL);

	err = parse_size(src, &v);
	*(u32 *)dst = (u32)v;
	return err;
}

mpool_err_t show_u32_size(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u32, val))
		return merr(EINVAL);

	n = space_to_string(*(const u32 *)val, str, strsz);

	return (n < strsz) ? 0 : merr(EINVAL);
}

size_t space_to_string(u64 spc, char *string, size_t strsz)
{
	static const char  suffixtab[] = "\0kmgtpezy";
	double      space = spc;
	const char *stp;

	stp = suffixtab;
	while (space >= 1024) {
		space /= 1024;
		++stp;
	}

	return snprintf(string, strsz, "%4.2lf%c", space, *stp);
}

mpool_err_t get_u64(const char *src, void *dst, size_t dstsz)
{
	if (PARAM_GET_INVALID(u64, dst, dstsz))
		return merr(EINVAL);

	return parse_u64(src, dst);
}

mpool_err_t show_u64(char *str, size_t strsz, const void *val, size_t unused)
{
	u64 hex_threshold = 64 * 1024;
	const char *fmt;
	size_t n;

	if (PARAM_SHOW_INVALID(u64, val))
		return merr(EINVAL);

	fmt = (*(const u64 *)val < hex_threshold) ? "%lu" : "0x%lx";
	n = snprintf(str, strsz, fmt, *(const u64  *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t show_u64_dec(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u64, val))
		return merr(EINVAL);

	n = snprintf(str, strsz, "%lu", (ulong) *(const u64 *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t get_u64_size(const char *src, void *dst, size_t dstsz)
{
	if (PARAM_GET_INVALID(u64, dst, dstsz))
		return merr(EINVAL);

	return parse_size(src, dst);
}

mpool_err_t show_u64_size(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(u64, val))
		return merr(EINVAL);

	n = space_to_string(*(const u64 *)val, str, strsz);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t get_s64(const char *src, void *dst, size_t dstsz)
{
	if (PARAM_GET_INVALID(s64, dst, dstsz))
		return merr(EINVAL);

	return parse_s64(src, dst);
}

mpool_err_t get_string(const char *src, void *dst, size_t dstsz)
{
	size_t n;

	assert(src >= (char *)dst + dstsz || (char *)dst >= src + strlen(src));

	n = strlcpy(dst, src, dstsz);

	return (n < dstsz) ? 0 : merr(EINVAL);
}

mpool_err_t show_string(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	assert((const char *)val >= str + strsz || str >= (const char *)val + strsz);

	n = strlcpy(str, val, strsz);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t check_string(uintptr_t min, uintptr_t max, void *val)
{
	if (!val || strlen(val) < min || strlen(val) > max)
		return merr(EINVAL);

	return 0;
}

mpool_err_t get_bool(const char *str, void *dst, size_t dstsz)
{
	long    v = 0;

	if (!str || !dst || dstsz < sizeof(bool))
		return merr(EINVAL);

	/* Allow leading and trailing white space.
	 */
	while (isspace(*str))
		++str;

	if (str[0] == '0' || str[0] == '1') {
		char *end = NULL;

		errno = 0;
		v = strtol(str, &end, 10);
		if (errno || end == str)
			return merr(errno ?: EINVAL);

		if (v < 0 || v > 1)
			return merr(EINVAL);
	} else {
		size_t len = strcspn(str, " \t\n\v\f\r");

		if (len == 4 && !strncasecmp("true", str, len))
			v = true;
		else if (len != 5 || strncasecmp("false", str, len))
			return merr(EINVAL);
	}

	*(bool *)dst = v;

	return 0;
}

mpool_err_t show_bool(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(bool, val))
		return merr(EINVAL);

	n = snprintf(str, strsz, "%s", *(const bool *)val ? "true" : "false");

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t get_uid(const char *str, void *dst, size_t dstsz)
{
	struct passwd  *pw;
	char           *end;
	long            num;

	if (PARAM_GET_INVALID(uid_t, dst, dstsz))
		return merr(EINVAL);

	/* str could be a number, or a name */
	end = NULL;
	errno = 0;
	num = strtol(str, &end, 10);
	if (errno || end == str) {
		pw = getpwnam(str);
		if (pw == NULL)
			return merr(EINVAL);
		*(uid_t *)dst = pw->pw_uid;
	} else {
		if (num < -1)
			return merr(EINVAL);
		*(uid_t *)dst = num;
	}
	return 0;
}

mpool_err_t show_uid(char *str, size_t strsz, const void *val, size_t unused)
{
	struct passwd  *pw;
	size_t          n;

	if (PARAM_SHOW_INVALID(uid_t, val))
		return merr(EINVAL);

	pw = getpwuid(*(uid_t *)val);
	if (pw)
		n = strlcpy(str, pw->pw_name, strsz);
	else if (*(int *)val == -1)
		n = strlcpy(str, "-1", strsz);
	else
		n = snprintf(str, strsz, "%u", *(const uid_t *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

mpool_err_t get_gid(const char *str, void *dst, size_t dstsz)
{
	u64             num;
	char           *ep = NULL;
	struct group   *gr;

	if (PARAM_GET_INVALID(gid_t, dst, dstsz))
		return merr(EINVAL);

	/* str could be a number, or a name */
	errno = 0;
	num = strtoul(str, &ep, 0);
	if (errno || ep == str || *ep) {
		gr = getgrnam(str);
		if (gr == NULL)
			return merr(EINVAL);
		*(gid_t *)dst = gr->gr_gid;
	} else {
		*(gid_t *)dst = num;
	}
	return 0;
}

mpool_err_t show_gid(char *str, size_t strsz, const void *val, size_t unused)
{
	struct group   *gr;
	size_t          len;

	if (PARAM_SHOW_INVALID(gid_t, val))
		return merr(EINVAL);

	gr = getgrgid(*(gid_t *)val);
	if (gr)
		len = strlcpy(str, gr->gr_name, strsz);
	else if (*(int *)val == -1)
		len = strlcpy(str, "-1", strsz);
	else
		len = snprintf(str, strsz, "%u", *(const gid_t *)val);

	return len >= strsz ? merr(EINVAL) : 0;
}

/**
 * get_mode() - convert an octal string in an integer.
 * @value:  input
 * @val:    output
 *
 * Return: EINVAL if ill formatted or if the result is greater than 0777
 */
mpool_err_t get_mode(const char *str, void *dst, size_t dstsz)
{
	u64   num;
	char *ep = NULL;

	if (PARAM_GET_INVALID(mode_t, dst, dstsz))
		return merr(EINVAL);

	if (str[0] == 0)
		return merr(EINVAL);

	num = strtoul(str, &ep, 8);
	if (*ep != 0)
		return merr(EINVAL);
	if (num > 0777)
		return merr(EINVAL);

	*(mode_t *)dst = num;

	return 0;
}

mpool_err_t show_mode(char *str, size_t strsz, const void *val, size_t unused)
{
	size_t n;

	if (PARAM_SHOW_INVALID(mode_t, val))
		return merr(EINVAL);

	if (*(int *)val == -1)
		n = strlcpy(str, "-1", strsz);
	else
		n = snprintf(str, strsz, "0%02o", *(const mode_t *)val);

	return (n < strsz) ? 0 : merr(EINVAL);
}

static void shuffle(int argc, char **argv, int insert, int check)
{
	char *saved = argv[check];
	int   i;

	for (i = check; i > insert; i--)
		argv[i] = argv[i-1];
	argv[insert] = saved;
}

static mpool_err_t
param_gen_match_table(struct param_inst *piv, struct match_token **table, int *entry_cnt)
{
	struct match_token *t;
	struct param_inst  *pi;
	int                 cnt;

	cnt = 0;
	for (pi = piv; pi->pi_type.param_token; ++pi)
		++cnt;

	t = calloc(cnt + 1, sizeof(*piv));
	if (!t)
		return merr(ENOMEM);

	*entry_cnt = cnt;
	*table = t;
	cnt = 0;

	for (pi = piv; pi->pi_type.param_token; ++pi) {
		t->pattern = pi->pi_type.param_token;
		t->token = cnt++;
		t++;
	}

	t->pattern = NULL;
	t->token = -1;

	return 0;
}

static void param_free_match_table(struct match_token *table)
{
	free(table);
}

bool show_advanced_params;

mpool_err_t process_params(int argc, char **argv, struct param_inst *piv, int *argindp, u32 flag)
{
	struct match_token *table;
	substring_t         val;
	mpool_err_t         err;

	int     arg, index, entry_cnt;

	if (argc < 1)
		return 0;

	/* Reset "given" count (note that this function is deterministically
	 * reusable only if the caller resets all the default values).
	 */
	for (index = 0; piv[index].pi_type.param_token != NULL; index++)
		piv[index].pi_given = 0;

	err = param_gen_match_table(piv, &table, &entry_cnt);
	if (err)
		return err;

	for (arg = 0; arg < argc; arg++) {
		struct param_inst *pi;

		index = match_token(argv[arg], table, &val);
		if (index < 0 || index >= entry_cnt)
			continue;

		pi = piv + index;

		if (!show_advanced_params &&
		    (pi->pi_flags & PARAM_FLAG_ADVANCED))
			continue; /* skip advanced params */

		if (flag && !(flag & pi->pi_flags))
			continue; /* skip if type not requested */

		err = pi->pi_type.param_str_to_val(val.from, pi->pi_value, pi->pi_type.param_size);
		if (err)
			goto out;

		/* Validate if pi_value is within allowed range */
		if (pi->pi_type.param_range_check) {
			err = pi->pi_type.param_range_check(pi->pi_type.param_min,
							    pi->pi_type.param_max, pi->pi_value);
			if (err) {
				char   *token = pi->pi_type.param_token;
				size_t  len = strcspn(token, "=");
				char    name[128];

				len = min_t(size_t, sizeof(name) - 1, len);
				strncpy(name, token, len);
				name[len] = '\0';
				goto out;
			}
		}

		pi->pi_given++;
		shuffle(argc, argv, 0, arg);
		if (argindp)
			(*argindp)++;
	}

out:
	param_free_match_table(table);

	if (err && argindp)
		*argindp = arg;

	return err;
}

static void param_get_name(int index, char *buf, size_t buf_sz, const struct param_inst *table)
{
	char  *key;
	size_t len;

	assert(buf);
	assert(buf_sz);

	key = table[index].pi_type.param_token;
	len = strcspn(key, "=");

	strlcpy(buf, key, MIN(len + 1, buf_sz));
}

mpool_err_t verify_params(const struct param_inst *paramv, char *buf, size_t bufsz)
{
	while (paramv && paramv->pi_type.param_token) {
		char name[128];

		if (paramv->pi_given < paramv->pi_given_min) {
			const char *fmt = "`%s' must be specified at least once";

			if (paramv->pi_given_min > 1)
				fmt = "`%s' must be specified at least %d times";
			param_get_name(0, name, sizeof(name), paramv);
			snprintf(buf, bufsz, fmt, name, paramv->pi_given_min);
			return merr(EINVAL);
		} else if (paramv->pi_given > paramv->pi_given_max) {
			const char *fmt = "`%s' may not be specified more than once";

			if (paramv->pi_given_max > 1)
				fmt = "`%s' may not be specified more than %d times";
			param_get_name(0, name, sizeof(name), paramv);
			snprintf(buf, bufsz, fmt, name, paramv->pi_given_max);
			return merr(EINVAL);
		}

		++paramv;
	}

	return 0;
}


/* Set the values in table params to defaults before calling this function
 */
void show_default_params(struct param_inst *params, u32 flag)
{
	const char *hdr = "\nParams:\n";

	for (; params->pi_type.param_token; ++params) {
		char name[32], value[128];

		if (!show_advanced_params &&
		    (params->pi_flags & PARAM_FLAG_ADVANCED))
			continue; /* skip unwanted advanced param */

		if (flag && !(flag & params->pi_flags))
			continue; /* skip type not requested */

		param_get_name(0, name, sizeof(name), params);

		value[0] = '\000';
		params->pi_type.param_val_to_str(value, sizeof(value), params->pi_value, 1);

		fprintf(co.co_fp, "%s  %-8s  %s (default: %s)\n", hdr, name, params->pi_msg, value);

		hdr = "";
	}
}
