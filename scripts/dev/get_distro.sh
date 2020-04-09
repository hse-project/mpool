#!/usr/bin/bash

#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

#
# This script defines some cmake macros that can be used in CMakeLists.txt
# files, to a) control the build logic, or b) to pass cpp mcaros into code
# (which is really a special case of a).
#
# The defined macros are:
#
# MPOOL_DISTRO          e.g.: fc25, el7, etc.
# MPOOL_DISTRO_PREFIX   e.g.: fc, el
# MPOOL_DISTROMAJOR     e.g.: 25, 7
# MPOOL_DISTROMINOR     empty for Fedora, stuff after first '.' for el
#
# The output of this script is four string fields in a single line:
# ${MPOOL_DISTRO_PREFIX} ${MPOOL_DISTRO} ${MPOOL_DISTROMAJOR} ${MPOOL_DISTROMINOR}

# Allow override of detection, if first arg matches a known pattern
if [[ $1 = "el6.9" ]]
then
    echo "el el6 6 9 supported"
    exit 0;
fi

valid="supported"

#
# Is this Fedora?
#
fedora_file="/etc/fedora-release"
if [[ ! -f ${fedora_file} ]]
then
    fedora_file="/etc/redhat-release"
fi
if [[ -f ${fedora_file} ]]
then
    distro=$(cat $fedora_file | awk ' { print $1 } ')
    if [[ $distro = "Fedora" ]]
    then
        fnum=$(cat $fedora_file | awk ' { print $3 } ')
        if (( fnum < 22 )); then
            valid="unsupported"
        fi
        echo "fc fc${fnum} $fnum 0 $valid"

        exit 0
    fi
fi

#
# Is this Redhat Enterprise?
#

rh_string1="Red Hat Enterprise Linux Server release"
rh_string2="Red Hat Enterprise Linux release"
recognized=0
redhat_file="/etc/redhat-release"
if [[ -f "$redhat_file" ]]; then
    is_centos=$(grep -c "CentOS Linux" $redhat_file )
    is_cent6=$(grep -c "CentOS release 6" $redhat_file )
    is_redhat=$(grep -c "Red Hat Enterprise Linux" $redhat_file )
    if   [[ $is_centos = "1" ]]; then
        distro="CentOS"
        ver=$(cat $redhat_file | awk ' { print $4 } ' | sed -e 's/\./ /g')
        major=$(echo $ver | awk ' { print $1 } ')
        minor=$(echo $ver | awk ' { print $2 } ')
    elif [[ $is_cent6  = "1" ]]; then
        distro="CentOS"
        ver=$(cat $redhat_file | awk ' { print $3 } ' | sed -e 's/\./ /g')
        major=$(echo $ver | awk ' { print $1 } ')
        minor=$(echo $ver | awk ' { print $2 } ')
    elif [[ $is_redhat = "1" ]]; then
        distro="RedHat"
	fields=($(cat $redhat_file))
	for field in ${fields[@]};
	do
	    ((i++))
	    if [[ $field =  "release" ]]; then
		ver=$(echo "${fields[$i]}" | sed -e 's/\./ /g')
		recognized=1
		break;
	    fi
	done

	if [[ $recognized = 0 ]]; then
	    echo "Redhat release not recognized"
	    exit -1
	fi
	major=$(echo $ver | awk ' { print $1 } ')
        minor=$(echo $ver | awk ' { print $2 } ')
    else
        echo "DISTRO=unknown"
        exit -1
    fi

    if (( major != 7 && major != 8 )); then
       valid="unsupported"
    fi
    echo "el el${major} $major $minor $valid"
    exit 0
fi
echo "unknown unknown0 0 0 unsupported"
exit -1
