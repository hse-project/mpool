#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

# rpm-pre-uninstall.sh
# rpm will execute the contents of this file with /bin/sh
# https://fedoraproject.org/wiki/Packaging:Scriptlets

if [[ $1 -eq 0 ]]; then
    # this is a real uninstall, NOT an upgrade

    # Deactivate all mpools
    mpool scan --deactivate -v

    # Disable mpool systemd service
    systemctl disable mpool.service --now
fi

exit 0
