<!---
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
-->

# Smoke Tests

## Initial Setup

### Pre-requisites for running these smoke tests

  - sudo access: these tests use sudo to perform certain functions.

  - ability to load and unload kernel modules: these tests load and unload
    mpool kernel modules.

  - test block devices: You must have access to one more more block devices.

  - /etc/nf-test-devices: A file that names the raw block devices to be used
    for testing.  The file should contain one block device special file name
    per line, for example:

        $ cat /etc/nf-test-devices
        /dev/nvme0n1
        /dev/nvme0n2

### Optional Features

Some tests rely on a python script to calculate descriptive statistics (min,
max, mean, standard deviation, etc) from data stored in log files.  These test
will still run (and will not fail), but to get the descriptive stats you need
the following:

    sudo dnf install python3-devel
    sudo pip3 install numpy
    sudo pip3 install pandas

If you suffer with proxy settings, you many need to add '--proxy=sbuproxy:8080'
or something similar to your pip3 commands.

## Quick Guide

WARNING: some of the following commands will destroy existing mpools!

    # Build mpool software
    ./scripts/fastbuild.sh

    # Initialize environment
    source ./test/smoke/smoke-setenv release

    # Setup a thin provisioned LV
    ./test/smoke/smoke --lvinit DEV1 DEV2

    # Reset (reinstall modules, wipe out mpools, etc)
    ./test/smoke/smoke --reset

    # Run basic tests
    ./test/smoke/smoke group.kvdb

    # Variation: If you rely on command-line completion, you can specify
    # tests by their file names:
    ./test/smoke/smoke test/smoke/tests/group.kvdb

    # Variation: If you prefer to type less, you can cd into the
    # smoke directory:
    cd ./test/smoke
    ./smoke tests/group.kvdb

    # Run individual tests
    ./test/smoke/smoke putget1
    ./test/smoke/smoke kmtc0

    # Edit code, rebuild
    make release -j32 -s
    
    # Reload and continue testing
    ./test/smoke/smoke --unload --load --mount
    ./test/smoke/smoke putget1

    # prior to commit (can also use 'smokev' to see test output)
    make smoke  (runs tests from group.kvdb)
