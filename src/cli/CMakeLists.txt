#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

message(STATUS "Configuring ui ${CMAKE_CURRENT_SOURCE_DIR}")

MPOOL_EXECUTABLE(
  NAME
    mpool

  SRCS
    common.c
    ls.c
    mpool.c
    mpool_ui.c
    ui_common.c
    yaml.c
    ${MPOOL_UTIL_DIR}/source/string.c
    ${MPOOL_UTIL_DIR}/source/parse_num.c
    ${MPOOL_UTIL_DIR}/source/param.c
    ${MPOOL_UTIL_DIR}/source/parser.c
    ${MPOOL_UTIL_DIR}/source/printbuf.c

  INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MPOOL_UTIL_DIR}/include
    ${MPOOL_INCLUDE_DIRS}

  DEP_LIBS
    mpool-lib
)
