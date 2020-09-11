// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#define _ISOC11_SOURCE

#include <util/base.h>
#include <util/page.h>

#include <mpool/mpool.h>

#include <mpctl/impool.h>

#include <sys/ioctl.h>
#include <stdarg.h>
#include <sysexits.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>

#define merr(_errnum)   (_errnum)

#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })


#if !defined(__USE_ISOC11)
#define aligned_alloc(_align, _size)    memalign((_align), (_size))
extern void *memalign(size_t, size_t) __attribute_malloc__ __wur;
#endif

struct mpool_cmd {
	const char *cmd;
	const char *synopsis;
	int         (*run)(int argc, char **argv);
	void        (*help)(int argc, char **argv);
};

static const struct mpool_cmd mpool_cmds[]; /* initialized at end of file */

static const char  *fmt_insufficient_arguments =
	"insufficient arguments for mandatory parameters";

static bool     headers = true;
static int      dry_run;

const char     *progname;
int             verbosity;
int             debug;


const char *mp_cksum_type_strv[] = {
	[MP_CK_UNDEF]  = "undef",
	[MP_CK_NONE]   = "none",
	[MP_CK_DIF]    = "dif",
	"invalid" /* must be last item */
};

const char *mp_media_classp_strv[] = {
	[MP_MED_STAGING]   = "ingest",
	[MP_MED_CAPACITY]  = "capacity",
	"invalid" /* must be last item */
};

const char *mpool_status_strv[] = {
	[MPOOL_STAT_UNDEF]      = "offline",
	[MPOOL_STAT_OPTIMAL]    = "optimal",
	[MPOOL_STAT_FAULTED]    = "faulted",
	"invalid" /* must be last item */
};

#define ENUM_VAL2NAME(a, val) \
	enum_val2name(a ## _strv, NELEM(a ## _strv) - 1, (uint)(val))

#define ENUM_NAME2VAL(a, name) \
	enum_name2val(a ## _strv, NELEM(a ## _strv) - 1, (name))

static const char *enum_val2name(const char **strv, uint strc, uint val)
{
	return strv[(val < strc && strv[val]) ? val : strc];
}

static void syntax(const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s: %s, use -h for help\n", progname, msg);
}

static void eprint(const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s: %s\n", progname, msg);
}

/* Note: This function is not thread-safe.
 */
static const char *strerrinfo(struct mpool_devrpt *ei, mpool_err_t err)
{
	static char errbuf[128];

	if (ei && ei->mdr_rcode) {
		ei->mdr_msg[sizeof(ei->mdr_msg) - 1] = '\000';

		if (ei->mdr_rcode == MPOOL_RC_ERRMSG)
			return ei->mdr_msg;

		return mpool_devrpt_strerror(ei->mdr_rcode);
	}

	return mpool_strinfo(err, errbuf, sizeof(errbuf));
}

/* Common/global options for inclusion into 'struct option'.
 */
#define COMOPTS_OPTIONS \
	{ "brief",	no_argument,		NULL, 'q' }, \
	{ "debug",	no_argument,		NULL, 'd' }, \
	{ "dryrun",	no_argument,		NULL, 'n' }, \
	{ "help",	no_argument,		NULL, 'h' }, \
	{ "no-headers",	no_argument,		NULL, 'H' }, \
	{ "type",	required_argument,	NULL, 't' }, \
	{ "verbose",	no_argument,		NULL, 'v' }, \
	{ "version",	no_argument,		NULL, 'V' }

/* Handler for common options specified by COMOPTS_OPTIONS.
 * Also handles ':' and '?' as returned by getopt().
 */
static void
comopts_handler(
	int             c,
	char           *optarg,
	char          **argv,
	int             curind,
	struct option  *longopts,
	int             longidx)
{
	switch (c) {
	case 'H':
		headers = false;
		break;

	case 'n':
		dry_run++;
		break;

	case 'd':
		debug = 1;
		break;

	case 'q':
		verbosity = 0;
		break;

	case 'V':
		printf("%s  %s  %s\n", mpool_version, mpool_tag, mpool_sha);
		exit(0);

	case 'v':
		verbosity++;
		break;

	case ':':
		syntax("missing argument for option '%s'", argv[curind]);
		exit(EX_USAGE);

	case '?':
		syntax("invalid option '%s'", argv[curind]);
		exit(EX_USAGE);

	default:
		if (c)
			syntax("unhandled option '%s'", argv[curind]);
		else if (!longopts[longidx].flag)
			syntax("unhandled option '--%s'",
			       longopts[longidx].name);
		break;
	}
}

static void prop_init(struct mpioc_prop *prop)
{
	memset(prop, 0, sizeof(*prop));

	/* TODO: These default properties will need to be parameterized */

	prop->pr_xprops.ppx_params.mp_uid = -1;
	prop->pr_xprops.ppx_params.mp_gid = -1;
	prop->pr_xprops.ppx_params.mp_mode = -1;
}

static void prop_dump(const struct mpioc_prop *prop, const char *which)
{
	const struct mpool_params  *props;

	char            uidstr[32], gidstr[32], modestr[32];
	struct passwd  *pw;
	struct group   *gr;
	char            uuidstr[40];
	const char     *name;
	int             len;

	if (!prop)
		return;

	if (which && 0 == strcmp(which, "all"))
		which = NULL;

	props = &prop->pr_xprops.ppx_params;

	name = props->mp_name;
	len = strlen(name);
	if (len < 4)
		len = 4;

	snprintf(uidstr, sizeof(uidstr), "(inherited)");
	if (props->mp_uid != -1) {
		snprintf(uidstr, sizeof(uidstr), "%u", props->mp_uid);
		pw = getpwuid(props->mp_uid);
		if (pw) {
			strncpy(uidstr, pw->pw_name, sizeof(uidstr) - 1);
			uidstr[sizeof(uidstr) - 1] = '\000';
		}
	}

	snprintf(gidstr, sizeof(gidstr), "(inherited)");
	if (props->mp_gid != -1) {
		snprintf(gidstr, sizeof(gidstr), "%u", props->mp_gid);
		gr = getgrgid(props->mp_gid);
		if (gr) {
			strncpy(gidstr, gr->gr_name, sizeof(gidstr) - 1);
			gidstr[sizeof(gidstr) - 1] = '\000';
		}
	}

	snprintf(modestr, sizeof(modestr), "(inherited)");
	if (props->mp_mode != -1)
		snprintf(modestr, sizeof(modestr), "0%02o", props->mp_mode);

	uuid_unparse(prop->pr_xprops.ppx_params.mp_poolid, uuidstr);

	if (headers)
		printf("%-*s  %-10s  %s\n", len, "NAME", "PROPERTY", "VALUE");

	if (!which || strstr(which, "fusable"))
		printf("%-*s  fusable     %lu\n", len, name, prop->pr_usage.mpu_fusable);

	if (!which || strstr(which, "label"))
		printf("%-*s  label       %s\n", len, name, props->mp_label);

	if (!which || strstr(which, "gid"))
		printf("%-*s  gid         %s\n", len, name, gidstr);

	if (!which || strstr(which, "mode"))
		printf("%-*s  mode        %s\n", len, name, modestr);

	if (!which || strstr(which, "mlog0"))
		printf("%-*s  mlog0       0x%lx\n", len, name, (ulong)props->mp_oidv[0]);

	if (!which || strstr(which, "mlog1"))
		printf("%-*s  mlog1       0x%lx\n", len, name, (ulong)props->mp_oidv[1]);

	if (!which || strstr(which, "poolid"))
		printf("%-*s  uuid        %s\n", len, name, uuidstr);

	if (!which || strstr(which, "status"))
		printf("%-*s  status      %s\n", len, name,
		       ENUM_VAL2NAME(mpool_status, props->mp_stat));

	if (!which || strstr(which, "total"))
		printf("%-*s  total       %lu\n", len, name, prop->pr_usage.mpu_total);

	if (!which || strstr(which, "uid"))
		printf("%-*s  uid         %s\n", len, name, uidstr);

	if (!which || strstr(which, "usable"))
		printf("%-*s  usable      %lu\n", len, name, prop->pr_usage.mpu_usable);

	if (!which || strstr(which, "used"))
		printf("%-*s  used        %lu\n", len, name, prop->pr_usage.mpu_used);

	printf("\n");
}

/* Scan the list for name/value pairs separated by the given separator.
 * Decode each name/value pair and store the result into the given
 * property object.
 *
 * Check to ensure each name scanned from the list also appears in the
 * list of valid property names.
 *
 * Returns an error code from errno.h on failure.
 * Returns 0 on success.
 */
static int
prop_decode(struct mpioc_prop *prop, const char *list, const char *sep, const char *valid)
{
	char   *nvlist, *nvlist_base;
	char   *name, *value;
	char    buf[1024];
	ulong   result;
	char   *end;
	int     rc;

	if (!prop || !list)
		return EINVAL;

	nvlist = strdup(list);
	if (!nvlist)
		return ENOMEM;

	nvlist_base = nvlist;

	for (rc = 0; nvlist; rc = 0) {
		while (isspace(*nvlist))
			++nvlist;

		value = strsep(&nvlist, sep);
		name = strsep(&value, "=");

		if (verbosity > 1)
			printf("%s: scanned name=%s value=%s\n", __func__, name, value);

		if (!name || !*name)
			continue;

		if (!value || !*value) {
			syntax("property '%s' has no value", name);
			rc = EINVAL;
			break;
		}

		if (valid && !strstr(valid, name)) {
			syntax("invalid property '%s'", name);
			rc = EINVAL;
			break;
		}

		if (0 == strcmp(name, "uid")) {
			struct passwd passwd, *pw;

			errno = 0;
			end = NULL;
			result = strtoul(value, &end, 0);

			if ((result == ULONG_MAX && errno) || end == value || *end) {
				rc = getpwnam_r(value, &passwd, buf, sizeof(buf), &pw);
				if (rc || !pw) {
					rc = rc ? errno : EINVAL;
					eprint("invalid uid '%s': %s", value, strerror(rc));
					break;
				}

				result = pw->pw_uid;
			}

			prop->pr_xprops.ppx_params.mp_uid = result;
			continue;
		}

		if (0 == strcmp(name, "gid")) {
			struct group group, *gr;

			errno = 0;
			end = NULL;
			result = strtoul(value, &end, 0);

			if ((result == ULONG_MAX && errno) || end == value || *end) {
				rc = getgrnam_r(value, &group, buf, sizeof(buf), &gr);
				if (rc || !gr) {
					rc = rc ? errno : EINVAL;
					eprint("invalid gid '%s': %s", value, strerror(rc));
					break;
				}

				result = gr->gr_gid;
			}

			prop->pr_xprops.ppx_params.mp_gid = result;
			continue;
		}

		if (0 == strcmp(name, "mode")) {
			errno = 0;
			end = NULL;
			result = strtoul(value, &end, 8);

			if ((result == ULONG_MAX && errno) || end == value || *end) {
				rc = end ? EINVAL : errno;
				eprint("invalid mode '%s': %s", value, strerror(rc));
				break;
			}

			prop->pr_xprops.ppx_params.mp_mode = result;
			continue;
		}

		/* This is a programming error wherein the caller specified
		 * a property via 'valid' for which there is no decoder.
		 */
		eprint("unhandled property '%s' ignored", name);
	}

	free(nvlist_base);

	return rc;
}

/* Dynamically build optstring from longopts[] for getopt_long().
 */
static char *mkoptstring(const struct option *longopts)
{
	const struct option    *longopt;

	char    optstring[1024];
	char   *pc = optstring;

	*pc++ = '+';    /* Enable POSIXLY_CORRECT behavior */
	*pc++ = ':';    /* Disable getopt error messages */

	for (longopt = longopts; longopt->name; ++longopt) {
		if (!longopt->flag && isprint(longopt->val)) {
			*pc++ = longopt->val;
			if (longopt->has_arg == required_argument) {
				*pc++ = ':';
			} else if (longopt->has_arg == optional_argument) {
				*pc++ = ':';
				*pc++ = ':';
			}

			if (pc >= optstring + sizeof(optstring))
				abort();
		}
	}
	*pc = '\000';

	return strdup(optstring);
}

static const char *name_is_invalid(const char *mpname)
{
	int     i;

	if (!mpname)
		return "is zero length";

	if (!isalpha(*mpname))
		return "does not start with an alphanumeric character";

	for (i = 0; mpname[i]; ++i) {
		if (!isascii(mpname[i]))
			return "contains a non-ascii character";
		if (iscntrl(mpname[i]))
			return "contains a control character";
		if (isblank(mpname[i]))
			return "contains a blank character";
		if (!isprint(mpname[i]))
			return "contains a non-printable character";
		if (strchr("!\"#$%&'()/;<=>?[\\]{|}`", mpname[i]))
			return "contains an invalid character";
	}

	if (*mpname == 'r')
		++mpname;

	if (*mpname++ != 's')
		return NULL;

	if (*mpname++ != 'd')
		return NULL;

	if (!islower(*mpname++))
		return NULL;

	if (*mpname == '\000')
		return "appears to be a disk name";

	if (!isdigit(*mpname++))
		return NULL;

	if (isdigit(*mpname))
		++mpname;

	if (*mpname == '\000')
		return "appears to be a partition name";

	return NULL;
}

static const char *create_proplist = "mclassp,uid,gid,mode";
static const char *mount_proplist = "uid,gid,mode";

static void create_help(int argc, char  **argv)
{
	const char *proplist;
	bool        create;

	create = (0 == strcmp(argv[0], "create"));
	proplist = create ? create_proplist : mount_proplist;

	printf("\n");
	printf("usage: %s %s [options] <mpool> <disk> ...\n", progname, argv[0]);
	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("-h, --help                 print this help list\n");
	printf("-n, --dryrun               do not execute operations\n");
	printf("-o, --prop property=value  specify one or more properties\n");
	printf("-v, --verbose              increase verbosity\n");
	printf("<disk>      disk device, partition, volume, ...\n");
	printf("<mpool>     mpool name\n");
	printf("<property>  one of: %s\n", proplist);
	printf("\n");

	printf("Examples:\n");

	if (create) {
		printf("  %s create -o mclassp=CAPACITY mpool1 %s\n", progname, "sdb7 sdc7 sdd7");
	} else {
		printf("  %s activate mpool1 sdb7 sdc7 sdd7\n", progname);
		/* TODO more examples... */
	}

	printf("\n");
}

/* The create command handles both the "create" and "activate"
 * subcommands as they very similar.
 */
static int create_command(int argc, char **argv)
{
	struct option   longopts[] = {
		{ "pd",		required_argument,	NULL, 'd' },
		{ "prop",	required_argument,	NULL, 'o' },
		COMOPTS_OPTIONS,
		{ NULL }
	};

	enum mp_media_classp   *mclassp;
	struct mpioc_mpool      mp;
	struct mpioc_prop       prop;

	const char *proplist;
	bool        create;
	char      **devicev;
	int         devicec;
	char       *mpname = NULL;
	char       *optstring;
	char       *subcmd;
	int         rc, i;

	subcmd = argv[0];
	create = (0 == strcmp(subcmd, "create"));
	proplist = create ? create_proplist : mount_proplist;

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: out of memory", __func__);
		exit(EX_OSERR);
	}

	devicec = 0;
	optind = 1;
	prop_init(&prop);

	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'h':
			create_help(argc, argv);
			free(optstring);
			exit(0);

		case 'o':
			rc = prop_decode(&prop, optarg, ",", proplist);
			if (rc)
				exit(EX_USAGE);
			break;

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	argc -= optind;
	argv += optind;

	if (verbosity > 1)
		prop_dump(&prop, NULL);

	if (argc < 1) {
		syntax(fmt_insufficient_arguments);
		exit(EX_USAGE);
	}

	devicev = calloc(MPOOL_DRIVES_MAX, sizeof(*devicev));
	mclassp = calloc(MPOOL_DRIVES_MAX, sizeof(*mclassp));

	if (!devicev || !mclassp) {
		eprint("%s: out of memory", __func__);
		exit(EX_OSERR);
	}

	if (argc > 0) {
		if (argc - 1 > MPOOL_DRIVES_MAX) {
			syntax("an mpool may contain no more than %d drives", MPOOL_DRIVES_MAX);
			exit(EX_USAGE);
		}

		mpname = argv[0];

		for (i = 1; i < argc && devicec < MPOOL_DRIVES_MAX; ++i) {
			mclassp[devicec] = MP_MED_CAPACITY;
			devicev[devicec++] = argv[i];
		}
	}

	if (name_is_invalid(mpname)) {
		syntax("mpool name '%s' %s", mpname, name_is_invalid(mpname));
		exit(EX_USAGE);
	}

	if (strlen(mpname) >= sizeof(mp.mp_params.mp_name)) {
		syntax("mpool name may not be longer than %zu characters",
		       sizeof(mp.mp_params.mp_name) - 1);
		exit(EX_USAGE);
	}

	if (devicec >= MPOOL_DRIVES_MAX) {
		syntax("an mpool may contain no more than %d drives", MPOOL_DRIVES_MAX);
		exit(EX_USAGE);
	} else if (devicec < 1) {
		syntax("at least one drive must be specified to %s an mpool", subcmd);
		exit(EX_USAGE);
	}

	if (dry_run)
		goto out;

	memset(&mp, 0, sizeof(mp));
	strcpy(mp.mp_params.mp_name, mpname);
	mp.mp_params = prop.pr_xprops.ppx_params;

	if (1) {
		size_t      dpathssz;
		char       *path;
		mpool_err_t      err;

		dpathssz = 0;

		/* Prepend "/dev/" to each non-fully qualified disk name.
		 */
		for (i = 0; i < devicec; ++i) {
			const char *device = devicev[i];

			if (device[0] != '/') {
				size_t len = strlen(device);

				path = malloc(len + 5 + 1);
				if (!path) {
					eprint("unable to malloc dev buffer");
					exit(EX_OSERR);
				}

				strcpy(path, "/dev/");
				strcat(path, device);
				devicev[i] = path;
			}

			dpathssz += strlen(devicev[i]) + 2;
		}

		mp.mp_dpathc = devicec;
		mp.mp_dpathssz = dpathssz;

		mp.mp_dpaths = malloc(dpathssz);
		if (!mp.mp_dpaths) {
			eprint("unable to malloc drive path buffer");
			exit(EX_OSERR);
		}

		/* Generate a single string containing all the device
		 * paths, each path separated by a newline.
		 */
		path = mp.mp_dpaths;
		*path = '\000';

		for (i = 0; i < devicec; ++i) {
			if (i > 0)
				strcat(path, "\n");
			strcat(path, devicev[i]);
			path += strlen(path);
		}

		if (create) {
			struct mpool_params     params;
			struct mpool_devrpt       ei;

			mpool_params_init(&params);

			params.mp_uid     = mp.mp_params.mp_uid;
			params.mp_gid     = mp.mp_params.mp_gid;
			params.mp_mode    = mp.mp_params.mp_mode;

			err = mpool_create(mpname, devicev[0], &params, 0, &ei);
			if (err) {
				eprint("%s failed: %s", subcmd, strerrinfo(&ei, err));
				free(mp.mp_dpaths);
				exit(EX_DATAERR);
			}
		} else {
			struct mpool_params    params;
			struct mpool_devrpt      ei;

			mpool_params_init(&params);

			err = mpool_activate(mpname, &params, 0, &ei);
			if (err) {
				eprint("%s failed: %s", subcmd, strerrinfo(&ei, err));
				free(mp.mp_dpaths);
				exit(EX_DATAERR);
			}
		}

		for (i = 0; i < devicec; ++i)
			if (devicev[i] != argv[i + 1])
				free(devicev[i]);
		free(mp.mp_dpaths);
	}

out:
	free(devicev);
	free(mclassp);

	return 0;
}

static void destroy_help(int argc, char **argv)
{
	printf("\n");
	printf("usage: %s %s [options] <mpool>\n", progname, argv[0]);
	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("<mpool>   mpool name\n");
	printf("\n");
}

static int destroy_command(int argc, char **argv)
{
	struct option longopts[] = {
		COMOPTS_OPTIONS,
		{ NULL }
	};

	char   *optstring;
	bool    destroy;
	char   *subcmd;

	subcmd = argv[0];
	destroy = (0 == strcmp(subcmd, "destroy"));

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: mkoptstring failed: out of memory", __func__);
		exit(EX_OSERR);
	}

	optind = 1;

	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'h':
			destroy_help(argc, argv);
			free(optstring);
			exit(0);

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		syntax(fmt_insufficient_arguments);
		exit(EX_USAGE);
	} else if (argc > 1) {
		syntax("excessive arguments for mandatory parameters");
		exit(EX_USAGE);
	}

	if (1) {
		struct mpioc_mpool  mp;
		ulong               cmd;
		int                 fd, rc;

		if (strlen(argv[0]) >= sizeof(mp.mp_params.mp_name)) {
			syntax("mpool name may not be longer than %zu chars",
			       sizeof(mp.mp_params.mp_name) - 1);
			exit(EX_USAGE);
		}

		memset(&mp, 0, sizeof(mp));
		strcpy(mp.mp_params.mp_name, argv[0]);

		fd = open(MPC_DEV_CTLPATH, O_RDWR);
		if (-1 == fd) {
			eprint("cannot open mpool control device %s: %s",
			       MPC_DEV_CTLPATH, strerror(errno));
			exit(EX_NOINPUT);
		}

		cmd = destroy ? MPIOC_MP_DESTROY : MPIOC_MP_DEACTIVATE;

		rc = ioctl(fd, cmd, &mp);
		if (rc) {
			char errbuf[128];

			mpool_strinfo(merr(errno), errbuf, sizeof(errbuf));
			eprint("%s %s failed: %s", subcmd, argv[0], errbuf);
			exit(EX_DATAERR);
		}

		close(fd);
	}

	return 0;
}

static void get_help(int argc, char **argv)
{
	printf("\n");
	printf("usage: %s get [options] <property>[,<property>...] <mpool>\n", progname);
	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("<mpool>     mpool name\n");
	printf("<property>  property name (use 'all' to see all properties)\n");
	printf("\n");
}

static int get_command(int argc, char **argv)
{
	struct option longopts[] = {
		COMOPTS_OPTIONS,
		{ NULL }
	};

	struct mpioc_prop  *propv_base, *prop;
	struct mpioc_list   ls;
	size_t              propmax = 1024;
	int                 fd, rc;
	int                 i, j, jmin;
	char               *optstring;

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: mkoptstring failed: out of memory", __func__);
		exit(EX_OSERR);
	}

	optind = 1;

	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'h':
			get_help(argc, argv);
			free(optstring);
			exit(0);

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		syntax(fmt_insufficient_arguments);
		exit(EX_USAGE);
	}

	propv_base = calloc(propmax, sizeof(*propv_base));
	if (!propv_base) {
		fprintf(stderr, "%s: calloc(propv) failed\n", progname);
		exit(EX_OSERR);
	}

	memset(&ls, 0, sizeof(ls));
	ls.ls_listv = propv_base;
	ls.ls_listc = propmax;
	ls.ls_cmd = MPIOC_LIST_CMD_PROP_LIST;

	fd = open(MPC_DEV_CTLPATH, O_RDONLY);
	if (-1 == fd) {
		eprint("cannot open mpool control device %s: %s\n",
		       MPC_DEV_CTLPATH, strerror(errno));
		exit(EX_NOINPUT);
	}

	rc = ioctl(fd, MPIOC_PROP_GET, &ls);
	if (rc) {
		char errbuf[256];

		mpool_strinfo(merr(errno), errbuf, sizeof(errbuf));
		eprint("list failed: %s\n", errbuf);
		exit(EX_DATAERR);
	}

	close(fd);

	for (jmin = 1, i = 0; i < ls.ls_listc && jmin < argc; ++i) {
		const char *mpname;

		prop = propv_base + i;
		mpname = prop->pr_xprops.ppx_params.mp_name;

		for (j = jmin; j < argc; ++j) {
			if (0 == strcmp(argv[j], mpname)) {
				prop_dump(prop, argv[0]);
				if (j == jmin)
					++jmin;
				break;
			}
		}
	}

	return 0;
}

static void set_help(int argc, char **argv)
{
	printf("\n");
	printf("usage: %s set [options] property=value mpool\n", progname);
	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("<mpool>     mpool name\n");
	printf("<property>  property name\n");
	printf("\n");
}

static int set_command(int argc, char **argv)
{
	struct option longopts[] = {
		COMOPTS_OPTIONS,
		{ NULL }
	};

	char   *optstring;

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: mkoptstring failed: out of memory", __func__);
		exit(EX_OSERR);
	}

	optind = 1;

	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'h':
			set_help(argc, argv);
			free(optstring);
			exit(0);

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	fprintf(stderr, "%s: set command not yet implemented\n", progname);
	return 0;
}

static void list_help(int argc, char **argv)
{
	printf("\n");
	printf("usage: %s list [options] [<mpool> ...]\n", progname);
	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("-h, --help         print this help list\n");
	printf("-n, --dryrun       show but do not execute operations\n");
	printf("-p                 display numbers in exact values\n");
	printf("-v, --verbose      increase verbosity\n");
	printf("<mpool>  mpool name\n");
	printf("\n");
}

static int list_command(int argc, char **argv)
{
	struct option longopts[] = {
		{ "parsable",	no_argument,	NULL, 'p' },
		COMOPTS_OPTIONS,
		{ NULL }
	};

	bool    parsable = false;
	char   *optstring;

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: mkoptstring failed: out of memory", __func__);
		exit(EX_OSERR);
	}

	optind = 1;

	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'h':
			list_help(argc, argv);
			free(optstring);
			exit(0);

		case 'p':
			parsable = true;
			break;

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	argc -= optind;
	argv += optind;

	if (1) {
		struct mpioc_prop  *propv_base, *prop;
		struct mpioc_list   ls;

		size_t  propmax = 1024;
		int     labwidth = 6;
		int     mpwidth = 5;
		char   *mpname;
		int     fd, rc;
		int     i, j;

		propv_base = calloc(propmax, sizeof(*propv_base));
		if (!propv_base) {
			fprintf(stderr, "%s: calloc(propv) failed\n", progname);
			exit(EX_OSERR);
		}

		memset(&ls, 0, sizeof(ls));
		ls.ls_listv = propv_base;
		ls.ls_listc = propmax;
		ls.ls_cmd = MPIOC_LIST_CMD_PROP_LIST;

		fd = open(MPC_DEV_CTLPATH, O_RDONLY);
		if (-1 == fd) {
			eprint("cannot open mpool control device %s: %s\n",
			       MPC_DEV_CTLPATH, strerror(errno));
			exit(EX_NOINPUT);
		}

		rc = ioctl(fd, MPIOC_PROP_GET, &ls);
		if (rc) {
			char errbuf[256];

			mpool_strinfo(merr(errno), errbuf, sizeof(errbuf));
			eprint("list failed: %s\n", errbuf);
			exit(EX_DATAERR);
		}

		close(fd);

		for (i = 0; i < ls.ls_listc; ++i) {
			bool    match = true;
			size_t  len;

			prop = propv_base + i;
			mpname = prop->pr_xprops.ppx_params.mp_name;

			for (j = 0; j < argc; ++j) {
				match = !strcmp(argv[j], mpname);
				if (match)
					break;
			}

			if (match) {
				len = strlen(mpname);
				if (len > mpwidth)
					mpwidth = len;

				len = strlen(prop->pr_xprops.ppx_params.mp_label);
				if (len >= labwidth)
					labwidth = len + 1;
			} else {
				*mpname = '\000';
			}
		}

		for (i = 0; i < ls.ls_listc; ++i) {
			const char suffixtab[] = "\0kmgtpezy";
			double total, used, usable, free;
			char totalstr[32], usedstr[32];
			char usablestr[32], freestr[32];
			char capstr[32];
			double capacity = 9999;
			const char *stp;
			char *fmt;
			int width;

			prop = propv_base + i;
			mpname = prop->pr_xprops.ppx_params.mp_name;

			if (!mpname[0])
				continue;

			if (headers) {
				width = parsable ? 16 : 7;
				headers = false;

				printf("%-*s %*s %*s %*s %9s %*s %9s\n",
				       mpwidth, "MPOOL", width, "TOTAL", width, "USED",
				       width, "AVAIL", "CAPACITY", labwidth, "LABEL", "HEALTH");
			}

			width = parsable ? 16 : 7;

			stp = suffixtab;
			total = prop->pr_usage.mpu_total;
			while (!parsable && total >= 1024) {
				total /= 1024;
				++stp;
			}
			fmt = (total < 10) ? "%.2lf%c" : "%4.0lf%c";
			snprintf(totalstr, sizeof(totalstr), fmt, total, *stp);

			stp = suffixtab;
			usable = prop->pr_usage.mpu_usable;
			while (!parsable && usable >= 1024) {
				usable /= 1024;
				++stp;
			}
			fmt = (usable < 10) ? "%.2lf%c" : "%4.0lf%c";
			snprintf(usablestr, sizeof(usablestr), fmt, usable, *stp);

			stp = suffixtab;
			used = prop->pr_usage.mpu_used;
			while (!parsable && used >= 1024) {
				used /= 1024;
				++stp;
			}
			fmt = (used < 10) ? "%.2lf%c" : "%4.0lf%c";
			snprintf(usedstr, sizeof(usedstr), fmt, used, *stp);

			stp = suffixtab;
			free = prop->pr_usage.mpu_usable -
				prop->pr_usage.mpu_used;
			while (!parsable && free >= 1024) {
				free /= 1024;
				++stp;
			}
			fmt = (free < 10) ? "%.2lf%c" : "%4.0lf%c";
			snprintf(freestr, sizeof(freestr), fmt, free, *stp);

			if (prop->pr_usage.mpu_total > 0) {
				capacity = prop->pr_usage.mpu_used * 100;
				capacity /= prop->pr_usage.mpu_usable;
			}
			snprintf(capstr, sizeof(capstr), "%.2lf%c",
				 capacity, parsable ? '\0' : '%');

			printf("%-*s %*s %*s %*s %9s %*s %9s\n",
			       mpwidth, mpname, width, totalstr, width, usedstr, width, freestr,
			       capstr, labwidth, prop->pr_xprops.ppx_params.mp_label,
			       ENUM_VAL2NAME(mpool_status, prop->pr_xprops.ppx_params.mp_stat));
		}

		free(propv_base);
	}

	return 0;
}

static void mb_dump(struct mblock_props *props)
{
	char    name[32];

	snprintf(name, sizeof(name), "0x%08lx", props->mpr_objid);

	if (headers)
		printf("%*s  PROPERTY    VALUE\n", (int)strlen(name), "MBID");

	printf("%s  objid        0x%lx\n", name, props->mpr_objid);
	printf("%s  alloc_cap    %u\n", name, props->mpr_alloc_cap);
	printf("%s  write_len    %u\n", name, props->mpr_write_len);
	printf("%s  optimal_wrsz %u\n", name, props->mpr_optimal_wrsz);
	printf("%s  mclassp      %s\n", name, ENUM_VAL2NAME(mp_media_classp, props->mpr_mclassp));
	printf("%s  committed    %u\n", name, props->mpr_iscommitted);
}

static void mb_help(int argc, char **argv)
{
	printf("\n");
	printf("usage: %s %s [options] <mpool> <objid>...\n", progname, argv[0]);

	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("-h, --help            print this help list\n");
	printf("-n, --dryrun          show but do not execute operations\n");
	printf("-c, --capacity mscap  specify mblock minimum capacity\n");
	printf("-v, --verbose         increase verbosity\n");
	printf("<mpool>  mpool name\n");
	printf("<objid>  mblock ID\n");
	printf("\n");
}

static void mballoc_help(int argc, char **argv)
{
	printf("\n");
	printf("usage: %s %s [options] <mpool> [<count>]\n", progname, argv[0]);

	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("-h, --help            print this help list\n");
	printf("-n, --dryrun          show but do not execute operations\n");
	printf("-c, --capacity mscap  specify mblock minimum capacity\n");
	printf("-v, --verbose         increase verbosity\n");
	printf("<mpool>  mpool name\n");
	printf("<count>  number of mblock to allocate (default: 1)\n");
	printf("\n");
}

static int mb_command(int argc, char **argv)
{
	struct option   longopts[] = {
		COMOPTS_OPTIONS,
		{ NULL }
	};

	struct mpool   *mp;

	char    errbuf[128];
	char   *mpname;
	char   *subcmd;
	char   *optstring;
	mpool_err_t  err;

	subcmd = argv[0];

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: mkoptstring failed: out of memory", __func__);
		exit(EX_OSERR);
	}

	optind = 1;

	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'h':
			if (strcmp(subcmd, "mballoc"))
				mb_help(argc, argv);
			else
				mballoc_help(argc, argv);
			free(optstring);
			exit(0);

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		syntax(fmt_insufficient_arguments);
		exit(EX_USAGE);
	}

	mpname = argv[0];
	--argc;
	++argv;

	err = mpool_open(mpname, O_RDWR, &mp, NULL);
	if (err) {
		eprint("mpool_open(%s) failed: %s",
		       mpname, mpool_strinfo(err, errbuf, sizeof(errbuf)));
		exit(EX_NOINPUT);
	}

	if (0 == strcmp(subcmd, "mballoc")) {

		char       *sep = "";
		uint        count;

		if (argc > 1)
			syntax("extraneous arguments ignored");

		count = (argc > 0) ? strtoul(argv[0], NULL, 0) : 1;

		while (count-- > 0) {
			struct mblock_props     props;
			uint64_t                mbh;

			err = mpool_mblock_alloc(mp, MP_MED_CAPACITY, false, &mbh, &props);
			if (err) {
				eprint("%s failed: %s", subcmd,
				       mpool_strinfo(err, errbuf, sizeof(errbuf)));
				exit(EX_NOINPUT);
			}

			if (verbosity > 0) {
				ulong maxcap;

				maxcap = props.mpr_alloc_cap;

				if (headers) {
					printf("%12s %10s\n", "MBID", "CAPACITY");
					headers = false;
				}

				printf("%#12lx %10lu\n", props.mpr_objid, maxcap);
			} else {
				printf("%s0x%lx", sep, props.mpr_objid);
				sep = " ";
			}
		}

		printf("%s", *sep ? "\n" : "");

	} else if (0 == strcmp(subcmd, "mblookup")) {
		uint64_t    mbh;
		int         i;

		if (argc < 1) {
			syntax(fmt_insufficient_arguments);
			exit(EX_USAGE);
		}

		for (i = 0; i < argc; ++i) {
			struct mblock_props props;

			mbh = strtoul(argv[i], NULL, 0);

			err = mpool_mblock_props_get(mp, mbh, &props);
			if (err) {
				eprint("%s 0x%lx failed: %s", subcmd, mbh,
				       mpool_strinfo(err, errbuf, sizeof(errbuf)));
				continue;
			}

			if (verbosity > 0) {
				mb_dump(&props);
				continue;
			}

			if (headers) {
				printf("%12s %10s\n", "MBID", "CAPACITY");
				headers = false;
			}

			printf("%#12lx %10u\n", props.mpr_objid, props.mpr_alloc_cap);
		}
	} else if (0 == strcmp(subcmd, "mbcommit") ||
		   0 == strcmp(subcmd, "mbdelete") ||
		   0 == strcmp(subcmd, "mbabort")) {

		uint64_t    mbh;
		int         i;

		if (argc < 1) {
			syntax(fmt_insufficient_arguments);
			exit(EX_USAGE);
		}

		for (i = 0; i < argc; ++i) {
			mbh = strtoul(argv[i], NULL, 0);

			if (subcmd[2] == 'd')
				err = mpool_mblock_delete(mp, mbh);
			else if (subcmd[2] == 'a')
				err = mpool_mblock_abort(mp, mbh);
			else
				err = mpool_mblock_commit(mp, mbh);

			if (err) {
				eprint("%s 0x%lx failed: %s", subcmd, mbh,
				       mpool_strinfo(err, errbuf, sizeof(errbuf)));
			}
		}
	} else {
		syntax("invalid subcommand %s", subcmd);
		exit(EX_USAGE);
	}

	mpool_close(mp);

	return 0;
}

static void mbrw_help(int argc, char **argv)
{
	printf("\n");
	printf("usage: %s %s [options] <mpool> <objid> ...\n", progname, argv[0]);

	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("-f, --iofile <name>  specify the input/output file name\n");
	printf("-h, --help           print this help list\n");
	printf("-l, --length <len>   specify the max bytes to r/w\n");
	printf("-n, --dryrun         show but do not execute operations\n");
	printf("-o, --offset <off>   specify the starting offset (in bytes)\n");
	printf("-v, --verbose        increase verbosity\n");
	printf("<mpool>  mpool name\n");
	printf("<objid>  mblock ID\n");
	printf("\n");
}

static int mbrw_command(int argc, char **argv)
{
	struct option   longopts[] = {
		{ "iofile",	required_argument,	NULL, 'f' },
		{ "length",	required_argument,	NULL, 'l' },
		{ "offset",	required_argument,	NULL, 'o' },
		COMOPTS_OPTIONS,
		{ NULL }
	};

	struct mpool   *mp;

	char    errbuf[128];
	char   *iofile_path;
	size_t  rw_length;
	off_t   rw_offset;
	char   *mpname;
	char   *subcmd;
	char   *optstring;
	size_t  bufsz;
	char   *buf;
	char   *end;
	mpool_err_t  err;
	int     i;

	subcmd = argv[0];

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: mkoptstring failed: out of memory", __func__);
		exit(EX_OSERR);
	}

	iofile_path = NULL;
	rw_length = -1;
	rw_offset = 0;
	optind = 1;

	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'f':
			iofile_path = optarg;
			break;

		case 'l':
			/* TODO: error checking... */
			rw_length = strtoul(optarg, &end, 0);
			break;

		case 'o':
			/* TODO: error checking... */
			rw_offset = strtoul(optarg, &end, 0);
			break;

		case 'h':
			mbrw_help(argc, argv);
			free(optstring);
			exit(0);

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		syntax(fmt_insufficient_arguments);
		exit(EX_USAGE);
	}

	mpname = argv[0];
	--argc;
	++argv;

	err = mpool_open(mpname, O_RDWR, &mp, NULL);
	if (err) {
		eprint("mpool_open(%s) failed: %s",
		       mpname, mpool_strinfo(err, errbuf, sizeof(errbuf)));
		exit(EX_NOINPUT);
	}

	rw_offset &= ~(PAGE_SIZE - 1);
	rw_length &= ~(PAGE_SIZE - 1);

	bufsz = 1024 * 1024;
	buf = aligned_alloc(PAGE_SIZE, bufsz);
	if (!buf) {
		eprint("%s malloc(%zu) failed", subcmd, bufsz);
		exit(EX_OSERR);
	}

	/* mpool currently doesn't return a short count for r/w requests
	 * that extend beyond EOF (where "EOF" means valid data in the
	 * mblock).  So we try to r/w as much as possible per iteration,
	 * halving our r/w request size when we detect a failure until
	 * our r/w size reaches the minimum (currently PAGE_SIZE).
	 */
	if (0 == strcmp(subcmd, "mbread")) {
		struct mblock_props props;
		int                 ofile_fd;

		if (argc < 1) {
			syntax(fmt_insufficient_arguments);
			exit(EX_USAGE);
		}

		ofile_fd = fileno(stdout);
		if (iofile_path) {
			ofile_fd = open(iofile_path, O_CREAT | O_TRUNC | O_WRONLY, 0400);
			if (-1 == ofile_fd) {
				eprint("unable to open output file %s: %s",
				       iofile_path, strerror(errno));
				exit(EX_NOINPUT);
			}
		}

		for (i = 0; i < argc; ++i) {
			off_t   off, offmax;
			size_t  wmax;
			u64     mbh;

			mbh = strtoul(argv[i], NULL, 0);

			err = mpool_mblock_props_get(mp, mbh, &props);
			if (err) {
				eprint("%s mp_mb_lookup(0x%lx) failed: %s", subcmd, mbh,
				       mpool_strinfo(err, errbuf, sizeof(errbuf)));
				continue;
			}

			offmax = min(props.mpr_alloc_cap, (u32)(rw_offset + rw_length));
			wmax = bufsz;
			off = rw_offset;

			while (off < offmax) {
				struct iovec    iov[2];
				ssize_t         cc;

				if (off + wmax > offmax)
					wmax = PAGE_SIZE;
				if (off + wmax > offmax)
					break;

				iov[0].iov_base = buf;
				iov[0].iov_len = wmax;

				err = mpool_mblock_read(mp, mbh, iov, 1, off);

				if (mpool_errno(err) == EINVAL) {
					if (wmax > PAGE_SIZE) {
						wmax /= 2;
						wmax = roundup(wmax, PAGE_SIZE);
						continue;
					} else if (off > rw_offset) {

						/* We had a least one successful
						 * read, so bail out w/o error.
						 */
						goto err_ok;
					}
				}

				if (err) {
					eprint("%s mpool_mblock_read(0x%lx) failed: %s",
					       subcmd, mbh,
					       mpool_strinfo(err, errbuf, sizeof(errbuf)));
					exit(EX_OSERR);
				}

				cc = write(ofile_fd, buf, wmax);
				if (-1 == cc) {
					eprint("%s 0x%lx: write failed: %s",
					       subcmd, mbh, strerror(errno));
					exit(EX_OSERR);
				}

				off += cc;
			}

err_ok:
			if (iofile_path)
				close(ofile_fd);
		}
	} else if (0 == strcmp(subcmd, "mbwrite")) {
		struct mblock_props props;
		int                 ifile_fd;

		if (argc < 1) {
			syntax(fmt_insufficient_arguments);
			exit(EX_USAGE);
		}

		ifile_fd = fileno(stdout);
		if (iofile_path) {
			ifile_fd = open(iofile_path, O_RDONLY);
			if (-1 == ifile_fd) {
				eprint("unable to open input file %s: %s",
				       iofile_path, strerror(errno));
				exit(EX_NOINPUT);
			}
		}

		for (i = 0; i < argc; ++i) {
			off_t   off, offmax;
			size_t  wmax;
			u64     mbh;

			mbh = strtoul(argv[i], NULL, 0);

			err = mpool_mblock_props_get(mp, mbh, &props);
			if (err) {
				eprint("%s mp_mb_lookup(0x%lx) failed: %s", subcmd, mbh,
				       mpool_strinfo(err, errbuf, sizeof(errbuf)));
				continue;
			}

			offmax = min(props.mpr_alloc_cap, (u32)(rw_offset + rw_length));
			wmax = bufsz;
			off = 0;

			while (off < offmax) {
				struct iovec    iov[2];
				ssize_t         cc;

				if (off + wmax > offmax)
					wmax = PAGE_SIZE;
				if (off + wmax > offmax)
					break;

				cc = read(ifile_fd, buf, wmax);
				if (cc == -1) {
					eprint("%s read %s failed: %s", subcmd, iofile_path,
					       strerror(errno));
					exit(EX_OSERR);
				} else if (cc == 0) {
					break;
				}

				iov[0].iov_base = buf;
				iov[0].iov_len = cc;

				err = mpool_mblock_write(mp, mbh, iov, 1);
				if (err) {
					eprint("%s mpool_mblock_write(0x%lx) failed: %s",
					       subcmd, mbh,
					       mpool_strinfo(err, errbuf, sizeof(errbuf)));
					exit(EX_OSERR);
				}

				off += cc;
			}
		}

		if (iofile_path)
			close(ifile_fd);
	} else {
		syntax("invalid subcommand %s", subcmd);
		exit(EX_USAGE);
	}

	mpool_close(mp);
	free(buf);

	return 0;
}

static void mmrd_help(int argc, char **argv)
{
	printf("\n");
	printf("usage: %s %s [options] <mpool> <objid> ...\n", progname, argv[0]);

	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("-f, --iofile <name>  specify the input/output file name\n");
	printf("-h, --help           print this help list\n");
	printf("-l, --length <len>   specify the max bytes to r/w\n");
	printf("-n, --dryrun         show but do not execute operations\n");
	printf("-o, --offset <off>   specify the starting offset (in bytes)\n");
	printf("-v, --verbose        increase verbosity\n");
	printf("<mpool>  mpool name\n");
	printf("<objid>  mblock ID\n");
	printf("\n");
}

static int mmrd_command(int argc, char **argv)
{
	struct option   longopts[] = {
		{ "iofile",	required_argument,	NULL, 'f' },
		{ "length",	required_argument,	NULL, 'l' },
		{ "offset",	required_argument,	NULL, 'o' },
		COMOPTS_OPTIONS,
		{ NULL }
	};

	struct mpool   *mp;

	char    errbuf[128];
	char   *iofile_path;
	size_t  rw_length;
	off_t   rw_offset;
	char   *mpname;
	char   *subcmd;
	char   *optstring;
	size_t  bufsz;
	char   *buf;
	char   *end;
	mpool_err_t  err;
	int     i;

	subcmd = argv[0];

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: mkoptstring failed: out of memory", __func__);
		exit(EX_OSERR);
	}

	iofile_path = NULL;
	rw_length = -1;
	rw_offset = 0;
	optind = 1;

	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'f':
			iofile_path = optarg;
			break;

		case 'l':
			/* TODO: error checking... */
			rw_length = strtoul(optarg, &end, 0);
			break;

		case 'o':
			/* TODO: error checking... */
			rw_offset = strtoul(optarg, &end, 0);
			break;

		case 'h':
			mbrw_help(argc, argv);
			free(optstring);
			exit(0);

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		syntax(fmt_insufficient_arguments);
		exit(EX_USAGE);
	}

	mpname = argv[0];
	--argc;
	++argv;

	err = mpool_open(mpname, O_RDWR, &mp, NULL);
	if (err) {
		eprint("mpool_open(%s) failed: %s",
		       mpname, mpool_strinfo(err, errbuf, sizeof(errbuf)));
		exit(EX_NOINPUT);
	}

	rw_offset &= ~(PAGE_SIZE - 1);
	rw_length &= ~(PAGE_SIZE - 1);

	bufsz = 1024 * 1024;
	buf = aligned_alloc(PAGE_SIZE, bufsz);
	if (!buf) {
		eprint("%s malloc(%zu) failed", subcmd, bufsz);
		exit(EX_OSERR);
	}

	/* mpool currently doesn't return a short count for r/w requests
	 * that extend beyond EOF (where "EOF" means valid data in the
	 * mblock).  So we try to r/w as much as possible per iteration,
	 * halving our r/w request size when we detect a failure until
	 * our r/w size reaches the minimum (currently PAGE_SIZE).
	 */
	if (0 == strcmp(subcmd, "mmread")) {
		struct mpool_mcache_map   *map;

		u64    *mbidv;
		u64    *mblenv;
		int     fd;

		if (argc < 1) {
			syntax(fmt_insufficient_arguments);
			exit(EX_USAGE);
		}

		fd = fileno(stdout);
		if (iofile_path) {
			fd = open(iofile_path, O_CREAT | O_TRUNC | O_WRONLY, 0400);
			if (-1 == fd) {
				eprint("unable to open output file %s: %s",
				       iofile_path, strerror(errno));
				exit(EX_NOINPUT);
			}
		}

		mbidv = malloc(argc * sizeof(mbidv[0]) * 2);
		if (!mbidv) {
			eprint("unable to alloc mbidv: %s", strerror(errno));
			exit(EX_OSERR);
		}

		mblenv = mbidv + argc;

		for (i = 0; i < argc; ++i) {
			struct mblock_props props;

			mbidv[i] = strtoul(argv[i], NULL, 0);

			err = mpool_mblock_find(mp, mbidv[i], &props);
			if (err) {
				eprint("mpool_mblock_find(%lx): %s", mbidv[i],
				       mpool_strinfo(err, errbuf, sizeof(errbuf)));
				exit(EX_DATAERR);
			}

			mblenv[i] = props.mpr_write_len;
		}

		err = mpool_mcache_mmap(mp, argc, mbidv, MPC_VMA_WARM, &map);
		if (err) {
			eprint("mpool_mcache_mmap failed: %s",
			       mpool_strinfo(err, errbuf, sizeof(errbuf)));
			exit(EX_OSERR);
		}

		for (i = 0; i < argc; ++i) {
			ssize_t cc;
			void *mem;

			err = mpool_mcache_madvise(map, i, 0, mblenv[i], MADV_WILLNEED);
			if (err) {
				eprint("mpool_mcache_madvise(%d, %lu): %s", i, mbidv[i],
				       mpool_strinfo(err, errbuf, sizeof(errbuf)));
			}

			mem = mpool_mcache_getbase(map, i);

			cc = write(fd, mem, mblenv[i]);
			if (cc != mblenv[i]) {
				eprint("i %d, write cc %ld != mblen %lu", i, cc, (ulong)mblenv[i]);
				exit(EX_OSERR);
			}
		}

		if (iofile_path)
			close(fd);

		err = mpool_mcache_munmap(map);
		if (err) {
			eprint("mpool_mcache_munmap failed: %s",
			       mpool_strinfo(err, errbuf, sizeof(errbuf)));
			exit(EX_OSERR);
		}

		free(mbidv);
	} else {
		syntax("invalid subcommand %s", subcmd);
		exit(EX_USAGE);
	}

	mpool_close(mp);
	free(buf);

	return 0;
}

static void main_help(int argc, char **argv)
{
	int i;

	printf("\n");
	printf("usage: %s <command> [options] [args...]\n", progname);
	printf("usage: %s -h\n", progname);
	printf("usage: %s -V\n", progname);
	printf("-H, --no-headers  suppress column headers\n");
	printf("-h, --help        print this help list\n");
	printf("-n, --dryrun      show but do not execute operations\n");
	printf("-V, --version     show version\n");
	printf("-v, --verbose     increase verbosity\n");
	printf("<command>  a command to execute (see below)\n");
	printf("\n");

	printf("The %s command creates, modifies, and manages media pools.\n", progname);

	printf("\nCommands:\n");
	for (i = 0; mpool_cmds[i].cmd; i++)
		printf("  %-10s  %s\n", mpool_cmds[i].cmd, mpool_cmds[i].synopsis);

	printf("\nFor help on a specific %s command:\n", progname);
	printf("  %s help <command>\n", progname);
	printf("  %s <command> -h\n", progname);
	printf("\n");

	if (verbosity < 1)
		return;

	printf("%8s  %s\n", "SIZE", "NAME");
	printf("%8zu  mpioc_union\n", sizeof(union mpioc_union));
	printf("%8zu  mpioc_mpool\n", sizeof(struct mpioc_mpool));
	printf("%8zu  mpioc_params\n", sizeof(struct mpioc_params));
	printf("%8zu  mpioc_drive\n", sizeof(struct mpioc_drive));
	printf("%8zu  mpioc_mblock\n", sizeof(struct mpioc_mblock));
	printf("%8zu  mpioc_mlog\n", sizeof(struct mpioc_mlog));
	printf("%8zu  mpioc_prop\n", sizeof(struct mpioc_prop));
	printf("%8zu  mpool_xprops\n", sizeof(struct mpool_xprops));
	printf("%8zu  mpool_mclass_xprops\n", sizeof(struct mpool_mclass_xprops));
	printf("%8zu  mpool_usage\n", sizeof(struct mpool_usage));
}

static int help_command(int argc, char **argv)
{
	int i;

	/* Is there a command after "help" on the command line?
	 */
	if (argc > 1) {
		for (i = 0; mpool_cmds[i].cmd; i++) {
			if (0 == strcmp(argv[1], mpool_cmds[i].cmd)) {
				mpool_cmds[i].help(argc - 1, argv + 1);
				return 0;
			}
		}

		syntax("invalid command %s", argv[1]);
		return EX_USAGE;
	}

	main_help(argc, argv);

	return 0;
}

int main(int argc, char **argv)
{
	struct option longopts[] = {
		COMOPTS_OPTIONS,
		{ NULL }
	};

	char   *optstring;
	int     i;

	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];

	optstring = mkoptstring(longopts);
	if (!optstring) {
		eprint("%s: mkoptstring failed: out of memory", __func__);
		exit(EX_OSERR);
	}

	/* Process global options, if any...
	 */
	while (1) {
		int curind = optind;
		int idx = 0;
		int c;

		c = getopt_long(argc, argv, optstring, longopts, &idx);
		if (-1 == c)
			break; /* got '--' or end of arg list */

		switch (c) {
		case 'h':
			main_help(argc, argv);
			free(optstring);
			return 0;

		default:
			comopts_handler(c, optarg, argv, curind, longopts, idx);
			break;
		}
	}

	free(optstring);

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		syntax(fmt_insufficient_arguments);
		exit(EX_USAGE);
	}

	for (i = 0; mpool_cmds[i].cmd; i++) {
		if (0 == strcmp(argv[0], mpool_cmds[i].cmd))
			return mpool_cmds[i].run(argc, argv);
	}

	syntax("invalid command %s", argv[0]);
	exit(EX_USAGE);
}


static const struct mpool_cmd mpool_cmds[] = {
	{ "create",	"create an mpool", create_command, create_help },
	{ "destroy",	"destroy an mpool", destroy_command, destroy_help },
	{ "activate",	"activate an mpool", create_command, create_help },
	{ "deactivate",	"deactivate an mpool", destroy_command, destroy_help },
	{ "list",	"list one more more mpools", list_command, list_help },
	{ "get",	"retrieve and show properties", get_command, get_help },
	{ "set",	"set properties", set_command, set_help },
	{ "help",	"show detailed usage", help_command, main_help },
	{ "mbabort",	"abort an mblock", mb_command, mb_help },
	{ "mballoc",	"allocate an mblock", mb_command, mballoc_help },
	{ "mbcommit",	"commit an mblock", mb_command, mb_help },
	{ "mbdelete",	"delete an mblock", mb_command, mb_help },
	{ "mblookup",	"look up an mblock", mb_command, mb_help },
	{ "mbread",	"read an mblock", mbrw_command, mbrw_help },
	{ "mbwrite",	"write an mblock", mbrw_command, mbrw_help },
	{ "mmread",	"read an mblock via mmap", mmrd_command, mmrd_help },
	{ NULL }
};
