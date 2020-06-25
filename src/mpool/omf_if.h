/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_OMF_IF_PRIV_H
#define MPOOL_MPOOL_OMF_IF_PRIV_H

#include <util/platform.h>
#include <util/omf.h>

struct pmd_layout;

/*
 * Common defs: versioned via version number field of enclosing structs
 */

/*
 * Superblock (sb) -- version 1
 *
 * Note this is 8-byte-wide reversed to get correct ascii order
 */
#define OMF_SB_MAGIC  0x7665446c6f6f706dULL  /* ASCII mpoolDev - no null */

/*
 * struct omf_sb_descriptor - version 1 superblock descriptor
 *
 * @osb_magic:  mpool magic value
 * @osb_name:   mpool name, contains a terminating 0 byte
 * @osb_cktype: enum mp_cksum_type value
 * @osb_vers:   sb format version
 * @osb_poolid: UUID of pool this drive belongs to
 * @osb_gen:    sb generation number on this drive
 */
struct omf_sb_descriptor {
	u64                            osb_magic;
	u8                             osb_name[MPOOL_NAMESZ_MAX];
	u8                             osb_cktype;
	u16                            osb_vers;
	struct mpool_uuid              osb_poolid;
	u32                            osb_gen;
};

/* struct omf_logrec_descriptor-
 *
 * @olr_tlen:  logical length of data record (all chunks)
 * @olr_rlen:  length of data chunk in this log record
 * @olr_rtype: enum logrec_type_omf value
 *
 */
struct omf_logrec_descriptor {
	u32    olr_tlen;
	u16    olr_rlen;
	u8     olr_rtype;
};

/*
 * struct omf_logblock_header-
 *
 * @olh_magic:   unique ID per mlog
 * @olh_pfsetid: flush set ID of the previous log block
 * @olh_cfsetid: flush set ID this log block
 * @olh_gen:     generation number
 * @olh_vers:    log block format version
 */
struct omf_logblock_header {
	struct mpool_uuid  olh_magic;
	u32                olh_pfsetid;
	u32                olh_cfsetid;
	u64                olh_gen;
	u16                olh_vers;
};

/**
 * objid_type()
 *
 * Return the type field from an objid.  Retuned as int, so it can also be
 * used for handles, which have the OMF_OBJ_UHANDLE bit set in addition to
 * a type.
 */
static inline int objid_type(u64 objid)
{
	return ((objid & 0xF00) >> 8);
}

static inline bool objtype_valid(enum obj_type_omf otype)
{
	return otype && (otype <= 2);
}

/*
 * omf API functions -- exported functions for working with omf structures
 */

/**
 * omf_sb_has_magic_le() - Determine if buffer has superblock magic value
 * @inbuf: char *
 *
 * Determine if little-endian buffer inbuf has superblock magic value
 * where expected; does NOT imply inbuf is a valid superblock.
 *
 * Return: 1 if true; 0 otherwise
 */
bool omf_sb_has_magic_le(const char *inbuf);

/**
 * omf_logblock_empty_le() - Determine if log block is empty
 * @lbuf: char *
 *
 * Check little-endian log block in lbuf to see if empty (unwritten).
 *
 * Return: 1 if log block is empty; 0 otherwise
 */
bool omf_logblock_empty_le(char *lbuf);

/**
 * omf_logblock_header_pack_htole() - pack log block header
 * @lbh: struct omf_logblock_header *
 * @outbuf: char *
 *
 * Pack header into little-endian log block buffer lbuf, ex-checksum.
 *
 * Return: 0 if successful, merr_t otherwise
 */
merr_t omf_logblock_header_pack_htole(struct omf_logblock_header *lbh, char *lbuf);

/**
 * omf_logblock_header_len_le() - Determine header length of log block
 * @lbuf: char *
 *
 * Check little-endian log block in lbuf to determine header length.
 *
 * Return: bytes in packed header; -EINVAL if invalid header vers
 */
int omf_logblock_header_len_le(char *lbuf);

/**
 * omf_logblock_header_unpack_letoh() - unpack log block header
 * @lbh: struct omf_logblock_header *
 * @inbuf: char *
 *
 * Unpack little-endian log block header from lbuf into lbh; does not
 * verify checksum.
 *
 * Return: 0 if successful, merr_t (EINVAL) if invalid log block header vers
 */
merr_t omf_logblock_header_unpack_letoh(struct omf_logblock_header *lbh, const char *inbuf);

/**
 * omf_logrec_desc_pack_htole() - pack log record descriptor
 * @lrd: struct omf_logrec_descriptor *
 * @outbuf: char *
 *
 * Pack log record descriptor into outbuf little-endian.
 *
 * Return: 0 if successful, merr_t (EINVAL) if invalid log rec type
 */
merr_t omf_logrec_desc_pack_htole(struct omf_logrec_descriptor *lrd, char *outbuf);

/**
 * omf_logrec_desc_unpack_letoh() - unpack log record descriptor
 * @lrd: struct omf_logrec_descriptor *
 * @inbuf: char *
 *
 * Unpack little-endian log record descriptor from inbuf into lrd.
 */
void omf_logrec_desc_unpack_letoh(struct omf_logrec_descriptor *lrd, const char *inbuf);

/**
 * logrec_type_datarec() - data record or not
 * @rtype:
 *
 * Return: true if the log record type is related to a data record.
 */
bool logrec_type_datarec(enum logrec_type_omf rtype);

#endif /* MPOOL_MPOOL_OMF_IF_PRIV_H */
