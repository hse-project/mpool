#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.

message( STATUS "Configuring ${PROJECT_NAME} deb..." )

set( CPACK_GENERATOR "DEB" )

set( CPACK_DEBIAN_PACKAGE_VERSION    "${CPACK_PACKAGE_VERSION}" )
set( CPACK_DEBIAN_PACKAGE_RELEASE    "${MPOOL_RELEASE}${PACKAGE_DIST}" )
set( CPACK_DEBIAN_PACKAGE_RELEASE_DIST  "OFF" )
set( CPACK_DEBIAN_PACKAGE_LICENSE    "MIT" )
set( CPACK_DEBIAN_PACKAGE_MAINTAINER "Micron Technology, Inc." )
set( CPACK_DEBIAN_PACKAGE_GROUP      "Unspecified" )
set( CPACK_DEBIAN_PACKAGE_URL        "https://github.com/hse-project/mpool" )
set( CPACK_DEBIAN_COMPONENT_INSTALL  "ON" )

# Setting this keeps CMake 3.7+ from generating nonsense filenames
set( CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT" )


# Runtime
#
set( CPACK_DEBIAN_RUNTIME_PACKAGE_NAME     "${CPACK_PACKAGE_NAME}" )
set( CPACK_DEBIAN_RUNTIME_PACKAGE_RELEASE  "${CPACK_DEBIAN_PACKAGE_RELEASE}" )

set( CPACK_DEBIAN_RUNTIME_PACKAGE_SUMMARY
    "Object Storage Media Pool (mpool) runtime package")

set( CPACK_DEBIAN_RUNTIME_PACKAGE_DESCRIPTION
    "Object Storage Media Pool (mpool) runtime package")

set( CPACK_DEBIAN_RUNTIME_FILE_NAME
    "${CPACK_DEBIAN_RUNTIME_PACKAGE_NAME}-${CPACK_DEBIAN_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}.${PACKAGE_ARCH}.deb" )


# Devel/SDK - currently contains static libs--see "Libs" above.
#
set( CPACK_DEBIAN_DEVEL_PACKAGE_NAME     "${CPACK_PACKAGE_NAME}-devel" )
set( CPACK_DEBIAN_DEVAL_PACKAGE_RELEASE  "${CPACK_DEBIAN_PACKAGE_RELEASE}" )

set( CPACK_DEBIAN_DEVEL_PACKAGE_SUMMARY
    "Object Storage Media Pool (mpool) software development kit")
set( CPACK_DEBIAN_DEVEL_PACKAGE_DESCRIPTION
    "Object Storage Media Pool (mpool) software development kit")

set( CPACK_DEBIAN_DEVEL_FILE_NAME
    "${CPACK_DEBIAN_DEVEL_PACKAGE_NAME}-${CPACK_DEBIAN_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}.${PACKAGE_ARCH}.deb" )


# Tests - we won't want to ship our test applications in production.
#
set( CPACK_DEBIAN_TEST_PACKAGE_NAME     "${CPACK_PACKAGE_NAME}-test" )
set( CPACK_DEBIAN_TEST_PACKAGE_RELEASE  "${CPACK_DEBIAN_PACKAGE_RELEASE}" )

set( CPACK_DEBIAN_TEST_PACKAGE_SUMMARY
    "Object Storage Media Pool (mpool) test package")

set( CPACK_DEBIAN_TEST_PACKAGE_DESCRIPTION
    "Object Storage Media Pool (mpool) test package")

set( CPACK_DEBIAN_TEST_FILE_NAME
    "${CPACK_DEBIAN_TEST_PACKAGE_NAME}-${CPACK_DEBIAN_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}.${PACKAGE_ARCH}.dep" )

set( CPACK_COMPONENT_TEST_DEPENDS       "runtime" )
