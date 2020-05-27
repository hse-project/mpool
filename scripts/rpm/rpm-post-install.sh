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

# sysctl config
echo
echo "*** NOTE ***"
echo
echo "This package configures the vm.max_map_count sysctl parameter."
echo "This is required for normal operation of mpool."
echo
sysctl -p /usr/lib/sysctl.d/90-mpool.conf

# create mpool related tmpfiles
systemd-tmpfiles --create /usr/lib/tmpfiles.d/mpool.conf

# reload udev rules
udevadm control --reload-rules

# Enable mpool systemd service
systemctl enable mpool.service --now

exit 0
