#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

# rpm-post-install.sh
# rpm will execute the contents of this file with /bin/sh
# https://fedoraproject.org/wiki/Packaging:Scriptlets

# load kernel modules
modprobe mpool

/sbin/ldconfig

# create mpool related tmpfiles
systemd-tmpfiles --create /usr/lib/tmpfiles.d/mpool.conf

# Enable mpool systemd service
systemctl enable mpool.service --now

exit 0
