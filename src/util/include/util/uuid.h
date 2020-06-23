/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UTIL_UUID_H
#define MPOOL_UTIL_UUID_H

#include <uuid/uuid.h>

#define MPOOL_UUID_SIZE        16
#define MPOOL_UUID_STRING_LEN  36

struct mpool_uuid {
	unsigned char uuid[MPOOL_UUID_SIZE];
};

static inline void mpool_unparse_uuid(const struct mpool_uuid *uuid, char *out)
{
	uuid_unparse(uuid->uuid, out);
}

static inline int mpool_parse_uuid(const char *in, struct mpool_uuid *out)
{
	return uuid_parse(in, out->uuid);
}

static inline void mpool_generate_uuid(struct mpool_uuid *uuid)
{
	uuid_generate(uuid->uuid);
}

static inline void mpool_uuid_copy(struct mpool_uuid *u_dst, struct mpool_uuid *u_src)
{
	uuid_copy(u_dst->uuid, u_src->uuid);
}

static inline int mpool_uuid_compare(struct mpool_uuid *uuid1, struct mpool_uuid *uuid2)
{
	return uuid_compare(uuid1->uuid, uuid2->uuid);
}

static inline void mpool_uuid_clear(struct mpool_uuid *uuid)
{
	uuid_clear(uuid->uuid);
}

static inline int mpool_uuid_is_null(struct mpool_uuid *uuid)
{
	return uuid_is_null(uuid->uuid);
}

#endif /* MPOOL_UTIL_UUID_H */
