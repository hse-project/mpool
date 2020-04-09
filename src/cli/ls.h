/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_LS_H
#define MPOOL_LS_H

#include "common.h"

vhelp_func_t mpool_list_help;
verb_func_t mpool_list_func;

uint64_t
mpool_ls_list(
	int                     argc,
	char                  **argv,
	uint32_t                flags,
	int                     verbosity,
	bool                    headers,
	bool                    parsable,
	bool                    yaml,
	char                   *obuf,
	size_t                  obufsz,
	struct mpool_devrpt    *ei);

#endif
