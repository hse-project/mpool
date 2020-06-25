#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

################################################################
#
# Entries in this file should have the following form:
#
#   set( <variable> <value>  CACHE <type> <docstring> [FORCE] )
#
# where <type> is one of:
#
#   FILEPATH = File chooser dialog.
#   PATH     = Directory chooser dialog.
#   STRING   = Arbitrary string.
#   BOOL     = Boolean ON/OFF checkbox.
#   INTERNAL = No GUI entry (used for persistent variables).
#
# Use INTERNAL or STRING When in doubt about what <type> to use.
#
# For more info, see:
#    http://www.cmake.org/cmake/help/v3.2/command/set.html
#
################################################################



# Control the build type.  Cmake has the following predefined types:
#
#   Type                   Compiler Flags
#   ----                   --------------
#   Debug                  -g
#   Release                -O3 -DNDEBUG
#   RelAssert              -O3
#   RelWithDebInfo         -O2 -DNDEBUG -g
#
# See http://www.cmake.org/cmake/help/v3.0/manual/cmake-variables.7.html
# for more info:

set( CMAKE_VERBOSE_MAKEFILE  FALSE      CACHE BOOL "")
set( CMAKE_BUILD_TYPE        "Release"  CACHE STRING "" )

set( CMAKE_C_FLAGS_RELWITHDEBINFO  "-O2 -DNDEBUG -g" CACHE STRING "" )
set( CMAKE_C_FLAGS_RELASSERT       "-O2" CACHE STRING "" )
set( CMAKE_C_FLAGS_OPTDEBUG        "-Og" CACHE STRING "" )
set( CMAKE_C_FLAGS_RELEASE         "-O2 -DNDEBUG" CACHE STRING "" )
set( CMAKE_C_FLAGS_DEBUG           "-g -fstack-protector-all" CACHE STRING "" )
