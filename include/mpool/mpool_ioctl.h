/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_IOCTL_H
#define MPOOL_IOCTL_H

#ifdef __KERNEL__
#include <linux/uuid.h>
#include <linux/uio.h>
#else
#include <stdint.h>
#include <stdbool.h>
#include <uuid/uuid.h>
#include <sys/uio.h>
#include <sys/types.h>
typedef uuid_t uuid_le;
#endif

#ifndef __user
#define __user
#endif

/*
 * Maximum name lengths including NUL terminator.  Note that the maximum
 * mpool name length is baked into libblkid and may not be changed here.
 */
#define MPOOL_NAMESZ_MAX            32
#define MPOOL_LABELSZ_MAX           64
#define PD_NAMESZ_MAX               32

#define MPC_DEV_SUBDIR              "mpool"
#define MPC_DEV_CTLNAME             MPC_DEV_SUBDIR "ctl"
#define MPC_DEV_CTLPATH             "/dev/" MPC_DEV_CTLNAME

#define MPOOL_LABEL_INVALID         ""
#define MPOOL_LABEL_DEFAULT         "raw"

#define MPOOL_RA_PAGES_INVALID      U32_MAX
#define MPOOL_RA_PAGES_MAX          ((128 * 1024) / PAGE_SIZE)

#define MPOOL_MCLASS_INVALID        MP_MED_INVALID
#define MPOOL_MCLASS_DEFAULT        MP_MED_CAPACITY

#define MPOOL_SPARES_INVALID        U8_MAX
#define MPOOL_SPARES_DEFAULT        5

#define MPOOL_ROOT_LOG_CAP          (8 * 1024 * 1024)

#define MPOOL_MBSIZE_MB_DEFAULT     32

#define MPOOL_MDCNUM_DEFAULT        16

/*
 * MPOOL struct definitions used by the ioctl commands.
 */

/**
 * mp_mgmt_flags - Mpool Management Flags
 * @MP_FLAGS_FORCE:
 * @MP_PERMIT_META_CONV: permit mpool metadata conversion. That is, allow the
 *	mpool activate to write back the mpool metadata to the latest version
 *	used by the binary activating the mpool.
 * @MP_FLAGS_RESIZE: Resize mpool
 */
enum mp_mgmt_flags {
	MP_FLAGS_FORCE,
	MP_FLAGS_PERMIT_META_CONV,
	MP_FLAGS_RESIZE,
};

/**
 * mp_media_classp = Media classes
 *
 * @MP_MED_STAGING:  Initial data ingest, hot data storage, or similar.
 * @MP_MED_CAPACITY: Primary data storage, cold data, or similar.
 */
enum mp_media_classp {
	MP_MED_STAGING   = 0,
	MP_MED_CAPACITY  = 1,
};

#define MP_MED_BASE        MP_MED_STAGING
#define MP_MED_NUMBER      (MP_MED_CAPACITY + 1)
#define MP_MED_INVALID     U8_MAX

/**
 * struct mpool_devprops -
 * @pdp_devid:   UUID for drive
 * @pdp_mclassp: enum mp_media_classp
 * @pdp_status:  enum pd_status
 * @pdp_total:   raw capacity of drive
 * @pdp_avail:   available capacity (total - bad zones) of drive
 * @pdp_spare:   spare capacity of drive
 * @pdp_fspare:  free spare capacity of drive
 * @pdp_usable:  usable capacity of drive
 * @pdp_fusable: free usable capacity of drive
 * @pdp_used:    used capacity of drive:
 */
struct mpool_devprops {
	uuid_le    pdp_devid;
	uint8_t    pdp_mclassp;
	uint8_t    pdp_status;
	uint8_t    pdp_rsvd1[6];
	uint64_t   pdp_total;
	uint64_t   pdp_avail;
	uint64_t   pdp_spare;
	uint64_t   pdp_fspare;
	uint64_t   pdp_usable;
	uint64_t   pdp_fusable;
	uint64_t   pdp_used;
	uint64_t   pdp_rsvd2;
};

/**
 * struct mpool_params -
 * @mp_poolid:          UUID of mpool
 * @mp_type:            user-specified identifier
 * @mp_uid:
 * @mp_gid:
 * @mp_mode:
 * @mp_stat:            overall mpool status (enum mpool_status)
 * @mp_mdc_captgt:      user MDC capacity
 * @mp_oidv:            user MDC OIDs
 * @mp_ra_pages_max:    max VMA map readahead pages
 * @mp_vma_size_max:    max VMA map size (log2)
 * @mp_mblocksz:        mblock size by media class (MiB)
 * @mp_utype:           user-defined type
 * @mp_label:           user specified label
 * @mp_name:            mpool name (2x for planned expansion)
 */
struct mpool_params {
	uuid_le     mp_poolid;
	uid_t       mp_uid;
	gid_t       mp_gid;
	mode_t      mp_mode;
	uint8_t     mp_stat;
	uint8_t     mp_spare_cap;
	uint8_t     mp_spare_stg;
	uint8_t     mp_rsvd0;
	uint64_t    mp_mdc_captgt;
	uint64_t    mp_oidv[2];
	uint32_t    mp_ra_pages_max;
	uint32_t    mp_vma_size_max;
	uint32_t    mp_mblocksz[MP_MED_NUMBER];
	uint16_t    mp_mdc0cap;
	uint16_t    mp_mdcncap;
	uint16_t    mp_mdcnum;
	uint16_t    mp_rsvd1;
	uint32_t    mp_rsvd2;
	uint64_t    mp_rsvd3;
	uint64_t    mp_rsvd4;
	uuid_le     mp_utype;
	char        mp_label[MPOOL_LABELSZ_MAX];
	char        mp_name[MPOOL_NAMESZ_MAX * 2];
};

/**
 * struct mpool_usage - in bytes
 * @mpu_total:   raw capacity for all drives
 * @mpu_usable:  usable capacity for all drives
 * @mpu_fusable: free usable capacity for all drives
 * @mpu_used:    used capacity for all drives; possible for
 *               used > usable when fusable=0; see smap
 *               module for details
 * @mpu_spare:   total spare space
 * @mpu_fspare:  free spare space
 *
 * @mpu_mblock_alen: mblock allocated length
 * @mpu_mblock_wlen: mblock written length
 * @mpu_mlog_alen:   mlog allocated length
 * @mpu_mblock_cnt:  number of active mblocks
 * @mpu_mlog_cnt:    number of active mlogs
 */
struct mpool_usage {
	uint64_t   mpu_total;
	uint64_t   mpu_usable;
	uint64_t   mpu_fusable;
	uint64_t   mpu_used;
	uint64_t   mpu_spare;
	uint64_t   mpu_fspare;

	uint64_t   mpu_alen;
	uint64_t   mpu_wlen;
	uint64_t   mpu_mblock_alen;
	uint64_t   mpu_mblock_wlen;
	uint64_t   mpu_mlog_alen;
	uint32_t   mpu_mblock_cnt;
	uint32_t   mpu_mlog_cnt;
};

/**
 * mpool_mclass_xprops -
 * @mc_devtype: type of devices in the media class
 *                  (enum pd_devtype)
 * @mc_mclass: media class (enum mp_media_classp)
 * @mc_sectorsz: media class (enum mp_media_classp)
 * @mc_spare: percent spare zones for drives
 * @mc_uacnt: UNAVAIL status drive count
 * @mc_zonepg: pages per zone
 * @mc_features: feature bitmask
 * @mc_usage: feature bitmask
 */
struct mpool_mclass_xprops {
	uint8_t                    mc_devtype;
	uint8_t                    mc_mclass;
	uint8_t                    mc_sectorsz;
	uint8_t                    mc_rsvd1;
	uint32_t                   mc_spare;
	uint16_t                   mc_uacnt;
	uint16_t                   mc_rsvd2;
	uint32_t                   mc_zonepg;
	uint64_t                   mc_features;
	uint64_t                   mc_rsvd3;
	struct mpool_usage         mc_usage;
};

/**
 * mpool_mclass_props -
 *
 * @mc_mblocksz:   mblock size in MiB
 * @mc_rsvd:       reserved struct field (for future use)
 * @mc_total:      total space in the media class (mc_usable + mc_spare)
 * @mc_usable:     usable space in bytes
 * @mc_used:       bytes allocated from usable space
 * @mc_spare:      spare space in bytes
 * @mc_spare_used: bytes allocated from spare space
 */
struct mpool_mclass_props {
	uint32_t   mc_mblocksz;
	uint32_t   mc_rsvd;
	uint64_t   mc_total;
	uint64_t   mc_usable;
	uint64_t   mc_used;
	uint64_t   mc_spare;
	uint64_t   mc_spare_used;
};

/**
 * struct mpool_xprops - Extended mpool properties
 * @ppx_params: mpool configuration parameters
 * @ppx_drive_spares: percent spare zones for drives in each media class
 * @ppx_uacnt:  UNAVAIL status drive count in each media class
 */
struct mpool_xprops {
	struct mpool_params     ppx_params;
	uint8_t                 ppx_rsvd[MP_MED_NUMBER];
	uint8_t                 ppx_drive_spares[MP_MED_NUMBER];
	uint16_t                ppx_uacnt[MP_MED_NUMBER];
	uint32_t                ppx_pd_mclassv[MP_MED_NUMBER];
	char                    ppx_pd_namev[MP_MED_NUMBER][PD_NAMESZ_MAX];
};

/*
 * mblock struct definitions used by the ioctl commands.
 */

/*
 * struct mblock_props -
 *
 * @mpr_objid:        mblock identifier
 * @mpr_alloc_cap:    allocated capacity in bytes
 * @mpr_write_len:    written user-data in bytes
 * @mpr_optimal_wrsz: optimal write size(in bytes) for all but the last incremental mblock write
 * @mpr_mclassp:      media class
 * @mpr_iscommitted:  Is this mblock committed?
 */
struct mblock_props {
	uint64_t                mpr_objid;
	uint32_t                mpr_alloc_cap;
	uint32_t                mpr_write_len;
	uint32_t                mpr_optimal_wrsz;
	uint32_t                mpr_mclassp; /* enum mp_media_classp */
	uint8_t                 mpr_iscommitted;
	uint8_t                 mpr_rsvd1[7];
	uint64_t                mpr_rsvd2;
};

struct mblock_props_ex {
	struct mblock_props     mbx_props;
	uint8_t                 mbx_zonecnt;      /* zone count per strip */
	uint8_t                 mbx_rsvd1[7];
	uint64_t                mbx_rsvd2;
};

/*
 * mlog struct definitions used by the ioctl commands.
 *
 * enum mlog_open_flags -
 * @MLOG_OF_COMPACT_SEM: Enforce compaction semantics
 * @MLOG_OF_SKIP_SER:    Appends and reads are guaranteed to be serialized
 *                       outside of the mlog API
 */
enum mlog_open_flags {
	MLOG_OF_COMPACT_SEM = 0x1,
	MLOG_OF_SKIP_SER    = 0x2,
};

/*
 * NOTE:
 * + a value of 0 for targets (*tgt) means no specific target and the
 *   allocator is free to choose based on media class configuration
 */
struct mlog_capacity {
	uint64_t    lcp_captgt;       /* capacity target for mlog in bytes */
	uint8_t     lcp_spare;        /* true if alloc mlog from spare space */
	uint8_t     lcp_rsvd1[7];
};

/*
 * struct mlog_props -
 *
 * @lpr_uuid:        UUID or mlog magic
 * @lpr_objid:       mlog identifier
 * @lpr_alloc_cap:   maximum capacity in bytes
 * @lpr_gen:         generation no. (user mlogs)
 * @lpr_mclassp:     media class
 * @lpr_iscommitted: Is this mlog committed?
 */
struct mlog_props {
	uuid_le     lpr_uuid;
	uint64_t    lpr_objid;
	uint64_t    lpr_alloc_cap;
	uint64_t    lpr_gen;
	uint8_t     lpr_mclassp;
	uint8_t     lpr_iscommitted;
	uint8_t     lpr_rsvd1[6];
	uint64_t    lpr_rsvd2;
};

/*
 * struct mlog_props_ex -
 *
 * @lpx_props:
 * @lpx_totsec:   total number of sectors
 * @lpx_zonecnt:   zone count per strip
 * @lpx_state:    mlog layout state
 * @lpx_secshift: sector shift
 */
struct mlog_props_ex {
	struct mlog_props   lpx_props;
	uint32_t            lpx_totsec;
	uint32_t            lpx_zonecnt;
	uint8_t             lpx_state;
	uint8_t             lpx_secshift;
	uint8_t             lpx_rsvd1[6];
	uint64_t            lpx_rsvd2;
};

/**
 * enum mdc_open_flags -
 * @MDC_OF_SKIP_SER: appends and reads are guaranteed to be serialized
 *                   outside of the MDC API
 */
enum mdc_open_flags {
	MDC_OF_SKIP_SER  = 0x1,
};

/**
 * struct mdc_capacity -
 * @mdt_captgt: capacity target for mlog in bytes
 * @mpt_spare:  true if alloc MDC from spare space
 */
struct mdc_capacity {
	uint64_t   mdt_captgt;
	bool       mdt_spare;
};

/**
 * struct mdc_props -
 * @mdc_objid1:
 * @mdc_objid2:
 * @mdc_alloc_cap:
 * @mdc_mclassp:
 */
struct mdc_props {
	uint64_t               mdc_objid1;
	uint64_t               mdc_objid2;
	uint64_t               mdc_alloc_cap;
	enum mp_media_classp   mdc_mclassp;
};

/*
 * mcache struct definitions used by the ioctl commands.
 */

/**
 * mpc_vma_advice -
 * @MPC_VMA_COLD:
 * @MPC_VMA_WARM:
 * @MPC_VMA_HOT:
 * @MPC_VMA_PINNED:
 */
enum mpc_vma_advice {
	MPC_VMA_COLD = 0,
	MPC_VMA_WARM,
	MPC_VMA_HOT,
	MPC_VMA_PINNED
};

/*
 * Drive properties used by the ioctl commands.
 */

/**
 * struct pd_znparam - zone parameter arg used in compute/set API functions
 * @dvb_zonepg:     zone size in PAGE_SIZE units.
 * @dvb_zonetot:    total number of zones
 */
struct pd_znparam {
	uint32_t   dvb_zonepg;
	uint32_t   dvb_zonetot;
	uint64_t   dvb_rsvd1;
};

#define PD_DEV_ID_LEN              64

/**
 * struct pd_prop - PD properties
 * @pdp_didstr:         drive id string (model)
 * @pdp_devtype:	device type (enum pd_devtype)
 * @pdp_phys_if:	physical interface of the drive
 *			Determined by the device discovery.
 *			(device_phys_if)
 * @pdp_mclassp:        performance characteristic of the media class
 *			Determined by the user, not by the device discovery.
 *			(enum mp_media_classp)
 * @pdp_cmdopt:         enum pd_cmd_opt. Features of the PD.
 * @pdp_zparam:	zone parameters
 * @pdp_discard_granularity: specified by
 *	/sys/block/<disk>/queue/discard_granularity
 * @pdp_sectorsz:	Sector size, exponent base 2
 * @pdp_optiosz:        Optimal IO size
 * @pdp_devsz:		device size in bytes
 *
 * Note: in order to avoid passing enums across user-kernel boundary
 * declare the following as uint8_t
 * pdp_devtype: enum pd_devtype
 * pdp_devstate: enum pd_state
 * pdp_phys_if: enum device_phys_if
 * pdp_mclassp: enum mp_media_classp
 */
struct pd_prop {
	char		        pdp_didstr[PD_DEV_ID_LEN];
	uint8_t                 pdp_devtype;
	uint8_t                 pdp_devstate;
	uint8_t                 pdp_phys_if;
	uint8_t                 pdp_mclassp;
	bool                    pdp_fua;
	uint64_t                pdp_cmdopt;

	struct pd_znparam       pdp_zparam;
	uint32_t                pdp_discard_granularity;
	uint32_t                pdp_sectorsz;
	uint32_t                pdp_optiosz;
	uint32_t                pdp_rsvd2;
	uint64_t	        pdp_devsz;
	uint64_t	        pdp_rsvd3;
};

/*
 * IOCTL arguments.
 */

/*
 * Each mpool MPIOC_* parameter block must contain a struct mpioc_cmn
 * parameter block as the very first field (i.e., each derived parameter
 * block "is-a" struct mpioc_cmn).
 */
struct mpioc_cmn {
	uint32_t                mc_unused;
	uint32_t                mc_rsvd;
	int64_t                 mc_err;         /* mpool_err_t */
	char __user            *mc_merr_base;
} __attribute__((__aligned__(8)));

struct mpioc_mpool {
	struct mpioc_cmn        mp_cmn;         /* Must be first field! */
	struct mpool_params     mp_params;
	uint32_t                mp_flags;       /* mp_mgmt_flags */
	uint32_t                mp_dpathc;      /* Count of device paths */
	uint32_t                mp_dpathssz;    /* Length of mp_dpaths */
	uint32_t                mp_rsvd1;
	uint64_t                mp_rsvd2;
	char __user            *mp_dpaths;      /* Newline separated paths */
	struct pd_prop __user  *mp_pd_prop;     /* mp_dpathc elements */
};

/**
 * struct mpioc_params -
 * @mps_cmn:
 * @mps_params;
 */
struct mpioc_params {
	struct mpioc_cmn        mps_cmn;        /* Must be first field! */
	struct mpool_params     mps_params;
};

struct mpioc_mclass {
	struct mpioc_cmn                    mcl_cmn; /* Must be first field! */
	struct mpool_mclass_xprops __user  *mcl_xprops;
	uint32_t                            mcl_cnt;
	uint32_t                            mcl_rsvd1;
};

struct mpioc_drive {
	struct mpioc_cmn        drv_cmn;         /* Must be first field! */
	uint32_t	        drv_flags;   /* mp_mgmt_flags */
	uint32_t	        drv_rsvd1;
	struct pd_prop __user  *drv_pd_prop; /* mp_dpathc elements */
	uint32_t                drv_dpathc;  /* Count of device paths */
	uint32_t                drv_dpathssz;/* Length of mp_dpaths */
	char __user            *drv_dpaths;  /* Newline separated device paths*/
};

enum mpioc_list_cmd {
	MPIOC_LIST_CMD_INVALID     = 0,
	MPIOC_LIST_CMD_PROP_GET    = 1,       /* Used by mpool get command */
	MPIOC_LIST_CMD_PROP_LIST   = 2,       /* Used by mpool list command */
	MPIOC_LIST_CMD_LAST = MPIOC_LIST_CMD_PROP_LIST,
};

struct mpioc_list {
	struct mpioc_cmn        ls_cmn;     /* Must be first field! */
	uint32_t                ls_cmd;     /* enum mpioc_list_cmd */
	uint32_t                ls_listc;
	void __user            *ls_listv;
};

struct mpioc_prop {
	struct mpioc_cmn            pr_cmn;         /* Must be first field! */
	struct mpool_xprops         pr_xprops;
	struct mpool_usage          pr_usage;
	struct mpool_mclass_xprops  pr_mcxv[MP_MED_NUMBER];
	uint32_t                    pr_mcxc;
	uint32_t                    pr_rsvd1;
	uint64_t                    pr_rsvd2;
};

struct mpioc_devprops {
	struct mpioc_cmn       dpr_cmn;         /* Must be first field! */
	char                   dpr_pdname[PD_NAMESZ_MAX];
	struct mpool_devprops  dpr_devprops;
};

/**
 * struct mpioc_mblock:
 * @mb_cmn:
 * @mb_objid:   mblock unique ID (permanent)
 * @mb_offset:  mblock read offset (ephemeral)
 * @mb_props:
 * @mb_layout
 * @mb_spare:
 * @mb_mclassp: enum mp_media_classp, declared as uint8_t
 */
struct mpioc_mblock {
	struct mpioc_cmn            mb_cmn;     /* Must be first field! */
	uint64_t                    mb_objid;
	int64_t                     mb_offset;
	struct mblock_props_ex      mb_props;

	uint8_t                     mb_spare;
	uint8_t                     mb_mclassp;
	uint16_t                    mb_rsvd1;
	uint32_t                    mb_rsvd2;
	uint64_t                    mb_rsvd3;
};

struct mpioc_mblock_id {
	struct mpioc_cmn    mi_cmn;     /* Must be first field! */
	uint64_t            mi_objid;
};

#define MPIOC_KIOV_MAX          (1024)

struct mpioc_mblock_rw {
	struct mpioc_cmn            mb_cmn;     /* Must be first field! */
	uint64_t                    mb_objid;
	int64_t                     mb_offset;
	uint32_t                    mb_rsvd2;
	uint16_t                    mb_rsvd3;
	uint16_t                    mb_iov_cnt;
	const struct iovec __user  *mb_iov;
};

/*
 * Mlog ioctl args
 */
struct mpioc_mlog {
	struct mpioc_cmn            ml_cmn;     /* Must be first field! */
	uint64_t                    ml_objid;
	uint64_t                    ml_rsvd;
	struct mlog_props_ex        ml_props;

	struct mlog_capacity        ml_cap;
	uint8_t                     ml_mclassp; /* enum mp_media_classp */
	uint8_t                     ml_rsvd1[7];
	uint64_t                    ml_rsvd2;
};

struct mpioc_mlog_id {
	struct mpioc_cmn    mi_cmn;     /* Must be first field! */
	uint64_t            mi_objid;
	uint64_t            mi_gen;
	uint8_t             mi_state;
	uint8_t             mi_rsvd1[7];
};

struct mpioc_mlog_io {
	struct mpioc_cmn        mi_cmn;     /* Must be first field! */
	uint64_t                mi_objid;
	int64_t                 mi_off;
	uint8_t                 mi_op;
	uint8_t                 mi_rsvd1[5];
	uint16_t                mi_iovc;
	struct iovec __user    *mi_iov;
	uint64_t                mi_rsvd2;
};

/**
 * struct mpioc_vma
 * @map_cmn:
 */
struct mpioc_vma {
	struct mpioc_cmn    im_cmn;
	uint32_t            im_advice;
	uint32_t            im_mbidc;
	uint64_t __user    *im_mbidv;
	uint64_t            im_bktsz;
	int64_t             im_offset;
	uint64_t            im_len;
	uint64_t            im_vssp;
	uint64_t            im_rssp;
	uint64_t            im_rsvd;
};

/**
 * struct mpioc_test - Used for testing
 * @mpt_cmn:
 * @mpt_cmd:    subcommand
 * @mpt_sval:   in/out data for subcommand
 * @mpt_uval:   in/out data for subcommand
 */
struct mpioc_test {
	struct mpioc_cmn    mpt_cmn;
	int32_t             mpt_cmd;
	int32_t             mpt_rsvd1;
	int64_t             mpt_sval[3];
	uint64_t            mpt_uval[3];
};

/*
 * mpioc_union is used by mpc_ioctl() to reserve enough storage
 * on the stack to contain any mpioc_* object (so as to avoid
 * a call to kmalloc() on each call to mpc_ioctl()).  Be very
 * careful not to bloat these structures.
 */
union mpioc_union {
	struct mpioc_cmn            mpu_cmn;
	struct mpioc_mpool          mpu_mpool;
	struct mpioc_drive          mpu_drive;
	struct mpioc_params         mpu_params;
	struct mpioc_mclass         mpu_mclass;
	struct mpioc_list           mpu_list;
	struct mpioc_prop           mpu_prop;
	struct mpioc_devprops       mpu_devprops;
	struct mpioc_mlog           mpu_mlog;
	struct mpioc_mlog_id        mpu_mlog_id;
	struct mpioc_mlog_io        mpu_mlog_io;
	struct mpioc_mblock         mpu_mblock;
	struct mpioc_mblock_id      mpu_mblock_id;
	struct mpioc_mblock_rw      mpu_mblock_rw;
	struct mpioc_vma            mpu_vma;
	struct mpioc_test           mpu_test;
};

#define MPIOC_MAGIC             ('2')

#define MPIOC_MP_CREATE         _IOWR(MPIOC_MAGIC, 1, struct mpioc_mpool)
#define MPIOC_MP_DESTROY        _IOWR(MPIOC_MAGIC, 2, struct mpioc_mpool)
#define MPIOC_MP_ACTIVATE       _IOWR(MPIOC_MAGIC, 5, struct mpioc_mpool)
#define MPIOC_MP_DEACTIVATE     _IOWR(MPIOC_MAGIC, 6, struct mpioc_mpool)
#define MPIOC_MP_RENAME         _IOWR(MPIOC_MAGIC, 7, struct mpioc_mpool)

#define MPIOC_PARAMS_GET        _IOWR(MPIOC_MAGIC, 10, struct mpioc_params)
#define MPIOC_PARAMS_SET        _IOWR(MPIOC_MAGIC, 11, struct mpioc_params)
#define MPIOC_MP_MCLASS_GET     _IOWR(MPIOC_MAGIC, 12, struct mpioc_mclass)

#define MPIOC_DRV_ADD           _IOWR(MPIOC_MAGIC, 15, struct mpioc_drive)
#define MPIOC_DRV_SPARES        _IOWR(MPIOC_MAGIC, 16, struct mpioc_drive)

#define MPIOC_PROP_GET          _IOWR(MPIOC_MAGIC, 20, struct mpioc_list)
#define MPIOC_PROP_SET          _IOWR(MPIOC_MAGIC, 21, struct mpioc_list)
#define MPIOC_DEVPROPS_GET      _IOWR(MPIOC_MAGIC, 22, struct mpioc_devprops)

#define MPIOC_MLOG_ALLOC        _IOWR(MPIOC_MAGIC, 30, struct mpioc_mlog)
#define MPIOC_MLOG_COMMIT       _IOWR(MPIOC_MAGIC, 32, struct mpioc_mlog_id)
#define MPIOC_MLOG_ABORT        _IOWR(MPIOC_MAGIC, 33, struct mpioc_mlog_id)
#define MPIOC_MLOG_DELETE       _IOWR(MPIOC_MAGIC, 34, struct mpioc_mlog_id)
#define MPIOC_MLOG_FIND         _IOWR(MPIOC_MAGIC, 37, struct mpioc_mlog)
#define MPIOC_MLOG_READ         _IOWR(MPIOC_MAGIC, 40, struct mpioc_mlog_io)
#define MPIOC_MLOG_WRITE        _IOWR(MPIOC_MAGIC, 41, struct mpioc_mlog_io)
#define MPIOC_MLOG_PROPS        _IOWR(MPIOC_MAGIC, 42, struct mpioc_mlog)
#define MPIOC_MLOG_ERASE        _IOWR(MPIOC_MAGIC, 43, struct mpioc_mlog_id)

#define MPIOC_MB_ALLOC          _IOWR(MPIOC_MAGIC, 50, struct mpioc_mblock)
#define MPIOC_MB_ABORT          _IOWR(MPIOC_MAGIC, 52, struct mpioc_mblock_id)
#define MPIOC_MB_COMMIT         _IOWR(MPIOC_MAGIC, 53, struct mpioc_mblock_id)
#define MPIOC_MB_DELETE         _IOWR(MPIOC_MAGIC, 54, struct mpioc_mblock_id)
#define MPIOC_MB_FIND           _IOWR(MPIOC_MAGIC, 56, struct mpioc_mblock)
#define MPIOC_MB_READ           _IOWR(MPIOC_MAGIC, 60, struct mpioc_mblock_rw)
#define MPIOC_MB_WRITE          _IOWR(MPIOC_MAGIC, 61, struct mpioc_mblock_rw)

#define MPIOC_VMA_CREATE        _IOWR(MPIOC_MAGIC, 70, struct mpioc_vma)
#define MPIOC_VMA_DESTROY       _IOWR(MPIOC_MAGIC, 71, struct mpioc_vma)
#define MPIOC_VMA_PURGE         _IOWR(MPIOC_MAGIC, 72, struct mpioc_vma)
#define MPIOC_VMA_VRSS          _IOWR(MPIOC_MAGIC, 73, struct mpioc_vma)

#define MPIOC_TEST              _IOWR(MPIOC_MAGIC, 99, struct mpioc_test)

#endif
