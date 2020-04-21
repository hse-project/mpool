/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */
/*
 *
 * Pool on-drive format (omf) module.
 *
 * Defines:
 * + on-drive format for mpool superblocks
 * + on-drive formats for mlogs, mblocks, and metadata containers (mdc)
 * + utility functions for working with these on-drive formats
 * That includes structures and enums used by the on-drive format.
 *
 * All mpool metadata is versioned and stored on media in little-endian format.
 *
 * Naming conventions:
 * -------------------
 * The name of the structures ends with _omf
 * The name of the structure members start with a "p" that means "packed".
 */

#ifndef MPCORE_OMF_H
#define MPCORE_OMF_H

#include <util/platform.h>
#include <util/omf.h>

/*
 * mlog structure:
 * + An mlog comprises a consecutive sequence of log blocks,
 *   where each log block is a single page within a zone
 * + A log block comprises a header and a consecutive sequence of records
 * + A record is a typed blob
 *
 * Log block headers must be versioned. Log block records do not
 * require version numbers because they are typed and new types can
 * always be added.
 */

/**
 * Log block format -- version 1
 *
 * log block := header record+ eolb? trailer?
 *
 * header := struct omf_logblock_header where vers=2
 *
 * record := lrd byte*
 *
 * lrd := struct omf_logrec_descriptor with value
 *   (<record length>, <chunk length>, enum logrec_type_omf value)
 *
 * eolb (end of log block marker) := struct omf_logrec_descriptor with value
 *   (0, 0, enum logrec_type_omf.EOLB/0)
 *
 * trailer := zero bytes from end of last log block record to end of log block
 *
 * OMF_LOGREC_CEND must be the max. value for this enum.
 */
/*
 *  enum logrec_type_omf -
 *
 *  A log record type of 0 signifies EOLB. This is really the start of the
 *  trailer but this simplifies parsing for partially filled log blocks.
 *  DATAFIRST, -MID, -LAST types are used for chunking logical data records.
 *
 *  @OMF_LOGREC_EOLB:      end of log block marker (start of trailer)
 *  @OMF_LOGREC_DATAFULL:  data record; contains all specified data
 *  @OMF_LOGREC_DATAFIRST: data record; contains first part of specified data
 *  @OMF_LOGREC_DATAMID:   data record; contains interior part of data
 *  @OMF_LOGREC_DATALAST:  data record; contains final part of specified data
 *  @OMF_LOGREC_CSTART:    compaction start marker
 *  @OMF_LOGREC_CEND:      compaction end marker
 */
enum logrec_type_omf {
	OMF_LOGREC_EOLB      = 0,
	OMF_LOGREC_DATAFULL  = 1,
	OMF_LOGREC_DATAFIRST = 2,
	OMF_LOGREC_DATAMID   = 3,
	OMF_LOGREC_DATALAST  = 4,
	OMF_LOGREC_CSTART    = 5,
	OMF_LOGREC_CEND      = 6,
};


/**
 * struct logrec_descriptor_omf -
 * "polr_" = packed omf logrec descriptor
 *
 * @polr_tlen:  logical length of data record (all chunks)
 * @polr_rlen:  length of data chunk in this log record
 * @polr_rtype: enum logrec_type_omf value
 */
struct logrec_descriptor_omf {
	__le32 polr_tlen;
	__le16 polr_rlen;
	u8     polr_rtype;
	u8     polr_pad;
} __packed;

/* Define set/get methods for logrec_descriptor_omf */
OMF_SETGET(struct logrec_descriptor_omf, polr_tlen, 32)
OMF_SETGET(struct logrec_descriptor_omf, polr_rlen, 16)
OMF_SETGET(struct logrec_descriptor_omf, polr_rtype, 8)
#define OMF_LOGREC_DESC_PACKLEN (sizeof(struct logrec_descriptor_omf))
#define OMF_LOGREC_DESC_RLENMAX 65535


#define OMF_UUID_PACKLEN     16
#define OMF_LOGBLOCK_VERS    1

/**
 * struct logblock_header_omf - for all versions
 * "polh_" = packed omf logblock header
 *
 * @polh_vers:    log block hdr version, offset 0 in all vers
 * @polh_magic:   unique magic per mlog
 * @polh_pfsetid: flush set ID of the previous log block
 * @polh_cfsetid: flush set ID this log block belongs to
 * @polh_gen:     generation number
 */
struct logblock_header_omf {
	__le16 polh_vers;
	u8     polh_magic[OMF_UUID_PACKLEN];
	u8     polh_pad[6];
	__le32 polh_pfsetid;
	__le32 polh_cfsetid;
	__le64 polh_gen;
} __packed;

/* Define set/get methods for logblock_header_omf */
OMF_SETGET(struct logblock_header_omf, polh_vers, 16)
OMF_SETGET_CHBUF(struct logblock_header_omf, polh_magic)
OMF_SETGET(struct logblock_header_omf, polh_pfsetid, 32)
OMF_SETGET(struct logblock_header_omf, polh_cfsetid, 32)
OMF_SETGET(struct logblock_header_omf, polh_gen, 64)

/* On-media log block header length */
#define OMF_LOGBLOCK_HDR_PACKLEN (sizeof(struct logblock_header_omf))

/**
 * Object types embedded in opaque uint64 object ids by the pmd module.
 * This encoding is also present in the object ids stored in the
 * data records on media.
 *
 * The obj_type field is 4 bits. There are two valid obj types.
 */
enum obj_type_omf {
	OMF_OBJ_UNDEF       = 0,
	OMF_OBJ_MBLOCK      = 1,
	OMF_OBJ_MLOG        = 2,
};


/**
 * sb_descriptor_ver_omf - Mpool super block version
 * @OMF_SB_DESC_UNDEF: value not on media
 */
enum sb_descriptor_ver_omf {
	OMF_SB_DESC_UNDEF        = 0,
	OMF_SB_DESC_V1           = 1,

};

#define OMF_MPOOL_NAME_LEN     32

/*
 * struct sb_descriptor_omf - super block descriptor format version 1.
 * "psb_" = packed super block
 *
 * Note: these fields, up to and including psb_cksum1, are known to libblkid.
 * cannot change them without havoc. Fields from psb_magic to psb_cksum1
 * included are at same offset in all versions.
 *
 * @psb_magic:  mpool magic value; offset 0 in all vers
 * @psb_name:   mpool name
 * @psb_poolid: UUID of pool this drive belongs to
 * @psb_vers:   sb format version; offset 56
 * @psb_gen:    sb generation number on this drive
 * @psb_cksum1: checksum of all fields above
 */
struct sb_descriptor_omf {
	__le64                         psb_magic;
	u8                             psb_name[OMF_MPOOL_NAME_LEN];
	u8                             psb_poolid[OMF_UUID_PACKLEN];
	__le16                         psb_vers;
	__le32                         psb_gen;
	u8                             psb_cksum1[4];
} __packed;

OMF_SETGET(struct sb_descriptor_omf, psb_magic, 64)
OMF_SETGET_CHBUF(struct sb_descriptor_omf, psb_name)
OMF_SETGET_CHBUF(struct sb_descriptor_omf, psb_poolid)
OMF_SETGET(struct sb_descriptor_omf, psb_vers, 16)
OMF_SETGET(struct sb_descriptor_omf, psb_gen, 32)
OMF_SETGET_CHBUF(struct sb_descriptor_omf, psb_cksum1)
#define OMF_SB_DESC_PACKLEN (sizeof(struct sb_descriptor_omf))

#endif /* MPCORE_OMF_H */
