#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

#
# Top-level mpool Makefile.
#

define HELP_TEXT

mpool Makefile Help
-------------------

Primary Targets:

    all       -- Build binaries, libraries, tests, etc.
    clean     -- Delete most build outputs (saves external repos).
    config    -- Create build output directory and run cmake config.
    distclean -- Delete all build outputs (i.e., start over).
    install   -- Install build artifacts locally
    package   -- Build "all" and generate RPMs
    help      -- Print this message.

Configuration Variables:

  These configuration variables can be set on the command line
  or in ~/mpool.mk to customize the build.

  Used by 'config' as well as 'clean', 'all', etc:
    BUILD_DIR -- The build output directory.  The default value is
                 BTOPDIR/BDIR.  BUILD_DIR can be set directly, in which
                 case BTOPDIR and BDIR are ignored, or BUILD_DIR can be
                 set indirectly via BTOPDIR and BDIR.  A common use case
                 is to set BTOPDIR in ~/mpool.mk and BDIR on the command
                 line.
    BTOPDIR   -- See BUILD_DIR.
    BDIR      -- See BUILD_DIR.

  Used only by 'config':
    ASAN          -- Enable the gcc address/leak sanitizer
    BUILD_NUMBER  -- Build job number; defaults to 0 if not set.
                     Deliberately named to inherit the BUILD_NUMBER
                     environment variable in Jenkins.
    BUILD_SHA     -- abbreviated git SHA to use in packaging
    CFILE         -- Name of file containing mpool config parameters.
    DEPGRAPH      -- Set to "--graphviz=<filename_prefix>" to generate
                     graphviz dependency graph files
    UBSAN         -- Enable the gcc undefined behavior sanitizer
    REL_CANDIDATE -- When set builds a release candidate.

  Rules of use:
    * The 'config' target uses CFILE, and BUILD_DIR.
      It creates the build output directory (BUILD_DIR)
      and stores the values of CFILE in
      BUILD_DIR/mpool_config.cmake.
    * Other build-related targets ('clean', 'all', etc.)
      require BUILD_DIR and ignore CFILE
      as their values are retrieved from BUILD_DIR/mpool_config.cmake.

  Defaults:
    ASAN           = $(ASAN_DEFAULT)
    BDIR           = $(BDIR_DEFAULT)      # note MPOOL_DISTRO is appended
    BUILD_DIR      = $$(BTOPDIR)/$$(BDIR)
    BUILD_NUMBER   = $(BUILD_NUMBER_DEFAULT)
    BUILD_SHA      = <none>
    BTOPDIR        = $(BTOPDIR_DEFAULT)
    CFILE          = $(CFILE_DEFAULT)
    EFENCE         = $(EFENCE_DEFAULT)
    UBSAN          = $(UBSAN_DEFAULT)
    REL_CANDIDATE  = $(REL_CANDIDATE_DEFAULT)


Customizations:

  The behavior of this makefile can be customized by creating the following files in your home directory:

    ~/mpool.mk  -- included at the top of this makefile, can be
                  used to change default build directory, default
                  build targe, etc.
    ~/mpool1.mk  -- included at the end of this makefile, can be used
                  to extend existing targets or to create your own
                  custom targets

Debug and Release Convenience Targets:

  Convenience targets are keyboard shortcuts aimed at reducing the
  incidence of carpal tunnel syndrome among our highly valued
  development staff.  Including 'release' (or 'debug') on the command
  line changes the defaults for CFILE, BDIR to produce a release (or
  debug) build.

Examples:

  Use 'config-preview' to preview a configuration without modifying any
  files or directories.  This will show you which kernel is used, where
  the build outputs are located, etc.

    make config-preview
    make config-preview release
    make config-preview BTOPDIR=~/builds BDIR=yoyo

  Rebuild the bulk of mpool code, leaving the code in external repos alone:

    make clean all

  Incremental rebuild after modifications to mpool code:

    make

  Create a 'release' build:

    make release all

  Work in the 'release' build output dir, but with your own configuration file:

    make CFILE=myconfig.cmake all

  Build against currently running kernel:

    make debug all

  Custom everything:

    make BDIR=mybuild config CFILE=mybuild.cmake
    make BDIR=mybuild all

  Build with asan/lsan:

    fc25 and newer:

	sudo dnf install libasan libubsan

    rhel 7 vintage:

	sudo yum install devtoolset-7
        sudo yum install devtoolset-7-libasan-devel devtoolset-7-libubsan-devel
        . /opt/rh/devtoolset-7/enable

    export LSAN_OPTIONS=suppressions=scripts/lsan.sup,print_suppressions=0,detect_leaks=true
    make relassert asan

  Build with ubasan:

    See asan/lsan (above) for setup instructions

    export UBSAN_OPTIONS=suppressions=scripts/ubsan.sup,print_stacktrace=1
    make relassert ubsan

endef


.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.NOTPARALLEL:


# MPOOL_SRC_DIR is set to the top of the mpool source tree.
MPOOL_SRC_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Other dirs commonly accessed w/in this makefile:
S=$(MPOOL_SRC_DIR)/scripts

# Get some details about the distro environment
# Can override detection by passing DISTRO=el6.9 (where el6.9 is a
# specifically recognized by the get_distro.sh script)
MPOOL_DISTRO_CMD_OUTPUT := $(shell scripts/dev/get_distro.sh $(DISTRO))
MPOOL_DISTRO_PREFIX     := $(word 1,$(MPOOL_DISTRO_CMD_OUTPUT))
MPOOL_DISTRO            := $(word 2,$(MPOOL_DISTRO_CMD_OUTPUT))
MPOOL_DISTRO_MAJOR      := $(word 3,$(MPOOL_DISTRO_CMD_OUTPUT))
MPOOL_DISTRO_MINOR      := $(word 4,$(MPOOL_DISTRO_CMD_OUTPUT))
MPOOL_DISTRO_SUPPORTED  := $(word 5,$(MPOOL_DISTRO_CMD_OUTPUT))

ifeq ($(MPOOL_DISTRO_SUPPORTED),unsupported)
  $(error invalid MPOOL_DISTRO ($(MPOOL_DISTRO_CMD_OUTPUT)) )
endif

################################################################
#
# Set config var defaults.
#
################################################################
ifeq ($(findstring release,$(MAKECMDGOALS)),release)
  BDIR_DEFAULT  := release.$(MPOOL_DISTRO)
  CFILE_DEFAULT := $(S)/cmake/release.cmake
else ifeq ($(findstring relwithdebug,$(MAKECMDGOALS)),relwithdebug)
  BDIR_DEFAULT  := relwithdebug.$(MPOOL_DISTRO)
  CFILE_DEFAULT := $(S)/cmake/relwithdebug.cmake
else ifeq ($(findstring relassert,$(MAKECMDGOALS)),relassert)
  BDIR_DEFAULT  := relassert.$(MPOOL_DISTRO)
  CFILE_DEFAULT := $(S)/cmake/relassert.cmake
else ifeq ($(findstring optdebug,$(MAKECMDGOALS)),optdebug)
  BDIR_DEFAULT  := optdebug.$(MPOOL_DISTRO)
  CFILE_DEFAULT := $(S)/cmake/optdebug.cmake
else ifeq ($(findstring debug,$(MAKECMDGOALS)),debug)
  BDIR_DEFAULT  := debug.$(MPOOL_DISTRO)
  CFILE_DEFAULT := $(S)/cmake/debug.cmake
else
  BDIR_DEFAULT  := release.$(MPOOL_DISTRO)
  CFILE_DEFAULT := $(S)/cmake/release.cmake
endif

BTOPDIR_DEFAULT       := $(MPOOL_SRC_DIR)/builds
BUILD_DIR_DEFAULT     := $(BTOPDIR_DEFAULT)/$(BDIR_DEFAULT)
MPOOL_REL_KERNEL      := $(shell uname -r)
MPOOL_DBG_KERNEL      := $(MPOOL_REL_KERNEL)+debug
BUILD_NUMBER_DEFAULT  := 0
UBSAN                 := 0
ASAN                  := 0
REL_CANDIDATE_DEFAULT := false

ifeq ($(findstring ubsan,$(MAKECMDGOALS)),ubsan)
  UBSAN := 1
endif

ifeq ($(findstring asan,$(MAKECMDGOALS)),asan)
  ASAN := 1
endif

################################################################
#
# Set config var from defaults unless set by user on the command line.
#
################################################################
BTOPDIR       ?= $(BTOPDIR_DEFAULT)
BDIR          ?= $(BDIR_DEFAULT)
BUILD_DIR     ?= $(BTOPDIR)/$(BDIR)
CFILE         ?= $(CFILE_DEFAULT)
UBSAN         ?= $(UBSAN_DEFAULT)
ASAN          ?= $(ASAN_DEFAULT)
BUILD_NUMBER  ?= $(BUILD_NUMBER_DEFAULT)
REL_CANDIDATE ?= $(REL_CANDIDATE_DEFAULT)

################################################################
# Git and external repos
################################################################

SUBREPO_PATH=sub

mpool_repo=.
mpool_branch=master

# External repos & branches or tags
# The Makefile code depends on the following naming pattern:
# zot_branch=$(zot_repo)_branch, with any '-' in $(zot_repo) converted to '_'

blkid_repo=util-linux
blkid_repo_url=https://github.com/hse-project/util-linux
#blkid_repo_url=https://git.kernel.org/pub/scm/utils/util-linux/util-linux
blkid_tag=v2.32

SUB_LIST= \
	$(blkid_repo)

SUBREPO_PATH_LIST= \
	$(SUBREPO_PATH)/$(blkid_repo)

PERL_CMAKE_NOISE_FILTER := \
    perl -e '$$|=1;\
        while (<>) {\
            next if m/(Entering|Leaving) directory/;\
            next if m/Not a git repository/;\
            next if m/GIT_DISCOVERY_ACROSS_FILESYSTEM/;\
            next if m/^\[..\d%\]/;\
            next if m/cmake_progress/;\
            print;\
        }'

# The following variables affect test execution.
#
# Build-time, set these using CMAKE_FLAGS="..."
#
# Run-time, set these on "make test" commandline
# Must export BUILD_DIR as MPOOL_BUILD_DIR
#
RUN_CTEST = export MPOOL_BUILD_DIR="$(BUILD_DIR)"; set -e -u; cd "$$MPOOL_BUILD_DIR"; ctest --output-on-failure $(CTEST_FLAGS)

# Run tests with label "smoke"
RUN_CTEST_SMOKE = ${RUN_CTEST} -L "smoke"


# Config Inputs:
#   BUILD_DIR
#   CFILE
#   DEPGRAPH
#   BUILD_NUMBER
define config-show
	(echo 'BUILD_DIR="$(BUILD_DIR)"';\
	  echo 'CFILE="$(CFILE)"';\
	  echo 'BUILD_NUMBER="$(BUILD_NUMBER)"';\
	  echo 'MPOOL_REL_KERNEL="$(MPOOL_REL_KERNEL)"';\
	  echo 'MPOOL_DISTRO_PREFIX="$(MPOOL_DISTRO_PREFIX)"';\
	  echo 'MPOOL_DISTRO="$(MPOOL_DISTRO)"';\
	  echo 'MPOOL_DISTRO_MAJOR="$(MPOOL_DISTRO_MAJOR)"';\
	  echo 'MPOOL_DISTRO_MINOR="$(MPOOL_DISTRO_MINOR)"';\
	  echo 'MPOOL_DISTRO_SUPPORTED="$(MPOOL_DISTRO_SUPPORTED)"';\
	  echo 'UBSAN="$(UBSAN)"';\
	  echo 'ASAN="$(ASAN)"';\
	  echo 'REL_CANDIDATE="$(REL_CANDIDATE)"')
endef

define config-gen =
	(echo '# Note: When a variable is set multiple times in this file,' ;\
	echo '#       it is the *first* setting that sticks!' ;\
	echo '' ;\
	echo '# building userspace binaries' ;\
	echo 'Set( UBSAN "$(UBSAN)" CACHE BOOL "" )' ;\
	echo 'Set( ASAN "$(ASAN)" CACHE BOOL "" )' ;\
	echo 'Set( BUILD_NUMBER "$(BUILD_NUMBER)" CACHE STRING "" )' ;\
	echo 'Set( REL_CANDIDATE "$(REL_CANDIDATE)" CACHE STRING "" )' ;\
	if test "$(BUILD_SHA)" ; then \
		echo '' ;\
		echo '# Use input SHA' ;\
		echo 'Set( MPOOL_SHA "$(BUILD_SHA)" CACHE STRING "")' ;\
	fi ;\
	echo '' ;\
	echo '# Linux distro detection' ;\
	echo 'Set( MPOOL_REL_KERNEL "$(MPOOL_REL_KERNEL)" CACHE STRING "" )' ;\
	echo 'Set( MPOOL_DISTRO_PREFIX "$(MPOOL_DISTRO_PREFIX)" CACHE STRING "" )' ;\
	echo 'Set( MPOOL_DISTRO "$(MPOOL_DISTRO)" CACHE STRING "" )' ;\
	echo 'Set( MPOOL_DISTRO_MAJOR "$(MPOOL_DISTRO_MAJOR)" CACHE STRING "" )' ;\
	echo 'Set( MPOOL_DISTRO_MINOR "$(MPOOL_DISTRO_MINOR)" CACHE STRING "" )' ;\
	echo '' ;\
	echo '# BEGIN: $(CFILE)' ;\
	cat  "$(CFILE)" ;\
	echo '# END:   $(CFILE)' ;\
	echo '' ;\
	echo '# BEGIN: $(S)/cmake/defaults.cmake' ;\
	cat  "$(S)/cmake/defaults.cmake" ;\
	echo '# END:   $(S)/cmake/defaults.cmake')
endef


# Allow devs to customize any vars set prior to this point.
#
MPOOL_CUSTOM_INC_DIR ?= $(HOME)
-include $(MPOOL_CUSTOM_INC_DIR)/mpool.mk


# If MAKECMDGOALS contains no goals other than any combination of
# BTYPES then make the given goals depend on the default goal.
#
BTYPES := debug release relwithdebug relassert optdebug asan ubsan
BTYPES := $(filter ${BTYPES},${MAKECMDGOALS})

ifeq ($(filter-out ${BTYPES},${MAKECMDGOALS}),)
BTYPESDEP := ${.DEFAULT_GOAL}
endif

CONFIG = $(BUILD_DIR)/mpool_config.cmake

.PHONY: all allv allq allqv allvq ${BTYPES}
.PHONY: checkfiles chkconfig clean config config-preview
.PHONY: distclean etags help install install-pre maintainer-clean
.PHONY: load package rebuild scrub smoke smokev sub_clone unload


# Goals in mostly alphabetical order.
#
all: config
	@$(MAKE) --no-print-directory -C "$(BUILD_DIR)" $(MF)

allv: config
	$(MAKE) -C "$(BUILD_DIR)" VERBOSE=1 $(MF)

allq: config
	$(MAKE) -C "$(BUILD_DIR)" $(MF) 2>&1 | $(PERL_CMAKE_NOISE_FILTER)

allqv allvq: config
	$(MAKE) -C "$(BUILD_DIR)" VERBOSE=1 $(MF) 2>&1 | $(PERL_CMAKE_NOISE_FILTER)

ifneq (${BTYPES},)
${BTYPES}: ${BTYPESDEP}
	@true
endif

chkconfig:
	@./scripts/dev/mpool-chkconfig

clean:
	@if test -f ${BUILD_DIR}/src/Makefile ; then \
		$(MAKE) --no-print-directory -C "$(BUILD_DIR)/src" clean ;\
		$(MAKE) --no-print-directory -C "$(BUILD_DIR)/test" clean ;\
		rm -rf "$(BUILD_DIR)"/*.rpm ;\
	fi

config-preview:
	@$(config-show)

${CONFIG}:
	@test -d "$(BUILD_DIR)" || mkdir -p "$(BUILD_DIR)"
	@echo "prune: true" > "$(BUILD_DIR)"/.checkfiles.yml
	@$(config-show) > $(BUILD_DIR)/config.sh
	@$(config-gen) > $@.tmp
	@cmp -s $@ $@.tmp || (cd "$(BUILD_DIR)" && cmake $(DEPGRAPH) -C $@.tmp $(CMAKE_FLAGS) "$(MPOOL_SRC_DIR)")
	@cp $@.tmp $@

config: sub_clone ${CONFIG}

distclean scrub:
	@if test -f ${CONFIG} ; then \
		rm -rf "$(BUILD_DIR)" ;\
	fi

etags:
	@echo "Making emacs TAGS file"
	@find src include 3rdparty test \
	        -type f -name "*.[ch]" -print | etags -

help:
	@true
	$(info $(HELP_TEXT))

libs-clean:
	@rm -f /usr/lib/libmpool.*

install-pre: libs-clean config
	@$(MAKE) --no-print-directory -C "$(BUILD_DIR)" install

install: install-pre
	-modprobe mpool
	ldconfig
	systemd-tmpfiles --create /usr/lib/tmpfiles.d/mpool.conf
	-systemctl enable mpool.service --now
	-systemctl restart mpool.service

load:
	modprobe -r mpool
	modprobe mpool

maintainer-clean: distclean
ifneq ($(wildcard ${SUBREPO_PATH_LIST}),)
	rm -rf ${SUBREPO_PATH_LIST}
endif

package: config
	-rm -f "$(BUILD_DIR)"/mpool*.rpm
	$(MAKE) --no-print-directory -C "$(BUILD_DIR)" package

print-%:
	$(info $*="$($*)")
	@true

printq-%:
	$(info $($*))
	@true

rebuild: scrub all

smoke:
	$(RUN_CTEST_SMOKE)

smokev:
	$(RUN_CTEST_SMOKE) -V

$(SUBREPO_PATH):
	mkdir -p $(SUBREPO_PATH)

$(SUBREPO_PATH)/$(blkid_repo):
	git clone $(blkid_repo_url).git $@
	cd $@ && git checkout $(blkid_tag)

sub_clone: ${SUBREPO_PATH_LIST}

unload:
	modprobe -r mpool

# mpool1.mk is used by developers to add their own targets
-include  $(MPOOL_CUSTOM_INC_DIR)/mpool1.mk


# BUILD_DIR may not be ., ./, ./., ./.., /, /., /.., nor empty,
# nor may it contain any whitespace.
#
ifeq ($(abspath ${BUILD_DIR}),)
$(error BUILD_DIR may not be [nil])
else ifeq ($(abspath ${BUILD_DIR}),/)
$(error BUILD_DIR may not be [/])
else ifeq ($(abspath ${BUILD_DIR}),$(abspath ${CURDIR}))
$(error BUILD_DIR may not be [${CURDIR}])
else ifeq ($(abspath ${BUILD_DIR}),$(abspath ${CURDIR}/..))
$(error BUILD_DIR may not be [${CURDIR}/..])
else ifneq ($(words ${BUILD_DIR}),1)
$(error BUILD_DIR may not contain whitespace)
endif
