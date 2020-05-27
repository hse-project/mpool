#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

# rpm-post-uninstall.sh
# rpm will execute the contents of this file with /bin/sh
# https://fedoraproject.org/wiki/Packaging:Scriptlets

if [[ $1 -eq 0 ]]; then
    # remove mpool related tmpfiles
    rm -fr /run/mpool

    # reload udev rules
    udevadm control --reload-rules
fi

exit 0
