#
# SPDX-License-Identifier: MIT
#
# Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
#

#
# Top-level mpool Makefile.
#

define HELP_TEXT

Primary Targets:

    all       -- Build binaries, libraries, tests, etc.
    clean     -- Delete most build outputs (saves external repos).
    config    -- Create build output directory and run cmake config.
    distclean -- Delete all build outputs (i.e., start over).
    install   -- Install build artifacts locally
    package   -- Build "all" and generate deb/rpm packages
    help      -- Print this message.

Configuration Variables:

  These configuration variables can be set on the command line
  or in ~/mpool.mk to customize the build.

    ASAN          -- Enable the gcc address/leak sanitizer
    BUILD_DIR     -- The top-level build output directory
    BUILD_NUMBER  -- Build job number (as set by Jenkins)
    CFILE         -- Name of file containing cmake config parameters.
    DEPGRAPH      -- Set to "--graphviz=<filename_prefix>" to generate
                     graphviz dependency graph files
    UBSAN         -- Enable the gcc undefined behavior sanitizer

  Defaults (not all are customizable):
    BUILD_DIR          $(BUILD_DIR)
    BUILD_NUMBER       $(BUILD_NUMBER)
    BUILD_PKG_ARCH     ${BUILD_PKG_ARCH}
    BUILD_PKG_DIR      ${BUILD_PKG_DIR}
    BUILD_PKG_REL      ${BUILD_PKG_REL}
    BUILD_PKG_TAG      ${BUILD_PKG_TAG}
    BUILD_PKG_TYPE     ${BUILD_PKG_TYPE}
    BUILD_PKG_VERSION  ${BUILD_PKG_VERSION}
    CFILE              $(CFILE)
    UBSAN              $(UBSAN)
    ASAN               $(ASAN)

Customizations:

  The behavior of this makefile can be customized by creating the following files in your home directory:

    ~/mpool.mk  -- included at the top of this makefile, can be
                  used to change default build directory, default
                  build targe, etc.
    ~/mpool1.mk  -- included at the end of this makefile, can be used
                  to extend existing targets or to create your own
                  custom targets

Examples:

  Create a 'release' package:

    make -j package

  Show the current cmake configuration:

    make config-preview

  Rebuild the bulk of mpool code, leaving the code in external repos alone:

    make -j clean all

  Work in the 'release' build output dir, but with your own configuration file:

    make CFILE=myconfig.cmake

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


# Edit when we cut a release branch.
BUILD_PKG_VERSION := 1.8.0
BUILD_PKG_REL := 0

BUILD_PKG_TAG := $(shell test -d ".git" && git describe --always --tags --dirty)
ifeq (${BUILD_PKG_TAG},)
BUILD_PKG_TAG := ${BUILD_PKG_VERSION}
else
BUILD_PKG_REL := $(shell echo ${BUILD_PKG_TAG} | sed -En 's/[^-]*-([0-9]{1,})-[a-z0-9]{6,}(-dirty){0,1}$$/\1/p')
endif

ifneq ($(shell egrep -i 'id=(ubuntu|debian)' /etc/os-release),)
BUILD_PKG_TYPE ?= deb
BUILD_PKG_ARCH ?= $(shell dpkg-architecture -q DEB_HOST_ARCH)
else
BUILD_PKG_TYPE ?= rpm
BUILD_PKG_ARCH ?= $(shell uname -m)
endif

ifeq ($(wildcard scripts/${BUILD_PKG_TYPE}/CMakeLists.txt),)
$(error "Unable to create a ${BUILD_PKG_TYPE} package, try rpm or deb")
endif


# MPOOL_SRC_DIR is set to the top of the mpool source tree.
MPOOL_SRC_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Other dirs commonly accessed w/in this makefile:
S=$(MPOOL_SRC_DIR)/scripts

ifeq ($(findstring release,$(MAKECMDGOALS)),release)
	BUILD_TYPE := release
	BUILD_STYPE := r
else ifeq ($(findstring relwithdebug,$(MAKECMDGOALS)),relwithdebug)
	BUILD_TYPE := relwithdebug
	BUILD_STYPE := i
else ifeq ($(findstring relassert,$(MAKECMDGOALS)),relassert)
	BUILD_TYPE := relassert
	BUILD_STYPE := a
else ifeq ($(findstring optdebug,$(MAKECMDGOALS)),optdebug)
	BUILD_TYPE := optdebug
	BUILD_STYPE := o
else ifeq ($(findstring debug,$(MAKECMDGOALS)),debug)
	BUILD_TYPE := debug
	BUILD_STYPE := d
else
	BUILD_TYPE := release
	BUILD_STYPE := r
endif


BUILD_DIR     ?= ${MPOOL_SRC_DIR}/builds
BUILD_NODE    ?= $(shell uname -n)
BUILD_PKG_DIR ?= ${BUILD_DIR}/${BUILD_NODE}/${BUILD_PKG_TYPE}/${BUILD_TYPE}
CFILE         ?= $(S)/cmake/${BUILD_TYPE}.cmake
UBSAN         ?= 0
ASAN          ?= 0
BUILD_NUMBER  ?= 0

ifeq ($(findstring ubsan,$(MAKECMDGOALS)),ubsan)
UBSAN := 1
endif

ifeq ($(findstring asan,$(MAKECMDGOALS)),asan)
ASAN := 1
endif



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


define config-gen =
	(echo '# Note: When a variable is set multiple times in this file,' ;\
	echo '#       it is the *first* setting that sticks!' ;\
	echo '' ;\
	echo 'Set( BUILD_NUMBER "$(BUILD_NUMBER)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_TYPE "$(BUILD_TYPE)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_STYPE "$(BUILD_STYPE)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_TYPE "$(BUILD_PKG_TYPE)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_ARCH "$(BUILD_PKG_ARCH)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_REL "$(BUILD_PKG_REL)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_VERSION "$(BUILD_PKG_VERSION)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_TAG "$(BUILD_PKG_TAG)" CACHE STRING "" )' ;\
	echo 'Set( UBSAN "$(UBSAN)" CACHE BOOL "" )' ;\
	echo 'Set( ASAN "$(ASAN)" CACHE BOOL "" )' ;\
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

ifneq (${BTYPES},)
${BTYPES}: ${BTYPESDEP}
	@true
endif


# Delete the cmake config file if it has changed.
#
CONFIG = $(BUILD_PKG_DIR)/config.cmake

ifneq (${MAKECMDGOALS},config-preview)
$(shell $(config-gen) | cmp -s - ${CONFIG} || rm -f ${CONFIG})
endif


.PHONY: all allv allq allqv allvq ${BTYPES}
.PHONY: chkconfig clean config config-preview
.PHONY: distclean help install install-pre maintainer-clean
.PHONY: load package rebuild scrub smoke smokev sub_clone unload


# Goals in mostly alphabetical order.
#
all: config
	@$(MAKE) --no-print-directory -C "$(BUILD_PKG_DIR)" $(MF)

allv: config
	$(MAKE) -C "$(BUILD_PKG_DIR)" VERBOSE=1 $(MF)

allq: config
	$(MAKE) -C "$(BUILD_PKG_DIR)" $(MF) 2>&1 | $(PERL_CMAKE_NOISE_FILTER)

allqv allvq: config
	$(MAKE) -C "$(BUILD_PKG_DIR)" VERBOSE=1 $(MF) 2>&1 | $(PERL_CMAKE_NOISE_FILTER)

chkconfig:
	@./scripts/dev/mpool-chkconfig

clean: MAKEFLAGS += --no-print-directory
clean:
	if test -f ${BUILD_PKG_DIR}/src/Makefile ; then \
		$(MAKE) -C "$(BUILD_PKG_DIR)/src" clean ;\
		$(MAKE) -C "$(BUILD_PKG_DIR)/test" clean ;\
		find ${BUILD_PKG_DIR} -name \*.${BUILD_PKG_TYPE} -exec rm -f {} \; ;\
	fi

config-preview:
ifneq ($(wildcard ${CONFIG}),)
	@sed -En 's/^[^#]*\((.*)CACHE.*/\1/p' ${CONFIG}
endif
	@true

${CONFIG}: MAKEFLAGS += --no-print-directory
${CONFIG}: Makefile CMakeLists.txt $(wildcard scripts/${BUILD_PKG_TYPE}/*)
	mkdir -p $(BUILD_PKG_DIR)
	rm -f ${BUILD_PKG_DIR}/CMakeCache.txt
	@$(config-gen) > $@.tmp
	cmake $(DEPGRAPH) $(CMAKE_FLAGS) -B ${BUILD_PKG_DIR} -C $@.tmp -S ${MPOOL_SRC_DIR}
	$(MAKE) -C $(BUILD_PKG_DIR) clean
	mv $@.tmp $@

config: sub_clone ${CONFIG}

distclean scrub:
	rm -rf ${BUILD_PKG_DIR} *.${BUILD_PKG_TYPE}

help:
	$(info $(HELP_TEXT))
	@true

libs-clean:
	@rm -f /usr/lib/libmpool.*

install-pre: MAKEFLAGS += --no-print-directory
install-pre: libs-clean config
	@$(MAKE) -C "$(BUILD_PKG_DIR)" install

install: install-pre
	-modprobe mpool
	ldconfig
	systemd-tmpfiles --create /usr/lib/tmpfiles.d/mpool.conf
	udevadm control --reload-rules
	-systemctl enable mpool.service --now
	-systemctl restart mpool.service

load:
	modprobe -r mpool
	modprobe mpool

maintainer-clean: distclean
ifneq ($(wildcard ${SUBREPO_PATH_LIST}),)
	rm -rf ${SUBREPO_PATH_LIST}
endif

package: MAKEFLAGS += --no-print-directory
package: config
	-find ${BUILD_PKG_DIR} -name \*.${BUILD_PKG_TYPE} -exec rm -f {} \;
	$(MAKE) -C ${BUILD_PKG_DIR} package
	cp ${BUILD_PKG_DIR}/*.${BUILD_PKG_TYPE} .

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
