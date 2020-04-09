// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/sysctl.h>

unsigned int mpc_reap_ttl    = 10 * 1000 * 1000;
unsigned int mpc_reap_mempct = 100;
unsigned int mpc_reap_debug;

static struct ctl_table_header *mpc_sysctl_table;

#define OID_INT(_name, _var, _mode)					\
{									\
	.procname = (_name), .data = &(_var), .maxlen = sizeof(_var),	\
	.mode = (_mode), .proc_handler = proc_dointvec,			\
}

static struct ctl_table
mpc_sysctl_oid[] = {
	OID_INT("reap_mempct",  mpc_reap_mempct,    0644),
	OID_INT("reap_debug",   mpc_reap_debug,     0644),
	OID_INT("reap_ttl",     mpc_reap_ttl,       0644),
	{ }
};

static struct ctl_table
mpc_sysctl_dir[] = {
	{ .procname = "mpctl", .mode = 0555, .child = mpc_sysctl_oid, },
	{ }
};

static struct ctl_table
mpc_sysctl_root[] = {
	{ .procname = "dev", .mode = 0555, .child = mpc_sysctl_dir, },
	{ }
};

unsigned int
mpc_reap_mempct_get(void)
{
	return clamp_t(unsigned int, mpc_reap_mempct, 5, 100);
}

void
mpc_reap_mempct_set(unsigned int pct)
{
	mpc_reap_mempct = clamp_t(unsigned int, pct, 1, 100);
}

unsigned int
mpc_reap_ttl_get(void)
{
	return max_t(unsigned int, mpc_reap_ttl, 100);
}

unsigned int
mpc_reap_debug_get(void)
{
	return mpc_reap_debug;
}

int
mpc_sysctl_register(void)
{
	mpc_sysctl_table = register_sysctl_table(mpc_sysctl_root);

	return mpc_sysctl_table ? 0 : -ENOMEM;
}

void
mpc_sysctl_unregister(void)
{
	if (mpc_sysctl_table)
		unregister_sysctl_table(mpc_sysctl_table);

	mpc_sysctl_table = NULL;
}
