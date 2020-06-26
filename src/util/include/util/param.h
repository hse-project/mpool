/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_PARAM_H
#define MPOOL_UTIL_PARAM_H

#include <util/parser.h>

#include <mpool/mpool.h>

typedef mpool_err_t param_get_t(const char *src, void *dst, size_t dstsz);

/**
 * struct xoption - extended option information
 * @optopt:     single-dash option letter from isprint()
 * @optlong:    double-dash option name, may be NULL
 * @optexcl:    list of optons excluded by this option
 * @optdesc:    option description
 * @optflag:    incremented each time option is given
 * @opthidden:  option listed only when mutest is enabled
 *
 * The information given by %xoption is used to build the optstring
 * and long options for getopt_long().  It is also used to print
 * the option usage.
 *
 * %optlong, %optexcl, and %optflag are optional and may be set to NULL.
 */
struct xoption {
	const int       optopt;
	const char     *optlong;
	const char     *optexcl;
	const char     *optdesc;
	int            *optflag;
	bool            opthidden;
	void           *optval;
	size_t          optvalsz;
	param_get_t    *optcvt;
};

/* Make boolean options 'int' so that we can count how many
 * times they were given on the command line.
 */
struct common_opts {
	int     co_activate;    /* -a, --activate           */
	int     co_discard;     /* -D, --discard            */
	int     co_deactivate;  /* -d, --deactivate         */
	int     co_force;       /* -f, --force              */
	int     co_noheadings;  /* -H, --noheading          */
	int     co_help;        /* -h, --help               */
	int     co_log;         /* -L, --log                */
	int     co_noresolve;   /* -N, --noresolve          */
	int     co_dry_run;     /* -n, --dry_run            */
	int     co_nosuffix;    /* -p, --nosuffix           */
	int     co_resize;      /* -r, --resize             */
	int     co_mutest;      /* -T, --micron_test_only   */
	int     co_version;     /* -V, --version            */
	int     co_verbose;     /* -v, --verbose            */
	int     co_yaml;        /* -Y, --yaml               */
	FILE   *co_fp;          /* stdout / stderr based on opt_help */
};
extern struct common_opts co;

extern bool show_advanced_params;

int xgetopt(int argc, char **argv, const char *optstring, const struct xoption *xoptionv);

void xgetopt_usage(const char *optstring, const struct xoption *xoptionv);

#define PARAM_GET_INVALID(_type, _dst, _dstsz)				\
	({								\
		((_dstsz) < sizeof(_type) || !(_dst) ||			\
		 (uintptr_t)(_dst) & (__alignof(_type) - 1));		\
	})

#define PARAM_SHOW_INVALID(_type, _val)					\
	({								\
		(!(_val) || (uintptr_t)val & (__alignof(_type) - 1));	\
	})

typedef mpool_err_t param_show_t(char *dst, size_t dstsz, const void *val, size_t val_nb);

typedef mpool_err_t param_check_t(uintptr_t min, uintptr_t max, void *val);

/**
 * struct param_type -
 * @param_token:
 * @param_size:
 * @param_min:
 * @param_max:
 * @param_str_to_val:
 * @param_val_to_str:
 * @param_range_check: validate value is within [param_min, param_max);
 */
struct param_type {
	char           *param_token;
	size_t          param_size;
	uintptr_t       param_min;
	uintptr_t       param_max;
	param_get_t    *param_str_to_val;
	param_show_t   *param_val_to_str;
	param_check_t  *param_range_check;
};

/**
 * struct param_inst -
 * @pi_type:  parameter props and getter/setter ops
 * @pi_value: ptr to the parameter
 * @pi_msg:   Short description of the parameter
 * @pi_entered: set to false before the command line parameters are parsed.
 *	The parser set it to true if the parameter is present/entered on the
 *	command line.
 */
struct param_inst {
	struct param_type   pi_type;
	void               *pi_value;
	char               *pi_msg;
	u32                 pi_flags;
	bool                pi_entered;
};

param_get_t get_u8;
param_show_t show_u8;
param_check_t check_u8;

param_get_t get_u16;
param_show_t show_u16;
param_check_t check_u16;
param_show_t show_u16_dec;

param_get_t get_u32;
param_show_t show_u32;
param_show_t show_u32_dec;
param_check_t check_u32;

param_get_t get_u32_size;
param_show_t show_u32_size;

param_get_t get_u64;
param_show_t show_u64;
param_show_t show_u64_dec;

param_get_t get_u64_size;
param_show_t show_u64_size;

param_get_t get_s64;

param_get_t get_string;
param_show_t show_string;

param_get_t get_bool;
param_show_t show_bool;

param_get_t get_uid;
param_show_t show_uid;
#define PARAM_TYPE_UID { "uid=%s", sizeof(uid_t), 0, 0, get_uid, show_uid, NULL }

param_get_t get_gid;
param_show_t show_gid;
#define PARAM_TYPE_GID { "gid=%s", sizeof(gid_t), 0, 0, get_gid, show_gid, NULL }

param_get_t get_mode;
param_show_t show_mode;
#define PARAM_TYPE_MODE { "mode=%s", sizeof(mode_t), 0, 0, get_mode, show_mode, NULL }

size_t space_to_string(u64 spc, char *string, size_t strsz);

/**
 * process_params() - process a set of command-line params
 * @argc: int, number of arguments available on the command-line
 * @argv: char **, an array of command-line arguments to parse
 * @pi: struct param *, an array defining how the params should
 *         be parsed.
 * @next_arg: int *, will be set to first arg in list that is not a param
 * @flag:
 *
 * The 'mpool' UI expects params to have the form x=y where x is the
 * param name, and y is the value. The 'sp' array defines how
 * a given param should be processed in the form of a string to
 * handler array.
 */
mpool_err_t process_params(int argc, char **argv, struct param_inst *pi, int *next_arg, u32 flag);

/**
 * show_default_params() - show available params
 * @params: struct param *, list of available params
 * @flag:
 * Part of CLI help functionality.
 */
void show_default_params(struct param_inst *params, u32 flag);

enum param_flag {
	PARAM_FLAG_ADVANCED     = 0x1,
	PARAM_FLAG_TUNABLE      = 0x2,
	PARAM_FLAG_ID           = 0x4,
	PARAM_FLAG_BOUND_CK     = 0x8,
};

#define PARAM_TYPE_END {NULL, 0, 0, 0, 0, 0, NULL}

#define PARAM_INST(ptype, val, msg)  {ptype, (void *)&val, msg, 0}

#define PARAM_INST_ADV(ptype, val, msg) {ptype, (void *)&val, msg, \
			PARAM_FLAG_ADVANCED}

#define PARAM_INST_END \
	{PARAM_TYPE_END, 0, NULL, 0}

/* Special purpose PARAM_INST macros for certain data types */
#define PARAM_INST_type(type, val, name, msg)\
	{{name"=%s", sizeof(type), 0, 0, get_##type, show_##type, NULL}, \
	(void *)&val, msg, 0}

#define PARAM_INST_ADV_type(type, val, name, msg)\
	{{name"=%s", sizeof(type), 0, 0, get_##type, show_##type, NULL}, \
	(void *)&val, msg, PARAM_FLAG_ADVANCED}

#define PARAM_INST_STRING(val, valsz, name, msg)\
	{{name"=%s", valsz, 0, 0, get_string, show_string, NULL}, \
	(void *)&val, msg, 0}

#define PARAM_INST_U8(val, name, msg)\
	PARAM_INST_type(u8, val, name, msg)

#define PARAM_INST_U16(val, name, msg)\
	PARAM_INST_type(u16, val, name, msg)

#define PARAM_INST_U16_ADV(val, name, msg)\
	PARAM_INST_ADV_type(u16, val, name, msg)

#define PARAM_INST_U32(val, name, msg)\
	PARAM_INST_type(u32, val, name, msg)

typedef u32 u32_size;
#define PARAM_INST_U32_SIZE(val, name, msg)\
	PARAM_INST_type(u32_size, val, name, msg)

#define PARAM_INST_U64(val, name, msg)\
	PARAM_INST_type(u64, val, name, msg)

#define PARAM_INST_U64_ADV(val, name, msg)\
	PARAM_INST_ADV_type(u64, val, name, msg)

typedef u64 u64_size;
#define PARAM_INST_U64_SIZE(val, name, msg)\
	PARAM_INST_type(u64_size, val, name, msg)

#define PARAM_INST_BOOL(val, name, msg)\
	PARAM_INST_type(bool, val, name, msg)

#define PARAM_INST_GID(val, name, msg)                                  \
	{{name"=%s", sizeof(gid_t), 0, 0, get_gid, show_gid, NULL},     \
	(void *)&val, msg, PARAM_FLAG_ID}

#define PARAM_INST_UID(val, name, msg)                                  \
	{{name"=%s", sizeof(uid_t), 0, 0, get_uid, show_uid, NULL},     \
	(void *)&val, msg, PARAM_FLAG_ID}

#define PARAM_INST_MODE(val, name, msg)                                 \
	{{name"=%s", sizeof(mode_t), 0, 0, get_mode, show_mode, NULL},  \
	(void *)&val, msg, PARAM_FLAG_TUNABLE}

#define PARAM_INST_PCT(val, name, msg)                               \
	{{name"=%s", sizeof(u8), 0, 101, get_u8, show_u8, check_u8}, \
	(void *)&val, msg, PARAM_FLAG_TUNABLE}

#endif /* MPOOL_UTIL_PARAM_H */
