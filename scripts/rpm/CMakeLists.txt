#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.

message( STATUS "Configuring ${PROJECT_NAME} rpm..." )

set( CPACK_GENERATOR                "RPM" )
set( CPACK_RPM_COMPONENT_INSTALL    "ON" )

set( CPACK_RPM_PACKAGE_LICENSE      "${CPACK_PACKAGE_LICENSE}" )
set( CPACK_RPM_PACKAGE_RELEASE      "${BUILD_PKG_REL}${BUILD_RTYPE}${BUILD_PKG_DIST}" )
set( CPACK_RPM_PACKAGE_DESCRIPTION  "${CPACK_PACKAGE_DESCRIPTION}" )
set( CPACK_RPM_PACKAGE_ARCHITECTURE "${BUILD_PKG_ARCH}" )
set( CPACK_RPM_PACKAGE_VENDOR       "${CPACK_PACKAGE_VENDOR}" )
set( CPACK_RPM_PACKAGE_VERSION      "${PROJECT_VERSION}${BUILD_PKG_VQUAL}" )
set( CPACK_RPM_PACKAGE_URL          "${PROJECT_HOMEPAGE_URL}" )
set( CPACK_RPM_FILE_NAME            "RPM-DEFAULT" )
    
set( CPACK_RPM_RELOCATION_PATHS     /usr /opt/micron /etc )


# Runtime
#
set( CPACK_RPM_RUNTIME_PACKAGE_NAME  "${PROJECT_NAME}" )

set( CPACK_RPM_RUNTIME_PACKAGE_SUMMARY
    "${CPACK_PACKAGE_DESCRIPTION_SUMMARY} runtime" )

set( CPACK_RPM_RUNTIME_PRE_INSTALL_SCRIPT_FILE     "${MPOOL_SCRIPTS_DIR}/rpm/rpm-pre-install.sh" )
set( CPACK_RPM_RUNTIME_POST_INSTALL_SCRIPT_FILE    "${MPOOL_SCRIPTS_DIR}/rpm/rpm-post-install.sh" )
set( CPACK_RPM_RUNTIME_PRE_UNINSTALL_SCRIPT_FILE   "${MPOOL_SCRIPTS_DIR}/rpm/rpm-pre-uninstall.sh" )
set( CPACK_RPM_RUNTIME_POST_UNINSTALL_SCRIPT_FILE  "${MPOOL_SCRIPTS_DIR}/rpm/rpm-post-uninstall.sh" )


# Devel/SDK - currently contains static libs--see "Libs" above.
#
set( CPACK_RPM_DEVEL_PACKAGE_SUMMARY
    "${CPACK_PACKAGE_DESCRIPTION_SUMMARY} SDK" )

set( CPACK_RPM_DEVEL_POST_INSTALL_SCRIPT_FILE
    "${MPOOL_SCRIPTS_DIR}/rpm/rpm-devel-post-install.sh" )


# Tests - we won't want to ship our test applications in production.
#
set( CPACK_RPM_TEST_PACKAGE_SUMMARY
    "${CPACK_PACKAGE_DESCRIPTION_SUMMARY} test tools")

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
