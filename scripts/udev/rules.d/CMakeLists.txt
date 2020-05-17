#
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/90-mpool.rules
    DESTINATION /usr/lib/udev/rules.d
    COMPONENT runtime
)
