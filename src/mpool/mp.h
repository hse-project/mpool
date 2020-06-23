/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_MPOOL_MPOOL_PRIV_H
#define MPOOL_MPOOL_MPOOL_PRIV_H

/**
 * DOC: Module info
 *
 * Media pool (mpool) manager module.
 *
 * Defines functions to create and maintain mpools comprising multiple drives
 * in multiple media classes used for storing mblocks and mlogs.
 *
 */

#include <util/platform.h>
#include <util/rwsem.h>

#include <mpctl/imdc.h>
#include <mpcore/mpcore.h>

/**
 * struct mpool_dev_info - Pool drive state, status, and params
 * @pdi_parm:     drive parms
 * @pdi_status:   enum prx_pd_status value: drive status
 * @pdi_state:    drive state
 * @pdi_name:     device name (only the last path name component)
 */
struct mpool_dev_info {
	struct pd_dev_parm      pdi_parm;
	atomic_t                pdi_status; /* Barriers or acq/rel required */
	enum pd_state_omf       pdi_state;
	char                    pdi_name[PD_NAME_LEN_MAX];
};

/* Shortcuts */
#define pdi_didstr    pdi_parm.dpr_prop.pdp_didstr
#define pdi_zonepg    pdi_parm.dpr_prop.pdp_zparam.dvb_zonepg
#define pdi_zonetot   pdi_parm.dpr_prop.pdp_zparam.dvb_zonetot
#define pdi_devtype   pdi_parm.dpr_prop.pdp_devtype
#define pdi_cmdopt    pdi_parm.dpr_prop.pdp_cmdopt
#define pdi_mclass    pdi_parm.dpr_prop.pdp_mclassp
#define pdi_devsz     pdi_parm.dpr_prop.pdp_devsz
#define pdi_sectorsz  pdi_parm.dpr_prop.pdp_sectorsz
#define pdi_prop      pdi_parm.dpr_prop

/**
 * struct mpool_descriptor - Media pool descriptor
 * @pds_pdv:      per drive info array
 * @pds_name:
 */
struct mpool_descriptor {
	struct mpool_dev_info   *pds_pdv;
	char                     pds_name[MPOOL_NAME_LEN_MAX];
};

/**
 * mpool_pd_status_get() -
 * @pd:
 *
 * Return: status of pool disk.
 */
enum prx_pd_status mpool_pd_status_get(struct mpool_dev_info *pd);

/**
 * mpool_pd_status_set() -
 * @pd:
 * @status:
 *
 */
void mpool_pd_status_set(struct mpool_dev_info *pd, enum prx_pd_status status);

#endif /* MPOOL_MPOOL_MPOOL_PRIV_H */
