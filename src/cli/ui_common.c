// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include "mpool.h"

void
emit_err(
	FILE              *fp,
	mpool_err_t        err,
	char              *errbuf,
	size_t             errbufsz,
	const char        *verb,
	const char         *object,
	struct mp_errinfo *ei)
{
	const char *entity = NULL;
	const char *msg    = NULL;

	if (!err && !ei->mdr_rcode)
		return;

	ei->mdr_msg[sizeof(ei->mdr_msg) - 1] = '\000';

	if (!object || !object[0])
		object = "";

	mpool_strinfo(err, errbuf, errbufsz);

	if (ei->mdr_rcode == MPOOL_RC_ERRMSG) {
		msg = ei->mdr_msg;
	} else if (ei->mdr_rcode) {
		msg = mpool_devrpt_strerror(ei->mdr_rcode);
		if (ei->mdr_msg[0])
			entity = ei->mdr_msg;
	} else if (err) {
		msg = mpool_strinfo(err, errbuf, errbufsz);
		err = 0;
	}

	fprintf(fp, "%s: Unable to %s%s%s%s%s%s%s%s%s%s\n",
		progname, verb,
		object[0] ? " " : "", object,
		msg || entity ? " (" : "",
		msg ?: "",
		entity ? " " : "", entity ?: "",
		msg || entity ? ")" : "",
		err ? ": " : "", err ? errbuf : "");
}
