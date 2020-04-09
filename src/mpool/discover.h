/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_DISCOVER_H
#define MPOOL_DISCOVER_H

#include <util/platform.h>

#include "mpctl.h"

struct imp_media_struct {
	enum mp_media_classp    mpd_classp;
	char                    mpd_path[NAME_MAX + 1];
};

struct imp_pool {
	char                        mp_name[MPOOL_NAME_LEN_MAX];
	struct mpool_uuid           mp_uuid;
	bool                        mp_activated;
	int                         mp_media_cnt;
	struct imp_media_struct    *mp_media;
};

struct imp_entry {
	char                mp_name[MPOOL_NAME_LEN_MAX];
	struct mpool_uuid   mp_uuid;
	struct pd_prop      mp_pd_prop;
	char                mp_path[NAME_MAX + 1];
};

/**
 * imp_mpool_exists() - check if specified mpool exists
 * @name: char *, name of mpool
 * @flags:   u32, flags
 * @entry:
 *
 * Returns: true if mpool is named in any block device, otherwise false
 */
bool
imp_mpool_exists(
	const char          *name,
	u32                  flags,
	struct imp_entry   **entry);

/**
 * imp_mpool_activated() - check if specified mpool is activated
 * @name: char *, name of mpool
 *
 * imp_mpool_activated will check if the device special file for
 * the named mpool exists, and return true if it does.
 *
 * Returns: true if mpool's special file exists
 */
bool
imp_mpool_activated(
	const char *name);

/**
 * imp_device_allocated() - Determine if a given media device is in use
 * @dpath: char *, name of media device
 * @flags:   u32, flags
 *
 * imp_device_allocated() will use libblkid to see if there is a reference
 * to the specified device.
 *
 * Returns: true if the named device is allocated to an mpool
 */
bool
imp_device_allocated(
	const char *dpath,
	u32         flags);

/**
 * imp_entries_get() - given a pool UUID, get data from libblkid
 * @name:    target mpool name
 * @uuid:    uuid of target mpool
 * @dpath:   target device name
 * @flags:   u32 *, flags
 * @entry:  pointer to array of struct imp_entry
 * @entry_cnt:    the count of member drives
 *
 * The array and its member strings will be allocated and must later be freed.
 */
merr_t
imp_entries_get(
	const char         *name,
	struct mpool_uuid    *uuid,
	const char         *dpath,
	u32                *flags,
	struct imp_entry  **entry,
	int                *entry_cnt);

/**
 * imp_entries2pd_prop() - Allocate a table of pd properties and copies
 *	the properties from the imp entries into that table.
 *
 * @entry_cnt:
 * @entries:
 */
struct pd_prop *
imp_entries2pd_prop(
	int entry_cnt,
	struct imp_entry *entries);

#endif
