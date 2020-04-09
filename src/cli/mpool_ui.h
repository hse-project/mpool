/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPOOL_UI_H
#define MPOOL_UI_H

#include <util/param.h>

#include <mpool/mpool.h>
#include <mpctl/impool.h>

#include "common.h"
#include "ls.h"

param_get_t get_status;
param_get_t get_media_classp;

param_show_t show_status;
param_show_t show_device_state;
param_show_t show_media_classp;
param_show_t show_pct;
param_show_t show_devtype;

#endif
