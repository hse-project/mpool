// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */
/*
 * This test tool tests mblock boundary enforcement via mcache maps.  Reading
 * mblocks via mcache maps should work fine; however, attempts to read beyond
 * mcache boundaries should result in the process receiving a SIGBUS.
 *
 * Much code borrowed from mpiotest.
 *
 * Setup:
 *    $ cd ~/mpool/builds/debug/stage/bin
 *
 * Examples:
 *    Given an mpool named "mp1":
 *
 *    $ sudo mcache_boundary mp1 MC_MAP_TYPE_READ
 *    $ sudo mcache_boundary -v mp1 MC_MAP_TYPE_MMAP
 */

#include <errno.h>
#include <getopt.h>
#include <malloc.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#define COMPNAME "mcache_api"

#include <util/inttypes.h>
#include <util/uuid.h>
#include <util/page.h>
#include <mpool/mpool.h>

#define merr(_errnum)   (_errnum)

const char     *progname;
const char     *rndfile = "/dev/urandom";


static char     errbuf[64];
static char    *rndbuf;
static char    *rndbuf_cursor;

static size_t   rndbufsz;

const int       num_mblocks = 2;

static bool     verbose;

static sig_atomic_t     sigbus;


/*
 * Use this to decide whether to jump back to the caller after the signal
 * handler, so that execution can continue.
 */
jmp_buf    *sigbus_jmp;

static void sigbus_handler(int sig)
{
	++sigbus;

	if (sigbus_jmp)
		/*
		 * The "1" is significant:  it will cause the sigsetjmp() that
		 * we set up before we got here to return 1 instead of 0, so
		 * that we don't just retry the function that led to the
		 * SIGBUS, over and over in an endless loop.
		 */
		(void)siglongjmp(*sigbus_jmp, 1);

	/*
	 * If we're still here, the jump failed, so bail.
	 */
	abort();
}


static int signal_reliable(int signo, __sighandler_t func)
{
	struct sigaction nact;

	bzero(&nact, sizeof(nact));

	nact.sa_handler = func;
	sigemptyset(&nact.sa_mask);

	if (SIGALRM == signo || SIGINT == signo) {
#ifdef SA_INTERRUPT
		nact.sa_flags |= SA_INTERRUPT;
#endif
	} else {
#ifdef SA_RESTART
		nact.sa_flags |= SA_RESTART;
#endif
	}

	return sigaction(signo, &nact, (struct sigaction *)0);
}


static void eprint(const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	snprintf(msg, sizeof(msg), "%s(%lx): ", progname, (long)pthread_self);

	va_start(ap, fmt);
	vsnprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), fmt, ap);
	va_end(ap);

	fputs(msg, stderr);
}


static void usage(void)
{
	printf("usage: %s [options] <media-class> <mpool>\n\n", progname);
	printf("-a,--all             run all tests\n");
	printf("-b,--boundary        run mcache mmap boundary test\n");
	printf("-m,--madvise         call madvise() on the mapped mblocks\n");
	printf("-v,--verbose         be wordy\n\n");
	printf("media-class          {STAGING|CAPACITY}\n");
	printf("mpool                name of mpool to use\n");
}


static int fill_rndbuf(void)
{
	size_t  fileptr;
	ssize_t bytes_read;
	int     fd, rc;

	/*
	 * Fill the random buffer
	 */
	fd = open(rndfile, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "open(%s): %s\n", rndfile, strerror(errno));
		return 1;
	}

	rndbufsz = MPOOL_MBSIZE_MB_MAX;

	rc = posix_memalign((void **)&rndbuf, PAGE_SIZE, rndbufsz);
	if (rc) {
		close(fd);
		return 1;
	}

	if (!rndbuf) {
		fprintf(stderr, "Couldn't allocate rndbuf: %s\n", strerror(errno));
		rc = 1;
		goto rndbuf_cleanup;
	}

	for (fileptr = 0; fileptr < rndbufsz; fileptr += bytes_read) {
		bytes_read = read(fd, rndbuf + fileptr, rndbufsz - fileptr);
		if (bytes_read < 1) {
			fprintf(stderr, "read(%s): bytes_read=%ld rndbufsz=%zu: %s\n",
				rndfile, bytes_read, rndbufsz, strerror(errno)
			);
			rc = 1;
			goto rndbuf_cleanup;
		}
	}

	rndbuf_cursor = rndbuf;

	goto rndbuf_exit;

rndbuf_cleanup:
	free(rndbuf);

rndbuf_exit:
	close(fd);
	return rc;
}


static enum mp_media_classp mclsname_to_mcls(const char *mclassname)
{
	if (!strcmp(mclassname, "STAGING"))
		return MP_MED_STAGING;
	else if (!strcmp(mclassname, "CAPACITY"))
		return MP_MED_CAPACITY;

	return MP_MED_INVALID;
}


/**
 * make_mblock() - allocate, write, and commit an mblock
 * @mbsize:            mblock size
 * @props:             pointer to mblock_props struct to receive allocated
 *                     mblock properties
 * @objid:             uint64_t, mblock ID
 * @media_class:       mp_media_classp, type of media to use.
 *
 * make_mblock() allocates an mblock of the requested size and writes random
 * data to it by cursoring through a previously-initialized random buffer.  It
 * then commits the mblock.
 *
 * Return: mpool_err_t
 */
static mpool_err_t
make_mblock(
	struct mpool         *mp,
	uint64_t              mbsize,
	struct mblock_props  *props,
	uint64_t             *objid,
	enum mp_media_classp  media_class)
{
	struct iovec   *iov = NULL;

	mpool_err_t err;
	mpool_err_t err2;

	if (((rndbuf_cursor - rndbuf) + mbsize) > rndbufsz) {
		fprintf(
			stderr, "%s %s",
			"Requested random fill data runs off the end of", "rndbuf\n");
		err = merr(EINVAL);

		goto make_mblock_exit;
	}

	err = mpool_mblock_alloc(mp, media_class, false, objid, props);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("mpool_mblock_alloc failed: %s\n", errbuf);
		goto make_mblock_cleanup;
	}

	if (props->mpr_alloc_cap != mbsize) {
		err = merr(ENOSPC);
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("mpool_mblock_alloc returned mblock of wrong size: %s\n", errbuf);
		goto make_mblock_cleanup;
	}
	iov = malloc(sizeof(*iov));

	if (!iov) {
		err = merr(errno);
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("failed to allocate iovec: %s\n", errbuf);
		goto make_mblock_cleanup;
	}

	iov->iov_base  = rndbuf_cursor;
	iov->iov_len   = mbsize;
	rndbuf_cursor += mbsize;

	err = mpool_mblock_write(mp, *objid, iov, 1);

	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("mpool_mblock_write failed: %s\n", errbuf);
		goto make_mblock_cleanup;
	}

	err = mpool_mblock_commit(mp, *objid);

	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("mb_mblock_commit failed: objid=0x%lx: %s\n", *objid, errbuf);
		goto make_mblock_cleanup;
	}

	goto make_mblock_exit;

make_mblock_cleanup:
	err2 = mpool_mblock_abort(mp, *objid);

	if (err2) {
		mpool_strinfo(err2, errbuf, sizeof(errbuf));
		eprint("mpool_mblock_abort failed: objid=0x%lx: %s\n", *objid, errbuf);
	} else {
		err2 = mpool_mblock_delete(mp, *objid);

		if (err2) {
			mpool_strinfo(err2, errbuf, sizeof(errbuf));
			eprint("mpool_mblock_delete failed: objid=0x%lx: %s\n", *objid, errbuf);
		}
	}

make_mblock_exit:
	free(iov);
	return err;
}


int
mcache_boundary_test(
	const char           *mpname,
	enum mp_media_classp  media_class,
	bool                  call_madvise)
{
	int i;

	/*
	 * This is a single-threaded program; therefore, we don't worry about
	 * masking off signals like mpiotest does.  Just install the signal
	 * handler to count the number of SIGBUS signals we receive.
	 */

	signal_reliable(SIGBUS, sigbus_handler);

	/* Fill random buffer we'll use for writing mblocks, and open the mpool. */

	volatile int rc = fill_rndbuf();

	if (rc)
		goto mcache_boundary_cleanup;

	mpool_err_t         err;
	struct mpool       *mp;
	struct mpool_devrpt ei;

	err = mpool_open(mpname, O_RDWR, &mp, &ei);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("mpool_open(%s): %s\n", mpname, errbuf);
		rc = 1;
		goto mcache_boundary_cleanup;
	}

	/*
	 * Alloc two mblocks and make an mcache map for them.
	 */
	struct mblock_props    props1;
	struct mpool_params    params;
	uint64_t               objid1;
	uint64_t               mbsize;

	err = mpool_params_get(mp, &params, &ei);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("mpool_params_get(%s): %s\n", mpname, errbuf);
		rc = 1;
		goto mcache_boundary_mp_close;
	}
	mbsize = params.mp_mblocksz[media_class] << 20;

	err = make_mblock(mp, mbsize, &props1, &objid1, media_class);
	if (err) {
		rc = 1;
		goto mcache_boundary_mp_close;
	}

	struct mblock_props props2;
	uint64_t            objid2;

	err = make_mblock(mp, mbsize, &props2, &objid2, media_class);
	if (err) {
		rc = 1;
		goto mcache_boundary_mblock_cleanup_1;
	}

	/*
	 * Create the mcache map, and advise that we need the pages in cache.
	 */

	struct test_mcache_mblock_info {
		uint64_t     mblockid;
		size_t       mblocklen;
		int          mblockidx;
	} *test_map_info;

	test_map_info = malloc(
		sizeof(struct test_mcache_mblock_info) * num_mblocks
	);

	if (!test_map_info) {
		err = merr(errno);
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("failed to allocate test_map_info: %s\n", errbuf);
		rc = 1;
		goto mcache_boundary_mblock_cleanup_2;
	}

	test_map_info[0].mblockid = objid1;
	test_map_info[1].mblockid = objid2;

	test_map_info[0].mblocklen = mbsize;
	test_map_info[1].mblocklen = mbsize;

	test_map_info[0].mblockidx = 0;
	test_map_info[1].mblockidx = 1;

	uint64_t mbidv[] = { objid1, objid2 };

	assert(sizeof(mbidv)/sizeof(uint64_t) == num_mblocks);

	struct mpool_mcache_map *map;

	err = mpool_mcache_mmap(mp, num_mblocks, mbidv, MPC_VMA_HOT, &map);

	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("failed to create mcache map: %s\n", errbuf);
		rc = 1;
		goto mcache_boundary_mblock_cleanup_2;
	}

	if (call_madvise)
		for (i = 0; i < num_mblocks; i++) {
			err = mpool_mcache_madvise(map, test_map_info[i].mblockidx, 0,
						   test_map_info[i].mblocklen, MADV_WILLNEED);

			if (err) {
				mpool_strinfo(err, errbuf, sizeof(errbuf));
				eprint("mpool_mcache_madvise failed: map=0x%lx mbid=0x%lx: %s\n",
				       map, test_map_info[i].mblockid, errbuf);
				rc = 1;
				goto mcache_boundary_map_cleanup;
			}
		}

	/*
	 * Read the full 4MB mblock, should succeed.
	 */
	rndbuf_cursor = rndbuf;

	char    *buf = malloc(test_map_info[0].mblocklen);

	if (!buf) {
		err = merr(errno);
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("failed to allocate map buf: %s\n", errbuf);
		rc = 1;
		goto mcache_boundary_map_cleanup;
	}

	uint     page_count = test_map_info[0].mblocklen / PAGE_SIZE;
	size_t  *pagenumv   = malloc(sizeof(size_t) * page_count);
	void   **addrv      = malloc(sizeof(void *) * page_count);

	if (!pagenumv || !addrv) {
		err = merr(ENOMEM);
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("failed to allocate page vector: %s\n", errbuf);
		rc = 1;
		goto mcache_boundary_map_cleanup;
	}

	for (i = 0; i < page_count; i++)
		pagenumv[i] = i;

	printf("Reading initial mblock.\n");

	err = mpool_mcache_getpages(map, page_count, test_map_info[0].mblockidx, pagenumv, addrv);
	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("mpool_mcache_getpages: %d objid=0x%lx len=%zu: %s\n",
		       test_map_info[0].mblockidx, test_map_info[0].mblockid,
		       test_map_info[0].mblocklen, errbuf);
		rc = 1;
		goto mcache_boundary_map_cleanup;
	}

	for (i = 0; i < page_count; i++)
		memcpy(buf + i * PAGE_SIZE, addrv[i], PAGE_SIZE);

	if (memcmp(buf, rndbuf, mbsize)) {
		eprint("Data read mismatch from 4MB mblock objid=0x%lx\n",
		       test_map_info[0].mblockid);
		rc = 1;
		goto mcache_boundary_map_cleanup;
	}

	printf("Successfully read initial mblock.\n");

	free(pagenumv);
	free(addrv);
	free(buf);

	/*
	 * Attempt to read the first 4KB past the end of the first mblock.
	 * Should get a SIGBUS.
	 */

	void *mblock1_start = mpool_mcache_getbase(map, test_map_info[0].mblockidx);

	jmp_buf    mblock1_read_past_end_jmpbuf;

	sigbus_jmp = &mblock1_read_past_end_jmpbuf;

	printf("Reading 4K past end of initial mblock.\n");

	if (sigsetjmp(*sigbus_jmp, 1) == 0) {
		char *buf1 = malloc(PAGE_SIZE);

		if (!buf1) {
			err = merr(errno);
			mpool_strinfo(err, errbuf, sizeof(errbuf));
			eprint("failed to allocate map buf: %s\n", errbuf);
			rc = 1;
			goto mcache_boundary_map_cleanup;
		}

		memcpy(buf1, mblock1_start + test_map_info[0].mblocklen, PAGE_SIZE);

		free(buf1);
	}

	printf("Returned from signal handler\n");

	if (sigbus != 1) {
		eprint("Did not get sigbus reading past end of first mblock objid=0x%lx\n",
		       test_map_info[0].mblockid);
		rc = 1;
		goto mcache_boundary_map_cleanup;
	}

	sigbus_jmp = NULL;

	/*
	 * Attempt to read 4KB before the beginning of the second mblock.
	 * Should get a SIGBUS.
	 */
	printf("Reading 4K before beginning of second mblock.\n");

	void *mblock2_start = mpool_mcache_getbase(map, test_map_info[1].mblockidx);

	jmp_buf    mblock2_read_before_start_jmpbuf;

	sigbus_jmp = &mblock2_read_before_start_jmpbuf;

	if (sigsetjmp(*sigbus_jmp, 1) == 0) {
		char *buf2 = malloc(PAGE_SIZE);

		if (!buf2) {
			err = merr(errno);
			mpool_strinfo(err, errbuf, sizeof(errbuf));
			eprint("failed to allocate map buf: %s\n", errbuf);
			rc = 1;
			goto mcache_boundary_map_cleanup;
		}

		memcpy(buf2, mblock2_start - PAGE_SIZE, PAGE_SIZE);

		free(buf2);
	}

	printf("Returned from signal handler\n");

	if (sigbus != 2) {
		eprint("Didn't get sigbus reading before start of second mblock objid=0x%lx\n",
		       test_map_info[1].mblockid);
		rc = 1;
		goto mcache_boundary_map_cleanup;
	}

	sigbus_jmp = NULL;

	/*
	 * Attempt to read the first 4KB past the end of the second mblock.
	 * Should get a SIGBUS.
	 */
	printf("Reading 4K past end of second mblock.\n");

	jmp_buf    mblock2_read_past_end_jmpbuf;

	sigbus_jmp = &mblock2_read_past_end_jmpbuf;

	if (sigsetjmp(*sigbus_jmp, 1) == 0) {
		char *buf3 = malloc(PAGE_SIZE);

		if (!buf) {
			err = merr(errno);
			mpool_strinfo(err, errbuf, sizeof(errbuf));
			eprint("failed to allocate map buf: %s\n", errbuf);
			rc = 1;
			goto mcache_boundary_map_cleanup;
		}

		memcpy(buf3, mblock2_start + test_map_info[1].mblocklen, PAGE_SIZE);

		free(buf3);
	}
	printf("Returned from signal handler\n");

	sigbus_jmp = NULL;

	/*
	 * Finally, we should see exactly three caught SIGBUS signals.
	 */

	if (sigbus != 3) {
		eprint("Did not get sigbus reading past end of second mblock objid=0x%lx\n",
		       test_map_info[1].mblockid);
		rc = 1;
	}

mcache_boundary_map_cleanup:
	free(test_map_info);

	err = mpool_mcache_munmap(map);

	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("mpool_mcache_destroy failed: %s\n", errbuf);
		rc = 1;
	}

mcache_boundary_mblock_cleanup_2:
	err = mpool_mblock_delete(mp, objid2);

	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("failed to delete second mblock, objid: 0x%lx, %s\n", objid2, errbuf);
		rc = 1;
	}

mcache_boundary_mblock_cleanup_1:
	err = mpool_mblock_delete(mp, objid1);

	if (err) {
		mpool_strinfo(err, errbuf, sizeof(errbuf));
		eprint("failed to delete first mblock, objid: 0x%lx, %s\n", objid1, errbuf);
		rc = 1;
	}

mcache_boundary_mp_close:
	mpool_close(mp);

mcache_boundary_cleanup:
	free(rndbuf);

	return rc;
}


int main(int argc, char **argv)
{
	bool err_usage = false;

	sigbus = 0;

	progname = strrchr(argv[0], '/');
	progname = (progname ? progname + 1 : argv[0]);

	struct option longopts[] = {
		{ "all",         no_argument,       NULL, 'a' },
		{ "boundary",    no_argument,       NULL, 'b' },
		{ "help",        no_argument,       NULL, 'h' },
		{ "madvise",     no_argument,       NULL, 'm' },
		{ "verbose",     no_argument,       NULL, 'v' },
	};

	int    ch;
	bool   all_tests     = false;
	bool   boundary_test = false;
	bool   call_madvise  = false;

	verbose = false;

	while ((ch = getopt_long(argc, argv, "abhmv", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			all_tests = true;
			break;
		case 'b':
			boundary_test = true;
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		case 'm':
			call_madvise = true;
			break;
		case 'v':
			verbose = true;
			break;
		default:
			usage();
			exit(EX_USAGE);
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2) {
		fprintf(stderr, "Missing argument\n");
		usage();
		exit(EX_USAGE);
	}

	char  *media_class_name = strdup(argv[0]);
	char  *mpname           = strdup(argv[1]);
	int    failures = 0;

	enum mp_media_classp media_class = mclsname_to_mcls(media_class_name);

	if (media_class == MP_MED_INVALID) {
		err_usage = true;
		fprintf(stderr, "Invalid media class: '%s'\n", media_class_name);
		goto exit_usage;
	}

	if (!mpname) {
		err_usage = true;
		fprintf(stderr, "Mpool name not specified\n");
		goto exit_usage;
	}

	if (all_tests || boundary_test) {
		printf("Running mcache boundary test\n");
		int rc = mcache_boundary_test(mpname, media_class, call_madvise);

		if (rc)
			printf("\tFAILED!\n");
		else
			printf("\tPassed\n");

		failures += rc;
	}

exit_usage:
	free(mpname);
	free(media_class_name);

	if (err_usage) {
		usage();
		exit(EX_USAGE);
	}

	if (failures)
		exit(EXIT_FAILURE);
	else
		exit(EXIT_SUCCESS);
}
