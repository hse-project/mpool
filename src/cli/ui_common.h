/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <stdio.h>

#include <mpool/mpool.h>

#ifndef MPC_DEV_SUBDIR
#define MPC_DEV_SUBDIR      "mpool"
#endif

/**
 * emit_err() - turn an err and errinfo struct into text on a stream
 * @fp:       file pointer to write
 * @err:      mpool_err_t
 * @errbuf:   a user-supplied character buffer, in input, the content of this
 *		buffer is not used.
 * @errbufsz: length of errbuf
 * @verb:     a text string describing an action
 * @object:   a text string describing the direct object of the verb.
 *	Can be NULL.
 * @ei:       error detail and potential corrective action.
 *
 * If err is non-zero, prints the string "Cannot <verb> <object>: <errno text>"
 * If errinfo is valid:
 *	if  the error code is MPOOL_RC_ERRMSG print an error message already
 *	formatted and ready. It is stored in med_entity.
 *      if the error code is not MPOOL_RC_ERRMSG, print
 *	"entity <foo>, error (<number>) <bar>"
 */
void
emit_err(
	FILE              *fp,
	mpool_err_t        err,
	char              *errbuf,
	size_t             errbufsz,
	const char        *verb,
	const char        *object,
	struct mp_errinfo *ei);

#endif /* UI_COMMON_H */
