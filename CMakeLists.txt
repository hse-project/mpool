#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

cmake_minimum_required(VERSION 3.6)
if(${CMAKE_MAJOR_VERSION} GREATER 2)
cmake_policy(SET CMP0046 OLD)
endif()

project( mpool )

message( STATUS "Configuring ${PROJECT_NAME} common..." )

set( CMAKE_C_STANDARD 99 )
set( CMAKE_INSTALL_PREFIX /usr )

set( MPOOL_SUB_REPOS              ${PROJECT_SOURCE_DIR}/sub)

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

# increasing warning levels
set( CFLAGS_WARN_1 "${CFLAGS_WARN_0} -Wall -Werror -std=gnu99")
set( CFLAGS_WARN_2 "${CFLAGS_WARN_1} -Wlogical-op -Wno-missing-field-initializers  -Wuninitialized -Wmaybe-uninitialized ")
set( CFLAGS_WARN_3 "${CFLAGS_WARN_2} -Wextra -Wno-conversion -Wno-sign-conversion -Wno-sign-compare -Wno-unused-parameter")
set( CFLAGS_WARN_4 "${CFLAGS_WARN_2} -Wextra -Wconversion    -Wsign-conversion    -Wsign-compare    -Wunused-parameter")
set( CFLAGS_WARN_MAX "${CFLAGS_WARN_4}")

set( CMAKE_C_FLAGS "${CFLAGS_WARN_3} -g3")

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
    set(CMAKE_KC_FLAGS "${CMAKE_KC_FLAGS} -Wimplicit-fallthrough=0" )
endif()

set ( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_KC_FLAGS}")

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
  set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg " )
  set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L /usr/local/lib -lprofiler" )
endif( ${PROFILING} )

# Needed for the JNI bridge.
#
set( YCSB TRUE CACHE BOOL "Build JNI bridge artifacts" )
if( ${YCSB} )
    add_compile_options( -fPIC )
endif()

# Set-up what kind of build this is
#
if( "${CMAKE_BUILD_TYPE}" STREQUAL "Debug" )
  add_definitions( -DMPOOL_BUILD_DEBUG )
  add_definitions( -DDEBUG_RCU )
  set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-all")
  set( MPOOL_BUILD_TYPE_FLAG "d" )

elseif( "${CMAKE_BUILD_TYPE}" STREQUAL "OptDebug" )
  add_definitions( -DMPOOL_BUILD_DEBUG )
  set( MPOOL_BUILD_TYPE_FLAG "od" )

elseif( "${CMAKE_BUILD_TYPE}" STREQUAL "Release" )
  add_definitions( -DMPOOL_BUILD_RELEASE )
  set( MPOOL_BUILD_TYPE_FLAG "r" )

elseif( "${CMAKE_BUILD_TYPE}" STREQUAL "RelAssert" )
  add_definitions( -DMPOOL_BUILD_RELASSERT )
  add_definitions( -D_FORTIFY_SOURCE )
  set( MPOOL_BUILD_TYPE_FLAG "ra" )

elseif( "${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo" )
  add_definitions( -DMPOOL_BUILD_RELEASE )
  set( MPOOL_BUILD_TYPE_FLAG "rd" )

else()
  message( FATAL_ERROR "Invalid build type [${CMAKE_BUILD_TYPE}]" )
endif()

################################################################
#
# Create the version header file
#
################################################################

#
# We use git
#
find_package(Git)

#
# Create our template for the header file
#
file( WRITE ${CMAKE_BINARY_DIR}/mpool_version.h.in
    "static const char mpool_version[] = \"@MPOOL_VERSION@\";\n"
    "static const char mpool_branch[] = \"@MPOOL_BRANCH@\";\n"
    "static const char mpool_tag[] = \"@MPOOL_TAG@\";\n"
    "static const char mpool_buildserver[] = \"@MPOOL_BUILDSERVER@\";\n"
)

#
# Create cmake code to transform input template into target header file.
#
file( WRITE ${CMAKE_BINARY_DIR}/mpool_version.cmake
"set(MPOOL_SHA ${MPOOL_SHA})
 if( NOT DEFINED MPOOL_SHA )
   execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE MPOOL_SHA
      OUTPUT_STRIP_TRAILING_WHITESPACE
   )
 endif()
 execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE MPOOL_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
 )
 execute_process(
    COMMAND date +%Y%m%d
    OUTPUT_VARIABLE MPOOL_DATESTAMP
    OUTPUT_STRIP_TRAILING_WHITESPACE
 )
 execute_process(
    COMMAND hostname -f
    OUTPUT_VARIABLE MPOOL_BUILDSERVER
    OUTPUT_STRIP_TRAILING_WHITESPACE
 )

 #
 # In 3.6.x, for some reason, CPACK_RPM_PACKAGE_ARCHITECTURE is an
 # empty string when this CMakeLists.txt gets processed.
 #
 execute_process(
  COMMAND uname -m
  OUTPUT_VARIABLE PACKAGE_ARCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
 )

 if( \${MPOOL_TAG} MATCHES \"dirty$\" )
   set( MPOOL_DIRTYTAG \".dirty\" )
 endif( )

 set( MPOOL_MAJORMINORPATCH ${MPOOL_VERSION_MAJOR}.${MPOOL_VERSION_MINOR}.${MPOOL_VERSION_PATCH} )

 set( MPOOL_RELEASE ${MPOOL_BUILD_TYPE_FLAG}${BUILD_NUMBER}.\${MPOOL_DATESTAMP}.\${MPOOL_SHA}\${MPOOL_DIRTYTAG} )

 #
 # MPOOL_SHA varibale may not be populated when user is downloading
 # source code from tarball on the github website instead of cloning the repo
 #
 if( NOT MPOOL_SHA )
   set( MPOOL_RELEASE ${MPOOL_BUILD_TYPE_FLAG}${BUILD_NUMBER}.\${MPOOL_DATESTAMP}\${MPOOL_DIRTYTAG} )
 endif( )

 if (REL_CANDIDATE)
  set( MPOOL_RELEASE ${MPOOL_BUILD_TYPE_FLAG}${BUILD_NUMBER}.\${MPOOL_DATESTAMP} )
 endif()

 set( MPOOL_VERSION \${MPOOL_MAJORMINORPATCH}-\${MPOOL_RELEASE} )

 if( NOT MPOOL_VERSION_CMAKE_INCLUDEONLY )
   if (EXISTS \${DST})
       configure_file(\${SRC} \${CMAKE_BINARY_DIR}/mpool_version.h.tmp @ONLY)
       file(SHA256 \${CMAKE_BINARY_DIR}/mpool_version.h.tmp NEWSHA)
       file(SHA256 \${DST} OLDSHA)
       # Replace mpool_version.h file with a newer only if the content changed.
       if (NOT \${OLDSHA} EQUAL \${NEWSHA})
           configure_file(\${SRC} \${DST} @ONLY)
       endif()
   else()
     	configure_file(\${SRC} \${DST} @ONLY)
   endif()

 endif( )
"
)

#
# Set up custom target
#
add_custom_target(
    mpool_version
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MPOOL_STAGE_INSTALL_PREFIX}/install
    COMMAND ${CMAKE_COMMAND} -D SRC=${CMAKE_BINARY_DIR}/mpool_version.h.in
                     -D DST=${MPOOL_STAGE_INSTALL_PREFIX}/include/mpool_version.h
                     -P ${CMAKE_BINARY_DIR}/mpool_version.cmake
)


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

# NOTE: The package name and version values are configured at "make config"
# time.  If changes are made in the tree (thereby changing the last commit SHA),
# the configured package name won't change until the next make config.
#
# This *should* be OK for a while, since we expect our build server to clone a
# a fresh tree before every build.

set( MPOOL_VERSION_CMAKE_INCLUDEONLY "ON" )
include( ${CMAKE_BINARY_DIR}/mpool_version.cmake )
set( MPOOL_VERSION_CMAKE_INCLUDEONLY "OFF" )


# Common package options...
#
set( CPACK_PACKAGE_NAME           "mpool" )
set( CPACK_PACKAGE_RELOCATABLE    "ON" )
set( CPACK_PACKAGE_VERSION_MAJOR  "${MPOOL_VERSION_MAJOR}" )
set( CPACK_PACKAGE_VERSION_MINOR  "${MPOOL_VERSION_MINOR}" )
set( CPACK_PACKAGE_VERSION_PATCH  "${MPOOL_VERSION_PATCH}" )

# Explicitly set CPACK_PACKAGE_VERSION first, otherwise CMake may generate
# nonsense version strings within generated spec files

set( CPACK_PACKAGE_VERSION
    "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}" )

include( ${MPOOL_SCRIPTS_DIR}/package/CMakeLists.txt )
include( CPack )


# CTest pretest script
#
configure_file( "${MPOOL_SCRIPTS_DIR}/cmake/CTestCustom.cmake.in"
    ${CMAKE_BINARY_DIR}/CTestCustom.cmake )

get_directory_property( DirIncs INCLUDE_DIRECTORIES )
get_directory_property( DirDefs COMPILE_DEFINITIONS )
get_directory_property( DirOpts COMPILE_OPTIONS )

message(STATUS "Compiler flags:")
message(STATUS "  INCLUDE_DIRECTORIES    = ${DirIncs}")
message(STATUS "  COMPILE_DEFINITIONS    = ${DirDefs}")
message(STATUS "  COMPILE_OPTIONS        = ${DirOpts}")
message(STATUS "  CMAKE_C_FLAGS          = ${CMAKE_C_FLAGS}")
message(STATUS "Add the following compiler flags based on build type:")
message(STATUS "  CMAKE_C_FLAGS_DEBUG          = ${CMAKE_C_FLAGS_DEBUG}")
message(STATUS "  CMAKE_C_FLAGS_RELEASE        = ${CMAKE_C_FLAGS_RELEASE}")
message(STATUS "  CMAKE_C_FLAGS_RELASSERT      = ${CMAKE_C_FLAGS_RELASSERT}")
message(STATUS "  CMAKE_C_FLAGS_RELWITHDEBINFO = ${CMAKE_C_FLAGS_RELWITHDEBINFO}")
message(STATUS "  CMAKE_C_FLAGS_OPTDEBUG       = ${CMAKE_C_FLAGS_OPTDEBUG}")
message(STATUS "  CMAKE_KC_FLAGS               = ${CMAKE_KC_FLAGS}")
message(STATUS "Linker flags:")
message(STATUS "  CMAKE_EXE_LINKER_FLAGS = ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "Notes:")
message(STATUS "  * Build with: 'make VERBOSE=1' to verify compiler flags")
