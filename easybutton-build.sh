#!/bin/sh

# this script will download libssh2 and libmbedtls source to build sshul staticly.

# comment this line to link share mbedtls library
LIBMBED_VER="2.28.4"
# comment this line to link share zlib library
LIBZLIB_VER="1.3"
# comment this line to link share libssh2 library
LIBSSH2_VER="1.11.0"
# build Release or Debug
BUILD_TYPE=Release

SSHUL_ROOT=$(pwd)
SSHUL_CMAKE_EXTARGS=
SSH2_CMAKE_EXTARGS=

which cmake > /dev/null
if [ $? -ne 0 ]; then
    echo "cmake not found"
    exit 1
fi

if [ $LIBMBED_VER ]; then
    # download and build static libmbedtls

    MBED_ROOT="$SSHUL_ROOT/mbedtls-$LIBMBED_VER"
    MBED_INC="$MBED_ROOT/include"
    MBEDTLS_LIB="$MBED_ROOT/build/library/libmbedtls.a"
    MBEDX509_LIB="$MBED_ROOT/build/library/libmbedx509.a"
    MBEDCRYPTO_LIB="$MBED_ROOT/build/library/libmbedcrypto.a"

    # download
    if [ ! -f $MBED_ROOT/CMakeLists.txt ]; then
        if [ ! -f mbedtls-$LIBMBED_VER.tar.gz ]; then
            wget https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v$LIBMBED_VER.tar.gz
            if [ $? -ne 0 ]; then
                echo "download mbedtls-$LIBMBED_VER.tar.gz failed"
                exit 1
            fi
        fi

        tar xf mbedtls-$LIBMBED_VER.tar.gz

        if [ ! -f $MBED_ROOT/CMakeLists.txt ]; then
            echo "unpack mbedtls-$LIBMBED_VER.tar.gz failed"
            exit 1
        fi
    fi

    # build
    if [ ! -f $MBEDCRYPTO_LIB ]; then
        mkdir $MBED_ROOT/build 2>/dev/null
        cd $MBED_ROOT/build

        echo "use cmake to build libmbedtls"
        cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
            -DENABLE_ZLIB_SUPPORT=OFF -DINSTALL_MBEDTLS_HEADERS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF ..
        cmake --build .

        cd $SSHUL_ROOT

        if [ ! -f $MBEDCRYPTO_LIB ]; then
            echo "build libmbedtls.a failed"
            exit 1
        fi
    fi

    SSHUL_CMAKE_EXTARGS="-DLIBMBED_LIBPATH=$MBED_ROOT/build/library $SSHUL_CMAKE_EXTARGS"
    SSH2_CMAKE_EXTARGS="-DMBEDTLS_INCLUDE_DIR=$MBED_INC -DMBEDTLS_LIBRARY=$MBEDTLS_LIB \
-DMBEDX509_LIBRARY=$MBEDX509_LIB -DMBEDCRYPTO_LIBRARY=$MBEDCRYPTO_LIB $SSH2_CMAKE_EXTARGS"
fi

if [ $LIBZLIB_VER ]; then
    # download and build static zlib

    ZLIB_ROOT="$SSHUL_ROOT/zlib-$LIBZLIB_VER"
    ZLIB_INC="$ZLIB_ROOT/build/include"
    ZLIB_LIB="$ZLIB_ROOT/build/lib/libz.a"

    # download
    if [ ! -f $ZLIB_ROOT/CMakeLists.txt ]; then
        if [ ! -f zlib-$LIBZLIB_VER.tar.xz ]; then
            wget https://www.zlib.net/zlib-$LIBZLIB_VER.tar.xz
            if [ $? -ne 0 ]; then
                echo "download zlib-$LIBZLIB_VER.tar.xz failed"
                exit 1
            fi
        fi

        tar xf zlib-$LIBZLIB_VER.tar.xz

        if [ ! -f $ZLIB_ROOT/CMakeLists.txt ]; then
            echo "unpack zlib-$LIBZLIB_VER.tar.xz failed"
            exit 1
        fi
    fi

    # build
    if [ ! -f $ZLIB_LIB ]; then
        mkdir $ZLIB_ROOT/build 2>/dev/null
        cd $ZLIB_ROOT/build

        echo "use cmake to build zlib"
        cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_INSTALL_PREFIX=$ZLIB_ROOT/build -DSKIP_INSTALL_FILES=ON ..
        cmake --build . && cmake --install . && rm $ZLIB_ROOT/build/lib/libz.so*

        cd $SSHUL_ROOT

        if [ ! -f $ZLIB_LIB ]; then
            echo "build libz.a failed"
            exit 1
        fi
    fi

    SSHUL_CMAKE_EXTARGS="-DLIBZLIB_LIBPATH=$ZLIB_ROOT/build/lib $SSHUL_CMAKE_EXTARGS"
    SSH2_CMAKE_EXTARGS="-DZLIB_ROOT=$ZLIB_ROOT/build $SSH2_CMAKE_EXTARGS"
fi

if [ $LIBSSH2_VER ]; then
    # download and build static libssh2

    SSH2_ROOT="$SSHUL_ROOT/libssh2-$LIBSSH2_VER"
    SSH2_INC="$SSH2_ROOT/include"
    SSH2_LIB="$SSH2_ROOT/build/src/libssh2.a"

    # download
    if [ ! -f $SSH2_ROOT/CMakeLists.txt ]; then
        if [ ! -f libssh2-$LIBSSH2_VER.tar.xz ]; then
            wget https://github.com/libssh2/libssh2/releases/download/libssh2-$LIBSSH2_VER/libssh2-$LIBSSH2_VER.tar.xz
            if [ $? -ne 0 ]; then
                echo "download libssh2-$LIBSSH2_VER.tar.xz failed"
                exit 1
            fi
        fi

        tar xf libssh2-$LIBSSH2_VER.tar.xz

        if [ ! -f $SSH2_ROOT/CMakeLists.txt ]; then
            echo "unpack libssh2-$LIBSSH2_VER.tar.xz failed"
            exit 1
        fi
    fi

    # build
    if [ ! -f $SSH2_LIB ]; then
        mkdir $SSH2_ROOT/build 2>/dev/null
        cd $SSH2_ROOT/build

        echo "use cmake to build libssh2"
        cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF \
            -DBUILD_TESTING=OFF -DENABLE_DEBUG_LOGGING=OFF -DCLEAR_MEMORY=OFF \
            -DENABLE_ZLIB_COMPRESSION=ON -DCRYPTO_BACKEND=mbedTLS $SSH2_CMAKE_EXTARGS ..
        cmake --build .

        cd $SSHUL_ROOT

        if [ ! -f $SSH2_LIB ]; then
            echo "build libssh2.a failed"
            exit 1
        fi
    fi

    SSHUL_CMAKE_EXTARGS="-DLIBSSH2_INCPATH=$SSH2_INC -DLIBSSH2_LIBPATH=$SSH2_ROOT/build/src $SSHUL_CMAKE_EXTARGS"
fi

echo "build sshul"
if [ ! -d build/CMakeCache.txt ]; then
    mkdir -p build 
    cd build
    # echo "-- $SSHUL_CMAKE_EXTARGS"
    cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_SKIP_RPATH=TRUE $SSHUL_CMAKE_EXTARGS ..
    cd ..
fi
cmake --build build

if [ $? -ne 0 ]; then
    echo "build sshul failed"
else
    echo "build sshul done"
fi
