include(ExternalProject)

# libblkid
#
set( LIBBLKID_LIBS ${MPOOL_EXT_INSTALL_PREFIX}/lib/libmpool-blkid.so PARENT_SCOPE)

set( LIBBLKID_CFLAGS "-O2 -fPIC" )

# Build a minimal libblkid for our minimal needs
set( LIBBLKID_CONFIG_OPTS
     --disable-all-programs
     --enable-libblkid
   )

ExternalProject_Add(libblkid-pkg
  PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
  DOWNLOAD_COMMAND  rsync -axSH --info=progress2,misc1,remove,symsafe,stats,del --exclude .git ${MPOOL_SUB_REPOS}/util-linux/ ./libblkid-pkg/
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND  pwd && ./autogen.sh && ./configure -q --prefix=${MPOOL_EXT_INSTALL_PREFIX} ${LIBBLKID_CONFIG_OPTS} CFLAGS=${LIBBLKID_CFLAGS}

  BUILD_COMMAND
      COMMAND make

  INSTALL_COMMAND
      # Syntax for multiple commands under INSTALL_COMMAND
      COMMAND mkdir -p ${MPOOL_EXT_INSTALL_PREFIX}/lib
      COMMAND mkdir -p ${MPOOL_EXT_INSTALL_PREFIX}/include/mpool_blkid
      COMMAND pwd && cp ./libblkid/src/blkid.h ${MPOOL_EXT_INSTALL_PREFIX}/include/mpool_blkid/blkid.h
      COMMAND cp -p ./.libs/libblkid.so ${MPOOL_EXT_INSTALL_PREFIX}/lib/libmpool-blkid.so

  BUILD_IN_SOURCE   1)

install(
  FILES "${MPOOL_EXT_INSTALL_PREFIX}/lib/libmpool-blkid.so"
  DESTINATION lib
  COMPONENT runtime)
