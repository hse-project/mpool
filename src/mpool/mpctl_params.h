/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MPCTL_PARAMS_H
#define MPCTL_PARAMS_H

unsigned int
mpc_reap_mempct_get(void);

void
mpc_reap_mempct_set(
	unsigned int pct);

unsigned int
mpc_reap_ttl_get(void);

unsigned int
mpc_reap_debug_get(void);

int
mpc_sysctl_register(void);

void
mpc_sysctl_unregister(void);

#endif /* MPCTL_PARAMS_H */
