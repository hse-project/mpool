#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

message(STATUS "Configuring ui ${CMAKE_CURRENT_SOURCE_DIR}")

MPOOL_EXECUTABLE2(
  NAME
    mpft

  SRCS
    mpft.c
    mpft_mlog.c
    mpft_mblock.c
    mpft_mdc.c
    mpft_ds.c
    mpft_thread.c
    ${MPOOL_UTIL_DIR}/source/param.c
    ${MPOOL_UTIL_DIR}/source/parser.c
    ${MPOOL_UTIL_DIR}/source/parse_num.c

  INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MPOOL_UTIL_DIR}/include
    ${MPOOL_INCLUDE_DIRS}

  DEPS
    libblkid-pkg
    mpool-solib

  LINK_LIBS
    ${LIBCURL_LIBS}
    mpool-solib

  COMPONENT
    test
)

