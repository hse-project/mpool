#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.

message( STATUS "Configuring ${PROJECT_NAME} rpm..." )

set( CPACK_GENERATOR "RPM" )

set( CPACK_RPM_PACKAGE_VERSION    "${CPACK_PACKAGE_VERSION}" )
set( CPACK_RPM_PACKAGE_RELEASE    "${MPOOL_RELEASE}${PACKAGE_DIST}" )
set( CPACK_RPM_PACKAGE_RELEASE_DIST  "OFF" )
set( CPACK_RPM_PACKAGE_LICENSE    "MIT" )
set( CPACK_RPM_PACKAGE_VENDOR     "Micron Technology, Inc." )
set( CPACK_RPM_PACKAGE_GROUP      "Unspecified" )
set( CPACK_RPM_PACKAGE_URL        "https://github.com/hse-project/mpool" )
set( CPACK_RPM_COMPONENT_INSTALL  "ON" )
set( CPACK_RPM_RELOCATION_PATHS   /usr /opt/micron /etc )

set( CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    ${DESTDIR}
    /etc
    /etc/modules-load.d
    /etc/sysconfig
    /etc/sysconfig/modules
    /lib
    /lib64
    /usr/lib
    /usr/lib/tmpfiles.d
    /usr/lib/systemd
    /usr/lib/systemd/system
    /usr/lib/sysctl.d
    /usr/lib/udev
    /usr/lib/udev/rules.d
    ${MPOOL_PYTHON_BASE_DIR}
    ${MPOOL_PYTHON_PKG_DIR}
    ${MPOOL_PYTHON_SOS_DIR}
    ${MPOOL_PYTHON_SOS_PLUGINS_DIR}
)

# Setting this keeps CMake 3.7+ from generating nonsense filenames
set( CPACK_RPM_FILE_NAME "RPM-DEFAULT" )

# How to generate the RPM spec file template for your version of CMake, so it
# can be passed to CPACK_RPM_RUNTIME_USER_BINARY_SPECFILE:
#
#   cd builds/release.fc25  # (or whatever build/dist you're working on)
#   cpack -D CPACK_RPM_GENERATE_USER_BINARY_SPECFILE_TEMPLATE=1 -G RPM
#
# https://cmake.org/cmake/help/latest/cpack_gen/rpm.html#variable:CPACK_RPM_GENERATE_USER_BINARY_SPECFILE_TEMPLATE
#

# Runtime
#
set( CPACK_RPM_RUNTIME_PACKAGE_NAME "${CPACK_PACKAGE_NAME}" )
set( CPACK_RPM_RUNTIME_PACKAGE_SUMMARY
    "Object Storage Media Pool (mpool) runtime package")

set( CPACK_RPM_RUNTIME_PACKAGE_DESCRIPTION
    "Object Storage Media Pool (mpool) runtime package")

set( CPACK_RPM_RUNTIME_FILE_NAME
    "${CPACK_RPM_RUNTIME_PACKAGE_NAME}-${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${BUILD_PKG_ARCH}.rpm" )

set( CPACK_RPM_RUNTIME_PACKAGE_RELEASE             "${CPACK_RPM_PACKAGE_RELEASE}" )
set( CPACK_RPM_RUNTIME_USER_BINARY_SPECFILE        "${MPOOL_SCRIPTS_DIR}/rpm/mpool.spec.in" )
set( CPACK_RPM_RUNTIME_PRE_INSTALL_SCRIPT_FILE     "${MPOOL_SCRIPTS_DIR}/rpm/rpm-pre-install.sh" )
set( CPACK_RPM_RUNTIME_POST_INSTALL_SCRIPT_FILE    "${MPOOL_SCRIPTS_DIR}/rpm/rpm-post-install.sh" )
set( CPACK_RPM_RUNTIME_PRE_UNINSTALL_SCRIPT_FILE   "${MPOOL_SCRIPTS_DIR}/rpm/rpm-pre-uninstall.sh" )
set( CPACK_RPM_RUNTIME_POST_UNINSTALL_SCRIPT_FILE  "${MPOOL_SCRIPTS_DIR}/rpm/rpm-post-uninstall.sh" )


# Devel/SDK - currently contains static libs--see "Libs" above.
#
set( CPACK_RPM_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-devel" )
set( CPACK_RPM_DEVEL_PACKAGE_SUMMARY
    "Object Storage Media Pool (mpool) software development kit")
set( CPACK_RPM_DEVEL_PACKAGE_DESCRIPTION
    "Object Storage Media Pool (mpool) software development kit")

set( CPACK_RPM_DEVEL_FILE_NAME
    "${CPACK_RPM_DEVEL_PACKAGE_NAME}-${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${BUILD_PKG_ARCH}.rpm" )

set( CPACK_RPM_DEVEL_POST_INSTALL_SCRIPT_FILE  "${MPOOL_SCRIPTS_DIR}/rpm/rpm-devel-post-install.sh" )
set( CPACK_RPM_DEVEL_USER_BINARY_SPECFILE      "${MPOOL_SCRIPTS_DIR}/rpm/mpool-devel.spec.in" )


# Tests - we won't want to ship our test applications in production.
#
set( CPACK_RPM_TEST_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-test" )
set( CPACK_RPM_TEST_PACKAGE_SUMMARY
    "Object Storage Media Pool (mpool) test package")

set( CPACK_RPM_TEST_PACKAGE_DESCRIPTION
    "Object Storage Media Pool (mpool) test package")

set( CPACK_RPM_DEVEL_FILE_NAME
    "${CPACK_RPM_DEVEL_PACKAGE_NAME}-${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${BUILD_PKG_ARCH}.rpm" )

set( CPACK_RPM_TEST_PACKAGE_RELEASE       "${CPACK_RPM_PACKAGE_RELEASE}" )
set( CPACK_RPM_TEST_USER_BINARY_SPECFILE  "${MPOOL_SCRIPTS_DIR}/rpm/mpool-test.spec.in" )
set( CPACK_COMPONENT_TEST_DEPENDS         "runtime" )
