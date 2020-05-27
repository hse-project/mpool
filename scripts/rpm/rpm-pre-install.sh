#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

# rpm-pre-install.sh
# rpm will execute the contents of this file with /bin/sh
# https://fedoraproject.org/wiki/Packaging:Scriptlets

if [[ $1 -gt 1 ]] && [[ -d /dev/mpool ]]; then
    # this is an upgrade - use the existing mpool executable to deactivate
    # mpools before installing new files
    echo "Deactivating mpools..."
    echo

    mpool scan --deactivate -v

    if [[ $? -eq 0 ]]; then
        echo
        echo "You will need to manually reactivate mpools after the package " \
             "upgrade is complete."
    fi
fi

exit 0
