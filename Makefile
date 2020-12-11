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
  help      -- Print this message.
  install   -- Install build artifacts locally
  package   -- Build "all" and generate deb/rpm packages
  smoke     -- Run smoke tests

Target Modiiers:

  asan      -- Enable address sanity checking
  debug     -- Create a debug build
  optdebug  -- Create a debug build with -Og
  release   -- Create a release build
  relassert -- Create a release build with assert enabled
  ubsan     -- Enable undefined behavior checking

Configuration Variables:

  The following configuration variables can be passed via the command line,
  environment, or ~/mpool.mk to customize the build.

    BUILD_DIR         -- The top-level build output directory
    BUILD_NUMBER      -- Build job number (as set by Jenkins)
    BUILD_PKG_TYPE    -- Specify package type (rpm or deb)
    BUILD_PKG_VENDOR  -- Specify the vendor/maintainer tag in the package
    DEPGRAPH          -- Set to "--graphviz=<filename_prefix>" to generate
                         graphviz dependency graph files

  Defaults (not all are customizable):

    BUILD_DIR          $(BUILD_DIR)
    BUILD_NODE         $(BUILD_NODE)
    BUILD_NUMBER       $(BUILD_NUMBER)
    BUILD_TYPE         $(BUILD_TYPE)
    BUILD_STYPE        $(BUILD_STYPE)
    BUILD_CFLAGS       $(BUILD_CFLAGS)
    BUILD_CDEFS        $(BUILD_CDEFS)
    BUILD_PKG_ARCH     ${BUILD_PKG_ARCH}
    BUILD_PKG_DIR      ${BUILD_PKG_DIR}
    BUILD_PKG_DIST     ${BUILD_PKG_DIST}
    BUILD_PKG_REL      ${BUILD_PKG_REL}
    BUILD_PKG_SHA      ${BUILD_PKG_SHA}
    BUILD_PKG_TAG      ${BUILD_PKG_TAG}
    BUILD_PKG_TYPE     ${BUILD_PKG_TYPE}
    BUILD_PKG_VERSION  ${BUILD_PKG_VERSION}
    BUILD_PKG_VENDOR   ${BUILD_PKG_VENDOR}
    BUILD_PKG_VQUAL    ${BUILD_PKG_VQUAL}
    CMAKE_BUILD_TYPE   ${CMAKE_BUILD_TYPE}

Examples:

  Create a 'release' package:

    make -j package

  Rebuild the bulk of mpool code, leaving the code in external repos alone:

    make -j clean all

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


# Edit the package VERSION and QUALifier when we cut a release branch or tag:
BUILD_PKG_VERSION := 1.9.0
BUILD_PKG_VQUAL := ''

BUILD_PKG_VENDOR ?= "Micron Technology, Inc."

BUILD_PKG_SHA := $(shell test -d ".git" && git rev-parse HEAD 2>/dev/null)

BUILD_PKG_TAG := $(shell test -d ".git" && \
	git describe --always --long --tags --dirty --abbrev=10 2>/dev/null)

ifeq (${BUILD_PKG_TAG},)
BUILD_PKG_TAG := ${BUILD_PKG_VERSION}
BUILD_PKG_REL := 0
BUILD_PKG_SHA := 0
else
BUILD_PKG_REL := $(shell echo ${BUILD_PKG_TAG} | \
	sed -En 's/.*-([0-9]+)-[a-z0-9]{7,}(-dirty){0,1}$$/\1/p')
BUILD_PKG_VQUAL := $(shell echo ${BUILD_PKG_TAG} | \
	sed -En 's/.*-([^-]+)-[0-9]+-[a-z0-9]{7,}(-dirty){0,1}$$/~\1/p')
endif

ifneq ($(shell egrep -i 'id=(ubuntu|debian)' /etc/os-release),)
BUILD_PKG_TYPE ?= deb
BUILD_PKG_ARCH ?= $(shell dpkg-architecture -q DEB_HOST_ARCH)
BUILD_PKG_DIST :=
else
BUILD_PKG_TYPE ?= rpm
BUILD_PKG_ARCH ?= $(shell uname -m)
BUILD_PKG_DIST := $(shell rpm --eval '%{?dist}')
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
	BUILD_CFLAGS := -O2
	BUILD_CDEFS := -DMPOOL_BUILD_RELEASE
	CMAKE_BUILD_TYPE := Release
else ifeq ($(findstring relwithdebug,$(MAKECMDGOALS)),relwithdebug)
	BUILD_TYPE := relwithdebug
	BUILD_STYPE := i
	BUILD_CFLAGS := -O2
	BUILD_CDEFS := -DMPOOL_BUILD_RELEASE
	CMAKE_BUILD_TYPE := RelWithDebInfo
else ifeq ($(findstring relassert,$(MAKECMDGOALS)),relassert)
	BUILD_TYPE := relassert
	BUILD_STYPE := a
	BUILD_CFLAGS := -O2
	BUILD_CDEFS := -DMPOOL_BUILD_RELASSERT -D_FORTIFY_SOURCE=2
	CMAKE_BUILD_TYPE := Debug
else ifeq ($(findstring optdebug,$(MAKECMDGOALS)),optdebug)
	BUILD_TYPE := optdebug
	BUILD_STYPE := o
	BUILD_CFLAGS := -Og
	BUILD_CDEFS := -DMPOOL_BUILD_DEBUG
	CMAKE_BUILD_TYPE := Debug
else ifeq ($(findstring debug,$(MAKECMDGOALS)),debug)
	BUILD_TYPE := debug
	BUILD_STYPE := d
	BUILD_CFLAGS := -fstack-protector-all
	BUILD_CDEFS := -DMPOOL_BUILD_DEBUG -DDEBUG_RCU
	CMAKE_BUILD_TYPE := Debug
else
	BUILD_TYPE := release
	BUILD_STYPE := r
	BUILD_CFLAGS := -O2
	BUILD_CDEFS := -DMPOOL_BUILD_RELEASE
	CMAKE_BUILD_TYPE := Release
endif


BUILD_DIR     ?= ${MPOOL_SRC_DIR}/builds
BUILD_NODE    ?= $(shell uname -n)
BUILD_PKG_DIR ?= ${BUILD_DIR}/${BUILD_NODE}/${BUILD_PKG_TYPE}/${BUILD_TYPE}
UBSAN         ?= 0
ASAN          ?= 0
BUILD_NUMBER  ?= 0

ifeq ($(findstring ubsan,$(MAKECMDGOALS)),ubsan)
UBSAN := 1
endif

ifeq ($(findstring asan,$(MAKECMDGOALS)),asan)
ASAN := 1
endif


# Developers can set HAVE_LIBBLKID_2_32=1 in their environment to avoid
# building libblkid for one-off builds not intended for distribution.
#
HAVE_LIBBLKID_2_32 ?= 0
SUBREPO_PATH_LIST :=

ifeq (${HAVE_LIBBLKID_2_32},0)

blkid_repo := util-linux
${blkid_repo}_url := https://github.com/hse-project/util-linux
${blkid_repo}_tag := v2.32

SUBREPO_PATH_LIST := sub/$(blkid_repo)
endif


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
# Must export BUILD_PKG_DIR as MPOOL_BUILD_DIR
#
RUN_CTEST = export MPOOL_BUILD_DIR="$(BUILD_PKG_DIR)"; set -e -u; cd "$$MPOOL_BUILD_DIR"; ctest --output-on-failure $(CTEST_FLAGS)

# Run tests with label "smoke"
RUN_CTEST_SMOKE = ${RUN_CTEST} -L "smoke"


define config-gen =
	(echo '# Note: When a variable is set multiple times in this file,' ;\
	echo '#       it is the *first* setting that sticks!' ;\
	echo '' ;\
	echo 'Set( BUILD_NUMBER        "$(BUILD_NUMBER)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_TYPE          "$(BUILD_TYPE)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_STYPE         "$(BUILD_STYPE)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_CFLAGS        "$(BUILD_CFLAGS)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_CDEFS         "$(BUILD_CDEFS)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_ARCH      "$(BUILD_PKG_ARCH)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_DIST      "$(BUILD_PKG_DIST)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_REL       "$(BUILD_PKG_REL)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_SHA       "$(BUILD_PKG_SHA)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_TAG       "$(BUILD_PKG_TAG)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_TYPE      "$(BUILD_PKG_TYPE)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_VERSION   "$(BUILD_PKG_VERSION)" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_VENDOR    "'$(BUILD_PKG_VENDOR)'" CACHE STRING "" )' ;\
	echo 'Set( BUILD_PKG_VQUAL     "$(BUILD_PKG_VQUAL)" CACHE STRING "" )' ;\
	echo 'Set( CMAKE_BUILD_TYPE    "$(CMAKE_BUILD_TYPE)" CACHE STRING "" )' ;\
	echo 'Set( UBSAN               "$(UBSAN)" CACHE BOOL "" )' ;\
	echo 'Set( ASAN                "$(ASAN)" CACHE BOOL "" )' ;\
	echo 'Set( HAVE_LIBBLKID_2_32  "$(HAVE_LIBBLKID_2_32)" CACHE BOOL "" )' ;\
	)
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

ifeq ($(filter config-preview help print-% printq-% smoke load unload,$(MAKECMDGOALS)),)
$(shell $(config-gen) | cmp -s - ${CONFIG} || rm -f ${CONFIG})
endif


.PHONY: all allv allq allqv allvq ${BTYPES}
.PHONY: chkconfig clean config config-preview
.PHONY: distclean help install install-pre maintainer-clean
.PHONY: load package rebuild scrub smoke smokev unload


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
	mkdir -p $(@D)
	rm -f ${@D}/CMakeCache.txt
	@$(config-gen) > $@.tmp
	cd ${@D} && cmake $(DEPGRAPH) $(CMAKE_FLAGS) -C $@.tmp ${MPOOL_SRC_DIR}
	$(MAKE) -C $(@D) clean
	mv $@.tmp $@

config: ${SUBREPO_PATH_LIST} ${CONFIG}

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

maintainer-clean:
	rm -rf ${BUILD_DIR} *.rpm *.deb
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

ifneq (${SUBREPO_PATH_LIST},)
${SUBREPO_PATH_LIST}:
	rm -rf $@ $@.tmp
	git clone $($(@F)_url).git $@.tmp
	cd $@.tmp && git checkout $($(@F)_tag)
	mv $@.tmp $@
endif

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
