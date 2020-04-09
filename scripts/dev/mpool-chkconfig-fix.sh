#!/usr/bin/bash

#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

CMD_DIR=$(dirname "$(readlink -f "$0")")
mpool_chkconfig="${CMD_DIR}/mpool-chkconfig"

me=$(basename $0)
function usage {
    echo "Usage: $me [args]"
    cat <<EOF

    Attempt to fix mpool-chkconfig issue.

Arguments:
    -u|--user <uname>       - Run as user 'uname'
    -h|--help               - Print this message

Exit Code

EOF
}

fail () {
    echo
    echo "*** Fail ***"
    echo "$1"
    echo
    exit 1
}

user=$USER

#
# Parse arguments.
#
while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
        (-u|--user)
            (( $# > 0 )) || fail "$flag requires an argument"
            user=$1
            shift;
            ;;
        (-h|--help)
            usage;
            exit;
            ;;
    esac
done

#
# Try mpool-chkconfig and attempt repair if it fails.
#
if [ `whoami` = root ]; then
    su "$user" -c ${mpool_chkconfig} 2>&1 1>/dev/null
else
    ${mpool_chkconfig} 2>&1 1>/dev/null
fi

if [ $? -ne 0 ]; then
    # Run as root to fix mpool-chkconfig issues.
    [ `whoami` = root ] || exec sudo $0 -u "$user"

    echo "mpool-chkconfig issue. Attempting to repair.."

    # First attempt mpool-chkconfig fix option
    ${mpool_chkconfig} -f 2>&1 1>/dev/null

    # Fix hugepages related issues
    #
    # Retained even though huge page support is temporarily in contrib
    if [ ! -d /dev/hugepages/nf ]; then
        mkdir -p /dev/hugepages/nf
    fi
    chgrp nf /dev/hugepages
    chown "${user}":nf /dev/hugepages/nf
    chmod 775 /dev/hugepages
    chmod 775 /dev/hugepages/nf

    # Fix issues with /mpool/$user directory.
    if [ ! -d /mpool/"${user}" ]; then
        mkdir -p /mpool/"${user}"
    fi
    chown "${user}":nf /mpool
    chmod g+w /mpool
    chown "${user}":nf /mpool/"${user}"
    chmod g+w /mpool/"${user}"

    # Verify whether the repair worked.
    su "$user" -c ${mpool_chkconfig}
    if [ $? -ne 0 ]; then
        fail "Giving up. Couldn't repair mpool-chkconfig issue.";
    fi
    echo "mpool-chkconfig issue fixed."
fi
