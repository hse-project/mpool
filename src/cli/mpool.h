/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_CLI_H
#define MPOOL_CLI_H

#include "common.h"

extern const struct xoption xoptionv[];

extern struct subject_s mpool_ui;
extern struct subject_s device_ui;
extern struct subject_s mblock_ui;
extern struct subject_s mlog_ui;
extern struct subject_s mdc_ui;

const char *progname;

/* Use this enum for the 'terse' argument to the various help functions to
 * improve readability
 */
enum verbosity {
	MPOOL_VERBOSE = false,
	MPOOL_TERSE = true,
};

#define MPUI_ERRBUFSZ 128

#endif /* MPOOL_CLI_H */
