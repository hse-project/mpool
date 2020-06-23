// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <util/platform.h>
#include <util/printbuf.h>

#include <mpool/mpool.h>

#include <util/param.h>

#include <mpool.h>
#include <mpool_version.h>

#include <sysexits.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>
#include <getopt.h>

const struct xoption
xoptionv[] = {
	{ 'a', "activate",    "d", "Activate all mpools", &co.co_activate, },
	{ 'D', "discard",    NULL, "Issue TRIM/DISCARD", &co.co_discard, },
	{ 'd', "deactivate",  "a", "Deactivate all mpools", &co.co_deactivate, },
	{ 'f', "force",      NULL, "Override safeguards", &co.co_force, },
	{ 'H', "noheadings", NULL, "Suppress headers", &co.co_noheadings, },
	{ 'h', "help",       NULL, "Show this help list", &co.co_help, },
	{ 'L', "log",        NULL, "Output to log file", &co.co_log, .opthidden = true, },
	{ 'N', "noresolve",  NULL, "Do not resolve uid/gid names", &co.co_noresolve, },
	{ 'n', "dry-run",    NULL, "dry run", &co.co_dry_run, },
	{ 'p', "nosuffix",   NULL, "Print numbers in machine readable format", &co.co_nosuffix, },
	{ 'r', "resize",     NULL, "Resize mpool", &co.co_resize, },
	{ 'T', "mutest",     NULL, "Enable mutest mode", &co.co_mutest, .opthidden = true, },
	{ 'v', "verbose",    NULL, "Increase verbosity", &co.co_verbose, },
	{ 'Y', "yaml",       NULL, "Output in yaml", &co.co_yaml, },
	{ -1 },
};

const char *progname;

struct verb_s *find_verb(struct subject_s *s, char *verb)
{
	struct verb_s  *partial = NULL;
	struct verb_s  *v;

	for (v = s->verb; v && v->name; ++v) {
		if (v->hidden && !co.co_mutest)
			continue;

		if (!strcmp(verb, v->name))
			return v; /* exact match */

		if (!strncmp(verb, v->name, strlen(verb))) {
			if (partial)
				break;

			partial = v; /* partial match */
		}
	}

	if (v && v->name && partial) {
		fprintf(co.co_fp, "%s: ambiguous command `%s' (%s or %s), use -h for help\n",
			progname, verb, v->name, partial->name);
		exit(EX_USAGE);
	}

	return partial;
}

mpool_err_t process_verb(struct subject_s *subject, int argc, char **argv)
{
	struct subject_s   *s = subject;
	struct verb_s      *v = NULL;

	if (argc < 1) {
		s->usage();
		s->help(MPOOL_VERBOSE);

		for (v = s->verb; v && v->name; ++v)
			if (!v->hidden || co.co_mutest)
				v->help(v, MPOOL_TERSE);

		fprintf(co.co_fp, "\n\nUse '%s <command> -h' for detailed help.\n\n", progname);

		return 0;
	}

	v = find_verb(s, argv[0]);
	if (!v) {
		fprintf(co.co_fp, "%s: invalid command '%s', use -h for help\n", progname, argv[0]);
		return EX_USAGE;
	}

	if (!v->xoption)
		v->xoption = xoptionv;

	if (xgetopt(argc, argv, v->optstring, v->xoption))
		return EX_USAGE;

	if (co.co_help && v->help) {
		v->help(v, MPOOL_VERBOSE);
		return 0;
	}

	return v->func(v, argc - optind, argv + optind);
}

int
main(int argc, char **argv)
{
	mpool_err_t err;

	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];

	if (xgetopt(argc, argv, "+hTv", xoptionv))
		return EX_USAGE;

	show_advanced_params = !!co.co_mutest;

	err = process_verb(&mpool_ui, argc - optind, argv + optind);

	return err ? -1 : 0;
}
