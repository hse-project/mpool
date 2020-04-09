#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

message(STATUS "Configuring mcache_api in ${CMAKE_CURRENT_SOURCE_DIR}")

include_directories( ${MPOOL_INCLUDE_DIRS} )
include_directories( ${MPOOL_UTIL_DIR}/include )

MPOOL_EXECUTABLE(
  NAME
    mcache_api

  SRCS
    mcache_api.c

  DEP_LIBS
    mpool-solib

  COMPONENT
    private
)
