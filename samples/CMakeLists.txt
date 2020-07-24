#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

message(STATUS "Configuring mpool samples in ${CMAKE_CURRENT_SOURCE_DIR}")

include_directories( ${MPOOL_INCLUDE_DIRS} )

MPOOL_EXECUTABLE(
  NAME
    mpool_sample

  SRCS
    mpool_sample.c

  DEP_LIBS
    mpool-solib

  COMPONENT
    test
)

MPOOL_EXECUTABLE(
  NAME
    mblock_sample

  SRCS
    mblock_sample.c

  DEP_LIBS
    mpool-solib

  COMPONENT
    test
)

MPOOL_EXECUTABLE(
  NAME
    mlog_sample

  SRCS
    mlog_sample.c

  DEP_LIBS
    mpool-solib

  COMPONENT
    test
)

MPOOL_EXECUTABLE(
  NAME
    mdc_sample

  SRCS
    mdc_sample.c

  DEP_LIBS
    mpool-solib

  COMPONENT
    test
)

MPOOL_EXECUTABLE(
  NAME
    mcache_sample

  SRCS
    mcache_sample.c

  DEP_LIBS
    mpool-solib

  COMPONENT
    test
)
