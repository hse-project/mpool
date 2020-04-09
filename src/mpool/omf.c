// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */
/*
 * Pool on-drive format (omf) module.
 *
 * Defines:
 * + on-drive format for mpool superblocks
 * + on-drive formats for mlogs, mblocks, and metadata containers (mdc)
 * + utility functions for working with these on-drive formats
 *
 * All mpool metadata is versioned and stored on media in little-endian format.
 *
 */
#include <util/platform.h>

#include "mpcore_defs.h"


bool omf_sb_has_magic_le(const char *inbuf)
{
	struct sb_descriptor_omf   *sb_omf;

	u64    magic;

	sb_omf = (struct sb_descriptor_omf *)inbuf;
	magic = omf_psb_magic(sb_omf);

	return magic == OMF_SB_MAGIC;
}


/*
 * logblock_header
 */
bool omf_logblock_empty_le(char *lbuf)
{
	bool   ret_val = true;
	int    i       = 0;

	for (i = 0; i < OMF_LOGBLOCK_HDR_PACKLEN; i++) {
		if (0 != (u8)lbuf[i]) {
			ret_val = false;
			break;
		}
	}

	return ret_val;
}

merr_t
omf_logblock_header_pack_htole(
	struct omf_logblock_header *lbh,
	char                       *outbuf)
{
	struct logblock_header_omf *lbh_omf;

	lbh_omf = (struct logblock_header_omf *)outbuf;

	if (lbh->olh_vers != OMF_LOGBLOCK_VERS)
		return merr(EINVAL);

	omf_set_polh_vers(lbh_omf, lbh->olh_vers);
	omf_set_polh_magic(lbh_omf, lbh->olh_magic.uuid, MPOOL_UUID_SIZE);
	omf_set_polh_gen(lbh_omf, lbh->olh_gen);
	omf_set_polh_pfsetid(lbh_omf, lbh->olh_pfsetid);
	omf_set_polh_cfsetid(lbh_omf, lbh->olh_cfsetid);

	return 0;
}

merr_t
omf_logblock_header_unpack_letoh(
	struct omf_logblock_header *lbh,
	const char                 *inbuf)
{
	struct logblock_header_omf *lbh_omf;

	lbh_omf = (struct logblock_header_omf *)inbuf;

	lbh->olh_vers    = omf_polh_vers(lbh_omf);
	omf_polh_magic(lbh_omf, lbh->olh_magic.uuid, MPOOL_UUID_SIZE);
	lbh->olh_gen     = omf_polh_gen(lbh_omf);
	lbh->olh_pfsetid = omf_polh_pfsetid(lbh_omf);
	lbh->olh_cfsetid = omf_polh_cfsetid(lbh_omf);

	return 0;
}

int omf_logblock_header_len_le(char *lbuf)
{
	struct logblock_header_omf *lbh_omf;

	lbh_omf = (struct logblock_header_omf *)lbuf;

	if (omf_polh_vers(lbh_omf) == OMF_LOGBLOCK_VERS)
		return OMF_LOGBLOCK_HDR_PACKLEN;

	return -EINVAL;
}


/*
 * logrec_descriptor
 */
static bool logrec_type_valid(enum logrec_type_omf rtype)
{
	return rtype <= OMF_LOGREC_CEND;
}

bool logrec_type_datarec(enum logrec_type_omf rtype)
{
	return rtype && rtype <= OMF_LOGREC_DATALAST;
}

merr_t
omf_logrec_desc_pack_htole(
	struct omf_logrec_descriptor   *lrd,
	char                           *outbuf)
{
	struct logrec_descriptor_omf   *lrd_omf;

	if (logrec_type_valid(lrd->olr_rtype)) {

		lrd_omf = (struct logrec_descriptor_omf *)outbuf;
		omf_set_polr_tlen(lrd_omf, lrd->olr_tlen);
		omf_set_polr_rlen(lrd_omf, lrd->olr_rlen);
		omf_set_polr_rtype(lrd_omf, lrd->olr_rtype);

		return 0;
	}

	return merr(EINVAL);
}

void
omf_logrec_desc_unpack_letoh(
	struct omf_logrec_descriptor   *lrd,
	const char                     *inbuf)
{
	struct logrec_descriptor_omf   *lrd_omf;

	lrd_omf = (struct logrec_descriptor_omf *)inbuf;
	lrd->olr_tlen  = omf_polr_tlen(lrd_omf);
	lrd->olr_rlen  = omf_polr_rlen(lrd_omf);
	lrd->olr_rtype = omf_polr_rtype(lrd_omf);
}
