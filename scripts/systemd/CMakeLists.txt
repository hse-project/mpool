#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

install(
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/mpool.service

  DESTINATION
    /usr/lib/systemd/system

  COMPONENT
    runtime
)
