/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_BASE_H
#define MPOOL_UTIL_BASE_H

/* In RHEL8, failure to #define _DEFAULT_SOURCE 1 results in a dizzying
 * range of missing macros and/or prototypes.  Some examples:
 * htole*() (and lots of other byte swapping functions), usleep(),
 * uint, ulong, isascii(), and probably more.
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <linux/fs.h>

#define MPOOL_UTIL_AUTHOR  "Micron Technology"
#define MPOOL_UTIL_DESC    "Object storate media pool util"

#endif /* MPOOL_UTIL_BASE_H */
