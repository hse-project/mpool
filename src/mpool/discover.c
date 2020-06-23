// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#define _LARGEFILE64_SOURCE

#include <util/platform.h>
#include <util/string.h>
#include <mpool/mpool.h>
#include <mpctl/impool.h>
#include <mpcore/mpcore_defs.h>

#include "discover.h"
#include "logging.h"

#include <stdio.h>
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>
#include <mpool_blkid/blkid.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

bool imp_mpool_activated(const char *name)
{
	struct stat stat_buf;
	char        path[NAME_MAX + sizeof(MPC_DEV_SUBDIR) + 8];
	int         rc;

	/* Determine if mpool is activated by checking for the special
	 * file /dev/mpool/<mpool_name>.
	 */
	snprintf(path, sizeof(path), "/dev/%s/%s", MPC_DEV_SUBDIR, name);

	rc = stat(path, &stat_buf);

	return (rc == 0 && S_ISCHR(stat_buf.st_mode));
}

bool imp_device_allocated(const char *path, u32 flags)
{
	merr_t  err;
	int     cnt;

	err = imp_entries_get(NULL, NULL, path, &flags, NULL, &cnt);
	if (err)
		return false;

	return (cnt > 0);
}

bool imp_mpool_exists(const char *name, u32 flags, struct imp_entry **entry)
{
	struct mpool_uuid   uuid;

	merr_t  err;
	int     cnt = 0;
	int     rc;

	/* Is the passed-in name an mpool name or uuid? */
	rc = mpool_parse_uuid(name, &uuid);

	err = imp_entries_get(rc ? name : NULL, rc ? NULL : &uuid, NULL, &flags, entry, &cnt);
	if (err)
		return false;

	return (cnt > 0);
}

static bool
imp_entry_match(
	struct imp_entry           *entry,
	const char                 *name,
	const struct mpool_uuid    *uuid,
	const char                 *dpath,
	bool                        invert)
{
	int matchmin = invert ? 1 : (!!name + !!uuid + !!dpath);
	int nmatched = 0;

	if (name && !strcmp(name, entry->mp_name))
		++nmatched;
	if (uuid && !memcmp(uuid, &entry->mp_uuid, sizeof(*uuid)))
		++nmatched;
	if (dpath && !strcmp(dpath, entry->mp_path))
		++nmatched;

	return (nmatched >= matchmin);
}

/**
 * imp_entries_get() - look at devices in /sys/class/block, returns one entry
 *	for each device that matches the input parameters.
 * @name: input
 * @uuid: input
 * @dpath: input
 * @flags: input
 * @entries: can be passed in NULL. If passed in NULL, the lookup is still
 *	done and entry_cnt is updated with the number of matching devices,
 *	but no entry is allocated nor returned.
 *	If passed not NULL and if matching devices are found, one entry per
 *	device is allocated and the table of entries is returned via this
 *	parameter. The caller is reponsible to free the entries.
 *	A free is needed if the function returns 0 (no error) and entry_cnt is
 *	non 0 and entries was passed in not NULL.
 * @entry_cnt: number of matching devices.
 */
merr_t
imp_entries_get(
	const char         *name,
	struct mpool_uuid  *uuid,
	const char         *dpath,
	u32                *flags,
	struct imp_entry  **entries,
	int                *entry_cnt)
{
	struct imp_entry   *my_entries = NULL;
	const char         *d_uuid, *d_type, *d_label;
	blkid_probe         pr;
	struct dirent      *d;

	int     cnt = 0, saved_cnt = 0, ret;
	bool    eacces_logged = false;
	bool    first_pass = true;
	bool    invert = false;
	char   *rpath = NULL;
	bool    rc;
	DIR    *dir;
	merr_t  err;

	if (!entry_cnt)
		return merr(EINVAL);

	*entry_cnt = 0;

	if (dpath) {
		rpath = realpath(dpath, NULL);
		if (!rpath)
			return merr(errno);
	}

	dir = opendir("/sys/class/block");
	if (!dir) {
		err = merr(errno);
		mp_pr_err("%s: Cannot open /sys/class/block", err, __func__);
		free(rpath);
		return err;
	}

	/* On first_pass, get a count.
	 * On second pass, copy entries.
	 */
second_pass_libblkid:
	while ((d = readdir(dir))) {
		char filename[NAME_MAX + 8];
		struct imp_entry entry;
		int n;

		if (d->d_name[0] == '.')
			continue;

		n = snprintf(filename, sizeof(filename), "/dev/%s", d->d_name);
		if (n >= sizeof(filename)) {
			err = merr(ENAMETOOLONG);
			mp_pr_err("design fail", err);
			continue;
		}

		pr = blkid_new_probe_from_filename(filename);
		if (!pr) {
			if (errno == EACCES && !eacces_logged) {
				err = merr(errno);
				mp_pr_err("Device discovery may need access rights in /sys/class/block",
					  err);
				eacces_logged = true;
			}
			continue;
		}

		rc = blkid_do_probe(pr);
		if (rc) {
			blkid_free_probe(pr);
			continue;
		}

		blkid_probe_lookup_value(pr, "TYPE", &d_type, NULL);
		if (!d_type || strcmp(d_type, "mpool")) {
			blkid_free_probe(pr);
			continue;
		}

		blkid_probe_lookup_value(pr, "UUID", &d_uuid, NULL);
		ret = mpool_parse_uuid(d_uuid, &entry.mp_uuid);
		if (ret == -1)
			memset(&entry.mp_uuid, 0, sizeof(entry.mp_uuid));

		blkid_probe_lookup_value(pr, "LABEL", &d_label, NULL);

		if (!d_label) {
			blkid_free_probe(pr);
			continue;
		}

		/* The LABEL contains a zero terminated mpool name, but
		 * place a zero at the end as a safeguard.
		 */
		strlcpy(entry.mp_name, d_label, sizeof(entry.mp_name));
		strlcpy(entry.mp_path, filename, sizeof(entry.mp_path));

		rc = imp_entry_match(&entry, name, uuid, rpath, invert);
		if (rc) {
			if (!first_pass) {
				err = imp_dev_get_prop(filename, &entry.mp_pd_prop);
				if (!err)
					*my_entries++ = entry;
			}

			cnt++;
			if (!first_pass && cnt == saved_cnt) {
				blkid_free_probe(pr);
				break;
			}
		}

		blkid_free_probe(pr);
	}

	err = 0;

	if (first_pass) {
		first_pass = false;
		if (cnt == 0)
			goto errout;

		if (!entries) {
			*entry_cnt = cnt;
			goto errout;
		}

		*entries = calloc(cnt, sizeof(**entries));
		if (!*entries) {
			err = merr(ENOMEM);
			goto errout;
		}

		my_entries = *entries;
		saved_cnt = cnt;
		cnt = 0;
		rewinddir(dir);
		goto second_pass_libblkid;
	}

	/* Return only the entries for which we could acquire valid
	 * information.
	 */
	*entry_cnt = my_entries - *entries;

errout:
	closedir(dir);
	free(rpath);

	return err;
}

struct pd_prop *imp_entries2pd_prop(int entry_cnt, struct imp_entry *entries)
{
	int		i;
	struct pd_prop *pdp;

	if (!entry_cnt)
		return NULL;

	pdp = malloc(sizeof(struct pd_prop) * entry_cnt);
	if (pdp == NULL)
		return NULL;

	for (i = 0; i < entry_cnt; i++)
		*(pdp+i) = entries[i].mp_pd_prop;

	return pdp;
}
