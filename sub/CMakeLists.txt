include(ExternalProject)

if( NOT "${HAVE_LIBBLKID_2_32}" )

# libblkid
#
set( LIBBLKID_LIBS ${MPOOL_EXT_INSTALL_PREFIX}/lib/libmpool-blkid.so PARENT_SCOPE)

set( LIBBLKID_CFLAGS "-O2 -fPIC" )

# Build a minimal libblkid for our minimal needs
set( LIBBLKID_CONFIGURE_OPTS
     --disable-all-programs
     --enable-libblkid
   )

set( LIBBLKID_CONFIGURE_CMD
    ./configure -q --prefix=${MPOOL_EXT_INSTALL_PREFIX} ${LIBBLKID_CONFIGURE_OPTS} CFLAGS=${LIBBLKID_CFLAGS} )

set( LIBBLKID_DOWNLOAD_OPTS
    -axSH --info=progress2,misc1,remove,symsafe,stats --exclude '.git' --exclude '.rpm' --exclude '.deb' --delete )

set( LIBBLKID_DOWNLOAD_CMD
    rsync ${LIBBLKID_DOWNLOAD_OPTS} ${MPOOL_SUB_REPOS}/util-linux/ ./libblkid-pkg/ )

ExternalProject_Add(libblkid-pkg
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}"

    DOWNLOAD_COMMAND
        COMMAND echo ${LIBBLKID_DOWNLOAD_CMD}
	COMMAND ${LIBBLKID_DOWNLOAD_CMD}

    CONFIGURE_COMMAND
	COMMAND ./autogen.sh
        COMMAND echo ${LIBBLKID_CONFIGURE_CMD}
        COMMAND ${LIBBLKID_CONFIGURE_CMD}

    UPDATE_COMMAND ""

    BUILD_COMMAND
    COMMAND make

    # Syntax for multiple commands under INSTALL_COMMAND
    INSTALL_COMMAND
        COMMAND mkdir -p ${MPOOL_EXT_INSTALL_PREFIX}/lib
        COMMAND mkdir -p ${MPOOL_EXT_INSTALL_PREFIX}/include/mpool_blkid
        COMMAND pwd && cp ./libblkid/src/blkid.h ${MPOOL_EXT_INSTALL_PREFIX}/include/mpool_blkid/blkid.h
        COMMAND cp -p ./.libs/libblkid.so ${MPOOL_EXT_INSTALL_PREFIX}/lib/libmpool-blkid.so
	COMMAND ln -fs ${MPOOL_EXT_INSTALL_PREFIX}/lib/libmpool-blkid.so ${MPOOL_EXT_INSTALL_PREFIX}/lib/libblkid.so.1

    BUILD_IN_SOURCE   1
    )

install(
  FILES "${MPOOL_EXT_INSTALL_PREFIX}/lib/libmpool-blkid.so"
  DESTINATION lib
  COMPONENT runtime)

else()

set( LIBBLKID_LIBS blkid PARENT_SCOPE)

file( MAKE_DIRECTORY ${MPOOL_EXT_INSTALL_PREFIX}/include/mpool_blkid )
file( COPY /usr/include/blkid/blkid.h
    DESTINATION ${MPOOL_EXT_INSTALL_PREFIX}/include/mpool_blkid )

endif()
