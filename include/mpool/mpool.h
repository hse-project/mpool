/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */
/*
 * Mpool Management APIs
 */

#ifndef MPOOL_H
#define MPOOL_H

#include <stdint.h>
#include <stdbool.h>
#include <uuid/uuid.h>
#include <sys/param.h>
typedef uuid_t uuid_le;

#include <mpool/mpool_ioctl.h>

typedef int64_t                 mpool_err_t;

struct mpool;                   /* opaque mpool handle */
struct mpool_mdc;               /* opaque MDC (metadata container) handle */
struct mpool_mcache_map;        /* opaque mcache map handle */
struct mpool_mlog;              /* opaque mlog handle */

#define MPOOL_RUNDIR_ROOT       "/var/run/mpool"

/* MTF_MOCK_DECL(mpool) */

/*
 * Mpool Administrative APIs...
 */

/**
 * mpool_create() - Create an mpool in the default media class
 * @mpname:  mpool name
 * @devname: device name
 * @params:  mpool params
 * @flags:   mpool management flags
 * @ei:      error detail
 */
mpool_err_t
mpool_create(
	const char             *mpname,
	const char             *devname,
	struct mpool_params    *params,
	uint32_t                flags,
	struct mpool_devrpt    *ei);

/**
 * mpool_destroy() - Deactivate and destroy an mpool
 * @mpname: mpool name
 * @flags:  mpool management flags
 * @ei:     error detail
 */
/* MTF_MOCK */
mpool_err_t mpool_destroy(const char *mpname, uint32_t flags, struct mpool_devrpt *ei);

/**
 * mpool_activate() - Activate an mpool
 * @mpname: name of mpool to activate
 * @params: mpool parameters
 * @flags:  mpool management flags
 * @ei:     error detail
 *
 * Discovers associated block devices and attempts cohesive activate.
 */
mpool_err_t
mpool_activate(
	const char             *mpname,
	struct mpool_params    *params,
	uint32_t                flags,
	struct mpool_devrpt    *ei);

/**
 * mpool_deactivate() - Deactivate an mpool by name
 * @mpname: mpool name
 * @flags:  mpool management flags
 * @ei:     error detail
 */
mpool_err_t mpool_deactivate(const char *mpname, uint32_t flags, struct mpool_devrpt *ei);

/**
 * mpool_mclass_add() - Add a media class to an mpool
 * @mpname:  mpool name
 * @devname: device name
 * @mclass:  media class of the device
 * @flags:   mpool management flags
 * @ei:      error detail
 */
mpool_err_t
mpool_mclass_add(
	const char             *mpname,
	const char             *devname,
	enum mp_media_classp    mclass,
	struct mpool_params    *params,
	uint32_t                flags,
	struct mpool_devrpt    *ei);

/**
 * mpool_rename() - Rename an mpool by name
 * @oldmp: old mpool name
 * @newmp: new mpool name
 * @flags: mpool management flags
 * @ei:    error detail
 */
mpool_err_t
mpool_rename(const char *oldmp, const char *newmp, uint32_t flags, struct mpool_devrpt *ei);

/**
 * mpool_scan() - Scan disks for mpools
 * @propcp: (output) count of mpools found
 * @propvp: (output) vector of mpool properties
 * @ei:     error detail
 *
 * %mpool_scan scans all drives on the system and builds
 * a list of tuples (mpool name and uuid) for each mpool found.
 *
 * Note that only valid fields in each struct mpool_params record
 * are %mp_poolid and %mp_name.
 */
mpool_err_t mpool_scan(int *propcp, struct mpool_params **propvp, struct mpool_devrpt *ei);

/**
 * mpool_list() - Get list of active mpools
 * @propcp: (output) count of mpools found
 * @propvp: (output) vector of mpool properties
 * @ei:     error detail
 *
 * %mpool_list retrieves the list of active mpools from the mpool kernel module.
 */
mpool_err_t mpool_list(int *propcp, struct mpool_params **propvp, struct mpool_devrpt *ei);

/**
 * mpool_open() - Open an mpool
 * @mp_name: mpool name
 * @flags:   open flags
 * @mp:      mpool handle (output)
 * @ei:      error detail
 *
 * Flags are limited to a subset of flags allowed by open(2):
 * O_RDONLY, O_WRONLY, O_RDWR, and O_EXCL.
 *
 * If the O_EXCL flag is given on first open then all subsequent calls to
 * @mpool_open() will fail with -EBUSY.  Similarly, if the mpool is open in
 * shared mode then specifying the O_EXCL flag will fail with -EBUSY.
 */
/* MTF_MOCK */
mpool_err_t
mpool_open(const char *mpname, uint32_t flags, struct mpool **mp, struct mpool_devrpt *ei);

/**
 * mpool_close() - Close an mpool
 * @mp: mpool handle
 */
/* MTF_MOCK */
mpool_err_t mpool_close(struct mpool *mp);

/**
 * mpool_mclass_get() - get properties of the specified media class
 * @mp:      mpool descriptor
 * @mclass:  input media mclass
 * @props:   media class props (output)
 *
 * Returns: 0 for success and ENOENT if the specified mclass is not present
 */
/* MTF_MOCK */
mpool_err_t
mpool_mclass_get(struct mpool *mp, enum mp_media_classp mclass, struct mpool_mclass_props *props);

/**
 * mpool_usage_get() - Get mpool usage
 * @mp:    mpool handle
 * @usage: mpool usage (output)
 */
mpool_err_t mpool_usage_get(struct mpool *mp, struct mpool_usage *usage);

/**
 * mpool_dev_props_get() - Get properties of a device within an mpool
 * @mp:      mpool handle
 * @devname: device name
 * @dprops:  device props (output)
 */
mpool_err_t
mpool_dev_props_get(struct mpool *mp, const char *devname, struct mpool_devprops *dprops);

/**
 * mpool_params_init() - initialize mpool params
 * @params: params instance to initialize
 */
void mpool_params_init(struct mpool_params *params);

/**
 * mpool_params_get() - get parameters of an activated mpool
 * @mp:     mpool handle
 * @params: mpool parameters
 * @ei:     error detail
 */
/* MTF_MOCK */
mpool_err_t
mpool_params_get(struct mpool *mp, struct mpool_params *params, struct mpool_devrpt *ei);

/**
 * mpool_params_set() - set parameters of an activated mpool
 * @mp:     mpool handle
 * @params: mpool parameters
 * @ei:     error detail
 */
mpool_err_t
mpool_params_set(struct mpool *mp, struct mpool_params *params, struct mpool_devrpt *ei);


/*
 * Mpool Data Manager APIs
 */

/******************************** MLOG APIs ************************************/

/**
 * mpool_mlog_alloc() - allocate an mlog
 * @mp:      mpool handle
 * @mclassp: mlog media class
 * @capreq:  mlog capacity requirements
 * @mlogid:  mlog id (output)
 * @props:   properties of new mlog (output)
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t
mpool_mlog_alloc(
	struct mpool            *mp,
	enum mp_media_classp     mclassp,
	struct mlog_capacity    *capreq,
	uint64_t                *mlogid,
	struct mlog_props       *props);

/**
 * mpool_mlog_commit() - commit an mlog
 * @mp:     mpool handle
 * @mlogid: mlog object ID
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_commit(struct mpool *mp, uint64_t mlogid);

/**
 * mpool_mlog_abort() - abort an mlog
 * @mp:     mpool handle
 * @mlogid: mlog object ID
 *
 * Return: %0 on success, <%0 on error
 */
mpool_err_t mpool_mlog_abort(struct mpool *mp, uint64_t mlogid);

/**
 * mpool_mlog_delete() - delete an mlog
 * @mp:     mpool handle
 * @mlogid: mlog object ID
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_delete(struct mpool *mp, uint64_t mlogid);

/**
 * mpool_mlog_open() - open an mlog
 * @mp:     mpool handle
 * @mlogid: mlog object ID
 * @flags:  mlog open flags (enum mlog_open_flags)
 * @gen:    generation number (output)
 * @mlogh:  mlog handle (output)
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t
mpool_mlog_open(
	struct mpool        *mp,
	uint64_t             mlogid,
	uint8_t              flags,
	uint64_t            *gen,
	struct mpool_mlog  **mlogh);

/**
 * mpool_mlog_close() - close an mlog
 * @mlogh: mlog handle
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_close(struct mpool_mlog *mlogh);

/**
 * mpool_mlog_append() - Appends data to an mlog
 * @mlogh: mlog handle
 * @iov:   buffer in the form of struct iovec
 * @len:   buffer len
 * @sync:  1 = sync; 0 = async append
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_append(struct mpool_mlog *mlogh, struct iovec *iov, size_t len, int sync);

/**
 * mpool_mlog_rewind() - Rewinds the internal read cursor to the start of log
 * @mlogh: mlog handle
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_rewind(struct mpool_mlog *mlogh);

/**
 * mpool_mlog_read() - Reads the next record from mlog based on where
 *                     the internal read cursor points to
 * @mlogh: mlog handle
 * @data:  buffer to read data into
 * @len:   buffer len
 * @rdlen: data in bytes of the returned record (output)
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_read(struct mpool_mlog *mlogh, void *data, size_t len, size_t *rdlen);

/**
 * mpool_mlog_seek_read() - Reads the next record from mlog after skipping bytes.
 * @mlogh: mlog handle
 * skip    number of bytes to be skipped before read
 * @data:  buffer to read data into
 * @len:   buffer len
 * @rdlen: data in bytes of the returned record (output)
 *
 * Return: %0 on success, <%0 on error
 *         If merr_errno() of the return value is EOVERFLOW, then the receive buffer
 *         "data" is too small and must be resized according to the value returned in "rdlen".
 */
/* MTF_MOCK */
mpool_err_t
mpool_mlog_seek_read(struct mpool_mlog *mlogh, size_t skip, void *data, size_t len, size_t *rdlen);

/**
 * mpool_mlog_sync() - Sync an mlog to stable media
 * @mlogh: mlog handle
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_sync(struct mpool_mlog *mlogh);

/**
 * mpool_mlog_len() - Returns length of an mlog
 * @mlogh: mlog handle
 * @len:   length of the mlog (output)
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_len(struct mpool_mlog *mlogh, size_t *len);

/**
 * mpool_mlog_erase() - Erase an mlog
 * @mlogh:  mlog handle
 * @mingen: mininum generation number to use (pass 0 if not relevant)
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mlog_erase(struct mpool_mlog *mlogh, uint64_t mingen);

/**
 * mpool_mlog_props_get() - Get properties of an mlog
 * @mlogh: mlog handle
 * @props: mlog properties (output)
 *
 * Return: %0 on success, <%0 on error
 */
mpool_err_t mpool_mlog_props_get(struct mpool_mlog *mlogh, struct mlog_props *props);


/******************************** MDC APIs ************************************/

/**
 * mpool_mdc_alloc() - Alloc an MDC
 * @mp:      mpool handle
 * @logid1:  Mlog ID 1
 * @logid2:  Mlog ID 2
 * @mclassp: media class
 * @capreq:  capacity requirements
 * @props:   MDC properties
 */
/* MTF_MOCK */
mpool_err_t
mpool_mdc_alloc(
	struct mpool               *mp,
	uint64_t                   *logid1,
	uint64_t                   *logid2,
	enum mp_media_classp        mclassp,
	const struct mdc_capacity  *capreq,
	struct mdc_props           *props);

/**
 * mpool_mdc_commit() - Commit an MDC
 * @mp:     mpool handle
 * @logid1: Mlog ID 1
 * @logid2: Mlog ID 2
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_commit(struct mpool *mp, uint64_t logid1, uint64_t logid2);

/**
 * mpool_mdc_abort() - Abort an MDC
 * @mp:     mpool handle
 * @logid1: Mlog ID 1
 * @logid2: Mlog ID 2
 */
mpool_err_t mpool_mdc_abort(struct mpool *mp, uint64_t logid1, uint64_t logid2);

/**
 * mpool_mdc_delete() - Delete an MDC
 * @mp:     mpool handle
 * @logid1: Mlog ID 1
 * @logid2: Mlog ID 2
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_delete(struct mpool *mp, uint64_t logid1, uint64_t logid2);

/**
 * mpool_mdc_get_root() - Retrieve mpool root MDC OIDs
 * @mp:     mpool handle
 * @logid1: Mlog ID 1
 * @logid2: Mlog ID 2
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_get_root(struct mpool *mp, uint64_t *logid1, uint64_t *logid2);

/**
 * mpool_mdc_open() - Open MDC by OIDs
 * @mp:      mpool handle
 * @logid1:  Mlog ID 1
 * @logid2:  Mlog ID 2
 * @flags:   MDC Open flags (enum mdc_open_flags)
 * @mdc_out: MDC handle
 */
/* MTF_MOCK */
mpool_err_t
mpool_mdc_open(
	struct mpool        *mp,
	uint64_t             logid1,
	uint64_t             logid2,
	uint8_t              flags,
	struct mpool_mdc   **mdc_out);

/**
 * mpool_mdc_close() - Close MDC
 * @mdc: MDC handle
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_close(struct mpool_mdc *mdc);

/**
 * mpool_mdc_sync() - Flush MDC content to media
 * @mdc: MDC handle
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_sync(struct mpool_mdc *mdc);

/**
 * mpool_mdc_rewind() - Rewind MDC to first record
 * @mdc: MDC handle
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_rewind(struct mpool_mdc *mdc);

/**
 * mpool_mdc_read() - Read next record from MDC
 * @mdc:   MDC handle
 * @data:  buffer to receive data
 * @len:   length of supplied buffer
 * @rdlen: number of bytes read
 *
 * Return: If merr_errno() of the return value is EOVERFLOW, then the receive buffer
 *         "data" is too small and must be resized according to the value returned in "rdlen".
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_read(struct mpool_mdc *mdc, void *data, size_t len, size_t *rdlen);

/**
 * mpool_mdc_append() - append record to MDC
 * @mdc:  MDC handle
 * @data: data to write
 * @len:  length of data
 * @sync: flag to defer return until IO is complete
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_append(struct mpool_mdc *mdc, void *data, ssize_t len, bool sync);
/**
 * mpool_mdc_cstart() - Initiate MDC compaction
 * @mdc: MDC handle
 *
 * Swap active (ostensibly full) and inactive (empty) mlogs
 * Append a compaction start marker to newly active mlog
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_cstart(struct mpool_mdc *mdc);

/**
 * mpool_mdc_cend() - End MDC compactions
 * @mdc: MDC handle
 *
 * Append a compaction end marker to the active mlog
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_cend(struct mpool_mdc *mdc);

/**
 * mpool_mdc_usage() - Return estimate of active mlog usage
 * @mdc:   MDC handle
 * @usage: Number of bytes used (includes overhead)
 */
/* MTF_MOCK */
mpool_err_t mpool_mdc_usage(struct mpool_mdc *mdc, size_t *usage);


/******************************** MBLOCK APIs ************************************/

/**
 * mpool_mblock_alloc() - allocate an mblock
 * @mp:      mpool
 * @mclassp: media class
 * @spare:   allocate from spare zones
 * @mbid:    mblock object ID (output)
 * @props:   properties of new mblock (output) - will be returned if the ptr is non-NULL
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t
mpool_mblock_alloc(
	struct mpool           *mp,
	enum mp_media_classp	mclassp,
	bool                    spare,
	uint64_t               *mbid,
	struct mblock_props    *props);

/**
 * mpool_mblock_find() - look up an mblock by object ID
 * @mp:    mpool
 * @objid: mblock object ID
 * @props: mblock properties (returned if the ptr is non-NULL)
 *
 * Return: %0 on success, <%0 on error
 */
mpool_err_t mpool_mblock_find(struct mpool *mp, uint64_t objid, struct mblock_props *props);

/**
 * mpool_mblock_commit() - commit an mblock
 * @mp:   mpool
 * @mbid: mblock object ID
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mblock_commit(struct mpool *mp, uint64_t mbid);

/**
 * mpool_mblock_abort() - abort an mblock
 * @mp:   mpool
 * @mbid: mblock object ID
 *
 * mblock must have been allocated but not yet committed.
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mblock_abort(struct mpool *mp, uint64_t mbid);

/**
 * mpool_mblock_delete() - delete an mblock
 * @mp:   mpool
 * @mbid: mblock object ID
 *
 * mblock must have been allocated but not yet committed.
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mblock_delete(struct mpool *mp, uint64_t mbid);

/**
 * mpool_mblock_props_get() - get properties of an mblock
 * @mp:    mpool
 * @mbid:  mblock ojbect ID
 * @props: properties (output)
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mblock_props_get(struct mpool *mp, uint64_t mbid, struct mblock_props *props);

/**
 * mpool_mblock_write() - write data to an mblock synchronously
 * @mp:      mpool
 * @mbid:    mblock object ID
 * @iov:     iovec containing data to be written
 * @iov_cnt: iovec count
 *
 * Mblock writes are all or nothing.  Either all data is written to media, or
 * no data is written to media.  Hence, return code is success/fail instead of
 * the usual number of bytes written.
 *
 * Return: %0 on success, <%0 on error
 */
/* MTF_MOCK */
mpool_err_t mpool_mblock_write(struct mpool *mp, uint64_t mbid, const struct iovec *iov, int iovc);

/**
 * mpool_mblock_read() - read data from an mblock
 * @mp:      mpool
 * @mbid:    mblock object ID
 * @iov:     iovec for output data
 * @iov_cnt: length of iov[]
 * @offset:  PAGE aligned offset into the mblock
 *
 * Return:
 * * On failure: <%0  - -ERRNO
 * * On success: >=%0 - number of bytes read
 */
/* MTF_MOCK */
mpool_err_t
mpool_mblock_read(struct mpool *mp, uint64_t mbid, const struct iovec *iov, int iovc, off_t offset);


/******************************** MCACHE APIs ************************************/


/**
 * mpool_mcache_madvise() - Give advice about use of memory
 * @map:    mcache map handle
 * @mbidx:  logical mblock number in mcache map
 * @offset: offset into the mblock specified by mbidx
 * @length: see madvise(2)
 * @advice: see madvise(2)
 *
 * Like madvise(2), but for mcache maps.
 *
 * Note that one can address the entire map (including holes) by
 * specifying zero for %mbidx, zero for %offset, and %SIZE_MAX for
 * %length.  In general, %SIZE_MAX may always be specified for %length,
 * in which case it addresses the map from the given mbidx based offset
 * to the end of the map.
 */
/* MTF_MOCK */
mpool_err_t
mpool_mcache_madvise(
	struct mpool_mcache_map    *map,
	uint                        mbidx,
	off_t                       offset,
	size_t                      length,
	int                         advice);

/**
 * mpool_mcache_purge() - Purge map
 * @map: mcache map handle
 * @mp:  mp mpool
 */
/* MTF_MOCK */
mpool_err_t mpool_mcache_purge(struct mpool_mcache_map *map, const struct mpool *mp);

/**
 * mpool_mcache_mincore() - Get VSS and RSS for the mcache map
 * @map:  mcache map handle
 * @mp:   mpool handle
 * @rssp: ptr to count of resident pages in the map
 * @vssp: ptr to count of virtual pages in the map
 *
 * Get the virtual and resident set sizes (in pages count)
 * for the given mcache map.
 */
/* MTF_MOCK */
mpool_err_t
mpool_mcache_mincore(
	struct mpool_mcache_map    *map,
	const struct mpool         *mp,
	size_t                     *rssp,
	size_t                     *vssp);

/**
 * mpool_mcache_getbase() - Get the base address of a memory-mapped mblock in an mcache map
 * @map:   mcache map handle
 * @mbidx: mcache map mblock index
 *
 * If the pages of an mcache map are contiguous in memory (as is the case in
 * user-space), return the the base address of the mapped mblock.  If the
 * pages are not contiguous, return NULL.
 */
/* MTF_MOCK */
void *mpool_mcache_getbase(struct mpool_mcache_map *map, const uint mbidx);


/**
 * mpool_mcache_getpages() - Get a vector of pages from a single mblock
 * @map:        mcache map handle
 * @pagec:      page count (len of @pagev array)
 * @mbidx:      mcache map mblock index
 * @offsetv:    vector of page offsets into objects/mblocks
 * @pagev:      vector of pointers to pages (output)
 *
 * mbidx is an index into the mbidv[] vector that was given
 * to mpool_mcache_create().
 *
 * Return: %0 on success, mpool_err_t on failure
 */
/* MTF_MOCK */
mpool_err_t
mpool_mcache_getpages(
	struct mpool_mcache_map    *map,
	const uint                  pagec,
	const uint                  mbidx,
	const off_t                 offsetv[],
	void                       *pagev[]);

/**
 * mpool_mcache_mmap() - Create an mcache map
 * @mp:     handle for the mpool
 * @mbidc:  mblock ID count
 * @mbidv:  vector of mblock IDs
 * @advice:
 * @mapp:   pointer to (opaque) mpool_mcache_map ptr
 *
 * Create an mcache map for the list of given mblock IDs
 * and returns a handle to it via *mapp.
 */
/* MTF_MOCK */
mpool_err_t
mpool_mcache_mmap(
	struct mpool               *mp,
	size_t                      mbidc,
	uint64_t                   *mbidv,
	enum mpc_vma_advice         advice,
	struct mpool_mcache_map    **mapp);

/**
 * mpool_mcache_munmap() - munmap an mcache mmap
 * @map:
 */
/* MTF_MOCK */
mpool_err_t mpool_mcache_munmap(struct mpool_mcache_map *map);


/******************************** ERROR MGMT APIs ************************************/


/**
 * mpool_strerror() - Format errno description from mpool_err_t
 * @err:
 * @buf:
 * @bufsz:
 */
char *mpool_strerror(mpool_err_t err, char *buf, size_t bufsz);

/**
 * mpool_strinfo() - Format file, line, and errno from mpool_err_t
 * @err:
 * @buf:
 * @bufsz:
 */
char *mpool_strinfo(mpool_err_t err, char *buf, size_t bufsz);

/**
 * mpool_errno() -
 * @err:
 */
int mpool_errno(mpool_err_t err);

#if defined(HSE_UNIT_TEST_MODE) && HSE_UNIT_TEST_MODE == 1
#include "mpool_ut.h"
#endif /* HSE_UNIT_TEST_MODE */

#endif /* MPOOL_H */
