<!---
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#
-->

This repository contains the user-space library and CLI for the Micron
Object Storage Media Pool known as the Mpool.

To build for the first time:

    1) git clone .../mpool.git
    2) cd mpool
    3) make package
    4) sudo dnf install -y packages...

To perform an incremental build (say, after modifying a file):

    1) make
    2) make install


To remove most artifacts and rebuild:

    1) make clean all


To remove all build artifacts and rebuild:

    1) make distclean all


To perform a debug build:

    1) make debug


Note that you can always use parallel make (make -j) to speed up the build.

The default build type is "release", but you can specify "debug" for a non--optimized
build with symbols, or "relassert"  for an optimized build with asserts enabled.

The build tree and build artifacts for each build type live in different
subdirectories under "build/" named for the release and distro.  For example:

    1) make -j distclean release
    2) make -j distclean relassert
    3) make -j distclean debug

    4) ls -F build
       debug.fc25/  relassert.fc25/  release.fc25/

To learn more Makefile tricks:

    make help
