#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

install(
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/mpool.conf

  DESTINATION
    /usr/lib/tmpfiles.d

  COMPONENT
    runtime
)
