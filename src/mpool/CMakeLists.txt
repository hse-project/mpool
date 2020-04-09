#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

message(STATUS "Configuring mpool in ${CMAKE_CURRENT_SOURCE_DIR}")

set(MPOOL_LIBS
  ${MPOOL_LIBS}
  uuid
  crypto
  pthread
)

set(LIBMPOOL_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${MPOOL_INCLUDE_DIRS}
  ${MPOOL_UTIL_DIR}/include
  ${PROJECT_SOURCE_DIR}
)


MPOOL_OBJECT_FILES(
  NAME
    util-objs

  DEPS
    pthread
    uuid

  CFLAGS
    -DMPOOL_BUILD_LIBMPOOL

  SRCS
    ${MPOOL_UTIL_DIR}/source/alloc.c
    ${MPOOL_UTIL_DIR}/source/printbuf.c
    ${MPOOL_UTIL_DIR}/source/string.c

  INCLUDES
    ${MPOOL_UTIL_DIR}/include
    ${PROJECT_SOURCE_DIR}
)


MPOOL_OBJECT_FILES(
  NAME
    mpcore-objs

  SRCS
    mlog.c
    mp.c
    omf.c
    pd.c
    sb.c

  INCLUDES
    ${LIBMPOOL_INCLUDE_DIRS}
)


MPOOL_OBJECT_FILES(
  NAME
    mpctl-objs

  DEPS
    libblkid-pkg
    ${MPOOL_LIBS}

  SRCS
    device_table.c
    dev_cntlr.c
    discover.c
    logging.c
    mdc.c
    mpctl.c
    mpool_err.c
    mpool_params.c

  INCLUDES
    ${LIBMPOOL_INCLUDE_DIRS}
)


MPOOL_LIBRARY(
  NAME
    mpool

  SRCS
    $<TARGET_OBJECTS:mpctl-objs>
    $<TARGET_OBJECTS:mpcore-objs>
    $<TARGET_OBJECTS:util-objs>

  LINK_LIBS
    ${LIBBLKID_LIBS}
    ${LIBCURL_LIBS}
    ${MPOOL_LIBS}
)


MPOOL_SHARED_LIBRARY(
  NAME
    mpool

  SRCS
    $<TARGET_OBJECTS:mpctl-objs>
    $<TARGET_OBJECTS:mpcore-objs>
    $<TARGET_OBJECTS:util-objs>

  LINK_LIBS
    ${LIBBLKID_LIBS}
    ${LIBCURL_LIBS}
    ${MPOOL_LIBS}
)
