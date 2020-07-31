#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

cmake_minimum_required( VERSION 3.6 )

cmake_policy( SET CMP0046 OLD )
cmake_policy( SET CMP0053 NEW )

project( mpool C )

set( PROJECT_VERSION            "${BUILD_PKG_VERSION}" )
set( PROJECT_HOMEPAGE_URL       "https://github.com/hse-project/mpool" )

set( CMAKE_PROJECT_VERSION      "${PROJECT_VERSION}" )
set( CMAKE_PROJECT_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}" )

message( STATUS "Configuring project ${PROJECT_NAME} ${PROJECT_VERSION} ${BUILD_PKG_TAG} common..." )

set( CMAKE_C_STANDARD 99 )
set( CMAKE_INSTALL_PREFIX /usr )

include(GNUInstallDirs)

##################
# The next three variables determine where the build outputs land:
#
#   MPOOL_EXT_INSTALL_PREFIX
#     - used by external code in ./sub directory.
#
#   MPOOL_STAGE_INSTALL_PREFIX
#     - used by mpool code.
#
#   MPOOL_SCRIPTS_DIR
#     - Scripts related to the mpool source tree (building it, working
#       with git, etc.)
#
# Build outputs for mpool and external code are kept separate primarily so
# developers can rebuild just the mpool code which is much quicker than
# building everything.
##################

set( MPOOL_EXT_INSTALL_PREFIX        ${PROJECT_BINARY_DIR}/ext_install)
set( MPOOL_STAGE_INSTALL_PREFIX      ${PROJECT_BINARY_DIR}/stage)
set( MPOOL_SCRIPTS_DIR               ${PROJECT_SOURCE_DIR}/scripts )
set( MPOOL_SUB_REPOS                 ${PROJECT_SOURCE_DIR}/sub)


set( CMAKE_RUNTIME_OUTPUT_DIRECTORY  ${MPOOL_STAGE_INSTALL_PREFIX}/bin)

set( MPOOL_UTIL_DIR
    ${PROJECT_SOURCE_DIR}/src/util)

set( MPOOL_3RDPARTY_DIR
    ${PROJECT_SOURCE_DIR}/3rdparty)

set( MPOOL_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/mpool/include )

include_directories( ${MPOOL_EXT_INSTALL_PREFIX}/include )
include_directories( ${MPOOL_STAGE_INSTALL_PREFIX}/include )

# Translate "-DFOO -DBAR" to "-DFOO;-DBAR" for add_definitions()
#
separate_arguments( BUILD_CDEFS_SEPARATED UNIX_COMMAND ${BUILD_CDEFS} )
add_definitions( ${BUILD_CDEFS_SEPARATED} )

add_compile_options(
    ${BUILD_CFLAGS} -g3 -std=gnu99 -Wall -Werror
    -Wlogical-op -Wno-missing-field-initializers -Wuninitialized -Wmaybe-uninitialized
    -Wextra -Wno-conversion -Wno-sign-conversion -Wno-sign-compare -Wno-unused-parameter
    )

if ( ${UBSAN} )
    message(STATUS "Enabling ubsan support")
    add_compile_options( -fno-omit-frame-pointer )
    add_compile_options( -fsanitize=undefined -fno-sanitize=alignment )
    link_libraries( -fsanitize=undefined -fno-sanitize=alignment )
endif()

if ( ${ASAN} )
    message(STATUS "Enabling asan/lsan support")
    add_compile_options( -fno-omit-frame-pointer )
    add_compile_options( -fsanitize=address )
    link_libraries( -fsanitize=address )
endif()

# Until we resolve these problems...
#
if ( ${CMAKE_C_COMPILER_VERSION} GREATER 7 )
    add_compile_options( -Wimplicit-fallthrough=0 )
endif()


# Define valgrind wrapper functions if valgrind-devel package
# is installed (sudo dnf install valgrind-devel).
#
find_path(ValgrindIncludes valgrind.h PATHS /usr/include/valgrind)
if(ValgrindIncludes)
    message(STATUS "Enabling valgrind support")
else()
    message(STATUS "Disabling valgrind support")
    add_definitions( -DNVALGRIND )
endif()

#
# Python
#

# Fedora system packages use Python 3; RHEL7 packages use Python 2
if( EXISTS /etc/fedora-release )
    set( MPOOL_PYTHON /usr/bin/python3 )
else()
    set( MPOOL_PYTHON /usr/bin/python )
endif()

execute_process( COMMAND ${MPOOL_PYTHON} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())" OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE MPOOL_PYTHON_PKG_DIR )

get_filename_component( MPOOL_PYTHON_BASE_DIR ${MPOOL_PYTHON_PKG_DIR}/.. ABSOLUTE )
set ( MPOOL_PYTHON_SOS_DIR ${MPOOL_PYTHON_PKG_DIR}/sos )
set ( MPOOL_PYTHON_SOS_PLUGINS_DIR ${MPOOL_PYTHON_PKG_DIR}/sos/plugins )

include(CMakeLists.macros)


# Flags to enable the use of gprof
#
set( PROFILING false )

if( ${PROFILING} )
    add_compile_options( -pg )
    link_directories( /usr/local/lib )
    link_libraries( profiler )
endif( ${PROFILING} )

# Needed for the JNI bridge.
#
set( YCSB TRUE CACHE BOOL "Build JNI bridge artifacts" )
if( ${YCSB} )
    add_compile_options( -fPIC )
endif()


################################################################
#
# Establish prerequisites for all compiled targets
#
################################################################

#
# All compiled binaries should depend upon mpool_version.
#
macro(add_library _target)
    _add_library(${_target} ${ARGN})
    add_dependencies(${_target} mpool_version)
endmacro()

macro(add_executable _target)
    _add_executable(${_target} ${ARGN})
    add_dependencies(${_target} mpool_version)
endmacro()


# Common package options...
#
set( CPACK_PACKAGE_LICENSE      "MIT" )
set( CPACK_PACKAGE_VENDOR       "${BUILD_PKG_VENDOR}" )
set( CPACK_PACKAGE_VERSION      "${PROJECT_VERSION}${BUILD_PKG_VQUAL}" )

set( CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "media pool ${BUILD_PKG_TAG}-${BUILD_STYPE}${BUILD_NUMBER}" )

set( CPACK_PACKAGE_DESCRIPTION            "Object Storage Media Pool (mpool)" )
set( CPACK_COMPONENT_RUNTIME_DESCRIPTION  "${CPACK_PACKAGE_DESCRIPTION} runtime" )
set( CPACK_COMPONENT_DEVEL_DESCRIPTION    "${CPACK_PACKAGE_DESCRIPTION} SDK" )
set( CPACK_COMPONENT_TEST_DESCRIPTION     "${CPACK_PACKAGE_DESCRIPTION} test tools" )
set( CPACK_COMPONENT_TEST_DEPENDS         "runtime" )

if( "${BUILD_NUMBER}" )
    set( BUILD_RTYPE "${BUILD_STYPE}${BUILD_NUMBER}" )
else()
    set( BUILD_RTYPE "" )
endif()

set( CPACK_PACKAGE_RELEASE      "${BUILD_PKG_REL}${BUILD_RTYPE}${BUILD_PKG_DIST}" )

# Create the version header file template:
#
file( WRITE ${CMAKE_BINARY_DIR}/mpool_version.h.in
    "static const char mpool_version[] = \"@CPACK_PACKAGE_VERSION@-@CPACK_PACKAGE_RELEASE@\";\n"
    "static const char mpool_tag[] = \"@BUILD_PKG_TAG@\";\n"
    "static const char mpool_sha[] = \"@BUILD_PKG_SHA@\";\n"
)

configure_file(
    ${CMAKE_BINARY_DIR}/mpool_version.h.in
    ${MPOOL_STAGE_INSTALL_PREFIX}/include/mpool_version.h
    @ONLY
    )


################################################################
#
# Subdirectories
#
################################################################

enable_testing()

add_subdirectory( sub )
add_subdirectory( src )
add_subdirectory( test )
add_subdirectory( include )
add_subdirectory( scripts/tmpfiles.d )
add_subdirectory( scripts/systemd )
add_subdirectory( scripts/udev/rules.d )
add_subdirectory( scripts/sysctl.d )
add_subdirectory( scripts/modules-load.d )


# Configure cpack now that we've seen all components...
#
include( ${MPOOL_SCRIPTS_DIR}/${BUILD_PKG_TYPE}/CMakeLists.txt )
include( CPack )


# CTest pretest script
#
configure_file( "${MPOOL_SCRIPTS_DIR}/cmake/CTestCustom.cmake.in"
    ${CMAKE_BINARY_DIR}/CTestCustom.cmake )

get_directory_property( DirIncs   INCLUDE_DIRECTORIES )
get_directory_property( DirCDefs  COMPILE_DEFINITIONS )
get_directory_property( DirCOpts  COMPILE_OPTIONS )
get_directory_property( DirLDirs  LINK_DIRECTORIES )
get_directory_property( DirLLibs  LINK_LIBRARIES )
get_directory_property( DirLOpts  LINK_OPTIONS )

message( STATUS "Compiler flags:" )
message( STATUS "  INCLUDE_DIRECTORIES     ${DirIncs}" )
message( STATUS "  COMPILE_DEFINITIONS     ${DirCDefs}" )
message( STATUS "  COMPILE_OPTIONS         ${DirCOpts}" )
message( STATUS "  LINK_DIRECTORIES        ${DirLDirs}" )
message( STATUS "  LINK_LIBRARIES          ${DirLLibs}" )
message( STATUS "  LINK_OPTIONS            ${DirLOpts}" )
