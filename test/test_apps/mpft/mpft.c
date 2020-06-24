// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <util/platform.h>
#include <util/string.h>
#include <util/param.h>
#include <util/printbuf.h>

#include "mpft.h"
#include "mpft_mlog.h"
#include "mpft_mblock.h"
#include "mpft_mdc.h"
#include "mpft_mp.h"

#include <stdarg.h>
#include <sysexits.h>
#include <sys/wait.h>

#define merr(_errnum)   (_errnum)

static const struct xoption
xoptionv[] = {
	{ 'h', "help",    NULL, "Show this help list", &co.co_help, },
	{ 'L', "log",     NULL, "Output to log file",  &co.co_log, },
	{ 'n', "dry-run", NULL, "dry run",             &co.co_dry_run, },
	{ 'T', "mutest",  NULL, "Enable test mode",    &co.co_mutest, },
	{ 'v', "verbose", NULL, "Increase verbosity",  &co.co_verbose, },
	{ -1 },
};

struct results_s {
	int total;
	int passed;
	int failed;
};

static struct group_s *m_group[] = {
	&mpft_mblock,
	&mpft_mlog,
	&mpft_mdc,
	&mpft_mp,
	NULL
};

void list_groups(struct group_s **m_group)
{
	struct group_s   *g;
	int               i = 0;

	while ((g = m_group[i])) {
		fprintf(co.co_fp, "  %-8s  %s\n", g->group_name, "");
		i++;
	};

	fprintf(co.co_fp, "  %-8s  %s\n", "all", "run all the above tests");
}

void usage(char *progname)
{
	fprintf(co.co_fp, "usage: %s [options] [group]\n", progname);

	xgetopt_usage("hLhnTv", xoptionv);

	fprintf(co.co_fp, "\nGroups:\n");
	list_groups(m_group);
}

/**
 * find_group() assumes that the passed-in group is a null-terminated string.
 */
struct group_s *find_group(struct group_s **group, char *grp)
{
	struct group_s    *g, *best_fit = NULL;
	int                i = 0;

	if (!grp)
		return NULL;

	while ((g = group[i])) {
		if (!strcasecmp(grp, g->group_name))
			return g; /* exact match */

		/* look for partial match */
		if (!strncasecmp(grp, g->group_name, strlen(grp))) {
			/* fail if there already was one */
			if (best_fit) {
				fprintf(stderr, "Multiple groups match %s\n", grp);
				return NULL;
			}
			best_fit = g;
		}
		i++;
	}
	if (best_fit)
		g = best_fit;

	return g;
}

void help_groups(struct group_s **group)
{
	int     i = 0;

	fprintf(co.co_fp, "No valid group found, possible groups:\n");

	while (group[i]) {
		fprintf(co.co_fp, "\t%s\n", group[i]->group_name);
		i++;
	}
}

struct tt_match {
	char                  *type_name;
	enum mpft_test_type    test_type;
};

static
struct tt_match test_types[] = {
	{ "stress",      MPFT_TEST_TYPE_STRESS },
	{ "perf",        MPFT_TEST_TYPE_PERF },
	{ "correctness", MPFT_TEST_TYPE_CORRECTNESS },
	{ "compound",    MPFT_TEST_TYPE_COMPOUND },
	{ "actor",       MPFT_TEST_TYPE_ACTOR },
	{ NULL,          MPFT_TEST_TYPE_INVALID },
};

enum mpft_test_type find_type(char *type)
{
	struct tt_match *tt = test_types;
	struct tt_match *best_fit = NULL;

	if (!type)
		return MPFT_TEST_TYPE_INVALID;

	while (tt && tt->type_name) {
		if (!strcasecmp(type, tt->type_name))
			return tt->test_type; /* exact match */

		/* look for partial match */
		if (!strncasecmp(type, tt->type_name, strlen(type))) {
			/* fail if there already was one */
			if (best_fit) {
				fprintf(stderr, "Multiple test types match %s\n", type);
				return MPFT_TEST_TYPE_INVALID;
			}
			best_fit = tt;
		}
		tt++;
	}

	if (best_fit)
		return best_fit->test_type;

	return MPFT_TEST_TYPE_INVALID;
}

static mpool_err_t show_type(char *str, size_t strsz, enum mpft_test_type test_type)
{
	struct tt_match *tt = test_types;

	while (tt && tt->type_name) {
		if (test_type == tt->test_type) {
			strlcpy(str, tt->type_name, strsz);
			return 0;
		}
		tt++;
	}
	snprintf(str, strsz, "Invalid");
	return 0;
}

struct test_s *find_test(struct group_s *g, char *test)
{
	struct test_s *t = g->group_test;
	struct test_s *best_fit = NULL;

	while (t && t->test_name) {
		if (!strcasecmp(test, t->test_name))
			return t; /* exact match */

		/* look for partial match */
		if (!strncasecmp(test, t->test_name, strlen(test))) {
			/* fail if there already was one */
			if (best_fit) {
				fprintf(stderr, "Multiple tests match %s\n", test);
				return NULL;
			}
			best_fit = t;
		}
		t++;
	}
	if (best_fit)
		t = best_fit;

	return t;
}

void help_tests(struct group_s *g)
{
	struct test_s  *t;

	fprintf(co.co_fp, "No valid test for group %s found, possible tests:\n", g->group_name);

	t = g->group_test;
	while (t && t->test_name) {
		fprintf(co.co_fp, "\t%s\n", t->test_name);
		t++;
	}
}

mpool_err_t
execute_test(
	struct results_s      *results,
	struct group_s        *g,
	enum mpft_test_type    test_type,
	struct test_s         *t,
	int                    argc,
	char                 **argv)
{
	char   test_name[256];
	char   ttype[20];
	char **save_args;
	int    i;
	mpool_err_t err;

	save_args = calloc(argc, sizeof(*save_args));
	for (i = 0; i < argc; i++)
		save_args[i] = argv[i];

	show_type(ttype, sizeof(ttype), test_type);
	snprintf(test_name, sizeof(test_name), "%s.%s.%s", g->group_name, ttype, t->test_name);
	argv[0] = test_name;

	fprintf(stdout, "Test %s\n", test_name);

	results->total++;

	err = t->test_func(argc, argv);
	if (err != 0) {
		results->failed++;
		fprintf(stdout, "\tTEST FAILED (%s)\n", test_name);
	} else {
		results->passed++;
		fprintf(stdout, "\tTEST PASSED (%s)\n", test_name);
	}


	for (i = 0; i < argc; i++)
		argv[i] = save_args[i];
	free(save_args);

	return err;
}

#define WILD "wild"

mpool_err_t
execute_group(
	struct results_s  *results,
	struct group_s    *g,
	char              *o_type,
	char              *o_test,
	int                argc,
	char             **argv)
{
	mpool_err_t err;

	/**
	 * Now we have <x> choices:
	 * 1. type = wild, test = wild
	 * 2. type = wild, test = <specific_test>
	 * 3. type = <specific_type>, test = wild
	 * 4. type = <specific_type>, test = <specific_test>
	 */
	if (!strcmp(o_type, WILD) && !strcmp(o_test, WILD)) {
		struct test_s   *t = g->group_test;

		if (co.co_help) {
			g->group_help();
			return 0;
		}
		while (t && t->test_name) {

			if (t->test_type == MPFT_TEST_TYPE_ACTOR) {
				t++;
				continue;
			}

			err = execute_test(results, g, t->test_type,
				t, argc, argv);
			if (err != 0)
				return err;
			t++;
		}
	} else if (!strcmp(o_type, WILD) && strcmp(o_test, WILD)) {
		struct test_s   *t = g->group_test;

		while (t && t->test_name) {

			if (t->test_type == MPFT_TEST_TYPE_ACTOR) {
				t++;
				continue;
			}

			if (co.co_help) {
				t->test_help();
			} else {
				if (!strcmp(t->test_name, o_test)) {
					err = execute_test(results, g, t->test_type, t, argc, argv);
					if (err != 0)
						return err;
				}
			}
			t++;
		}
	} else if (strcmp(o_type, WILD) && !strcmp(o_test, WILD)) {
		struct test_s   *t = g->group_test;
		enum mpft_test_type test_type = find_type(o_type);

		if (test_type == MPFT_TEST_TYPE_INVALID)
			return merr(EINVAL);

		while (t && t->test_name) {

			if (t->test_type == MPFT_TEST_TYPE_ACTOR) {
				t++;
				continue;
			}

			if (co.co_help) {
				t->test_help();
			} else {
				if (t->test_type == test_type) {
					err = execute_test(results, g, t->test_type, t, argc, argv);
					if (err != 0)
						return err;
				}
			}
			t++;
		}
	} else {
		struct test_s   *t = g->group_test;
		enum mpft_test_type test_type = find_type(o_type);

		if (test_type == MPFT_TEST_TYPE_INVALID)
			return merr(EINVAL);

		while (t && t->test_name) {

			/**
			 * Actors have to be called explicitly, i.e.
			 * don't call them if any part of path is
			 * wildcarded.
			 */
			if ((t->test_type == MPFT_TEST_TYPE_ACTOR) &&
			    (strcmp(t->test_name, o_test))) {
				t++;
				continue;
			}

			if ((t->test_type == test_type) && (!strcmp(t->test_name, o_test))) {
				if (co.co_help) {
					t->test_help();
				} else {
					err = execute_test(results, g, t->test_type, t, argc, argv);
					if (err != 0)
						return err;
				}
				break;
			}
			t++;
		}
	}
	return 0;
}

static u8 c_to_n(u8 c)
{
	u8 n = 255;

	if ((c >= '0') && ('9' >= c))
		n = c - '0';

	if ((c >= 'a') && ('f' >= c))
		n = c - 'a' + 0xa;

	if ((c >= 'A') && ('F' >= c))
		n = c - 'A' + 0xa;

	return n;
}

u8 *pattern;
u32 pattern_len;

int pattern_base(char *base)
{
	int i;
	int len = strlen(base);

	if (len == 0)
		pattern_len = 16;
	else
		pattern_len = len;

	pattern = malloc(pattern_len);
	if (pattern == NULL)
		return -1;

	if (len == 0) {	           /* No pattern given, so make one up */
		for (i = 0; i < pattern_len; i++)
			pattern[i] = i % 256;
	} else {
		for (i = 0; i < pattern_len; i++) {
			pattern[i] = c_to_n(base[i]);

			if (pattern[i] == 255) {
				free(pattern);
				pattern = NULL;
				return -1;
			}
		}
	}

	return 0;
}

void pattern_fill(char *buf, u32 buf_sz)
{
	u32 remaining = buf_sz;
	u32 idx;

	while (remaining > 0) {
		idx = buf_sz - remaining;
		buf[idx] = pattern[idx % pattern_len];
		remaining--;
	}
}

int pattern_compare(char *buf, u32 buf_sz)
{
	u32 remaining = buf_sz;
	u32 idx;

	while (remaining > 0) {
		idx = buf_sz - remaining;

		if (buf[idx] != pattern[idx % pattern_len])
			return -1;

		remaining--;
	}
	return 0;
}

#define LOG_MSG_SIZE 1024
void log_command_line(int argc, char **argv)
{
	char log_buf[LOG_MSG_SIZE];
	size_t offset = 0;
	int i;

	snprintf_append(log_buf, LOG_MSG_SIZE, &offset, "cmd:");
	for (i = 0; i < argc; i++)
		snprintf_append(log_buf, LOG_MSG_SIZE, &offset, "%s ", argv[i]);
}

static char *executable_name;

mpool_err_t mpft_launch_actor(char *actor, ...)
{
	va_list argp;
	char *arg;
	int argc = 0;
	char **argv;
	int max_len;
	int i;
	pid_t pid;

	max_len = strlen(executable_name);

	va_start(argp, actor);
	while ((arg = va_arg(argp, char *))) {
		argc++;
		if (strlen(arg) > max_len)
			max_len = strlen(arg);
	}
	va_end(argp);

	/* +1 for executable name */
	argc++;

	/* +1 for actor name */
	argc++;

	pid = fork();
	if (pid) {		/* Parent */
		int status;

		waitpid(pid, &status, 0);

		if ((WIFEXITED(status) && !WEXITSTATUS(status)))
			return 0;
		else
			return merr(WEXITSTATUS(status));

	} else {		/* Child */

		argv = calloc(argc + 1, sizeof(*argv));
		if (argv == NULL) {
			fprintf(stderr, "%s: Unable to alloc space for argv\n", __func__);
			_exit(-1);
		}

		argv[0] = executable_name;
		argv[1] = actor;

		va_start(argp, actor);
		for (i = 2; i < argc; i++)
			argv[i] = va_arg(argp, char *);
		va_end(argp);

		argv[argc] = NULL;

		execvp(executable_name, argv);
		perror("execve");
		_exit(-1);
	}


	return 0;
}

#define GROUP_NAME_MAX 40
#define TYPE_NAME_MAX 40
#define TEST_NAME_MAX 40

static char opt_group[GROUP_NAME_MAX];
static char opt_type[TYPE_NAME_MAX];
static char opt_test[TEST_NAME_MAX];

static struct param_inst mpft_params[] = {
	PARAM_INST_STRING(opt_group, sizeof(opt_group), "group", "Test group"),
	PARAM_INST_STRING(opt_type, sizeof(opt_type), "type", "Test type"),
	PARAM_INST_STRING(opt_test, sizeof(opt_test), "test", "Test name"),
	PARAM_INST_END
};

int main(int argc, char **argv)
{

	mpool_err_t  err = 0;
	int     next_arg = 1;
	char   *progname = argv[0];
	bool    test_range_selected;

	struct results_s results;

	executable_name = argv[0];
	memset(&results, 0, sizeof(results));

	snprintf(opt_group, sizeof(opt_group), "%s", WILD);
	snprintf(opt_type, sizeof(opt_type), "%s", WILD);
	snprintf(opt_test, sizeof(opt_test), "%s", WILD);

	/* The format of the command line will be:
	 *   mpool <verb> [<object> [<option1> <option2>]]
	 *   e.g. mpool create mp1 /dev/nvme0n1
	 *
	 * Since each subject may have its own list of verbs, and
	 * each subject+verb its own object + option list, we need
	 * to hierarchically parse the command line.
	 */

	if (xgetopt(argc, argv, "hLhnTv", xoptionv))
		return EX_USAGE;

	if (co.co_log)
		log_command_line(argc, argv);

	next_arg = optind;

	err = process_params(argc, argv, mpft_params, &next_arg, 0);
	if (err) {
		usage(progname);
		return EX_USAGE;
	}

	test_range_selected = (strcmp(opt_group, WILD) ||
		strcmp(opt_type, WILD) || strcmp(opt_test, WILD));

	if ((argc - next_arg <= 0) && !test_range_selected) {
		usage(progname);
		return EX_USAGE;
	}

	if (!test_range_selected && !strcasecmp("all", argv[next_arg])) {
		printf("ALL\n");
	} else {
		/**
		 * There are three choices here:
		 * 1. 'mpft <group>
		 * 2. 'mpft <group>.<type>
		 * 3. 'mpft <group>.<type>.<test>
		 */

		if (strncasecmp(opt_group, WILD, sizeof(opt_group))) {
			/* group specified as option */
		} else {
			char *str;
			char *tk, *svptr;

			str = strdup(argv[next_arg]);
			if (!str)
				return EX_OSERR;

			svptr = NULL;

			tk = strtok_r(str, ".", &svptr);
			if (tk == NULL) {
				usage(progname);
				free(str);
				return EX_USAGE;
			}

			strlcpy(opt_group, tk, sizeof(opt_group));

			tk = strtok_r(NULL, ".", &svptr);
			if (tk) {
				strlcpy(opt_type, tk, sizeof(opt_type));

				tk = strtok_r(NULL, ".", &svptr);
				if (tk)
					strlcpy(opt_test, tk, sizeof(opt_test));
			}

			free(str);
		}
	}

	if (co.co_verbose)
		fprintf(stdout, "group %s, type %s, test %s\n", opt_group, opt_type, opt_test);

	/**
	 * At this point we have group, type, and test determined from
	 * the command line options, with each being either "wild" or a
	 * specific group, type, or test.
	 *
	 * Now, we need to execute the specified tests.
	 */

	/* group */
	if (!strcasecmp(opt_group, WILD)) {
		int               i = 0;
		struct group_s   *g;

		while ((g = m_group[i]) && g->group_test) {
			if (co.co_verbose)
				fprintf(co.co_fp, "group:%s\n", g->group_name);
			execute_group(&results, g, opt_type, opt_test,
				argc-next_arg, &argv[next_arg]);
			i++;
		}
	} else {
		struct group_s   *g = m_group[0];

		g = find_group(m_group, opt_group);
		if (g && g->group_test) {
			if (co.co_verbose)
				fprintf(co.co_fp, "group:%s\n", g->group_name);
			execute_group(&results, g, opt_type, opt_test,
				      argc-next_arg, &argv[next_arg]);
		}
	}

	fprintf(stdout, "Ran %d tests, %d passed, %d failed\n",
		results.total, results.passed, results.failed);

	return results.failed;
}
