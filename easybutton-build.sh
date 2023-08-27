#!/bin/sh

# this script will download libssh2 and libmbedtls source to build sshul staticly.

# comment this line to link share mbedtls library
LIBMBED_VER="2.28.4"
# comment this line to link share libssh2 library
LIBSSH2_VER="1.11.0"
# build Release or Debug
BUILD_TYPE=Release

CMAKE_EXTRA_ARGS=
SSHUL_ROOT=$(pwd)

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

    CMAKE_EXTRA_ARGS="-DLIBMBED_LIBPATH=$MBED_ROOT/build/library $CMAKE_EXTRA_ARGS"
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
        if [ $LIBMBED_VER ]; then
            cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF \
                -DBUILD_TESTING=OFF -DENABLE_DEBUG_LOGGING=OFF -DCLEAR_MEMORY=OFF \
                -DENABLE_ZLIB_COMPRESSION=OFF -DCRYPTO_BACKEND=mbedTLS -DMBEDTLS_INCLUDE_DIR=$MBED_INC \
                -DMBEDTLS_LIBRARY=$MBEDTLS_LIB -DMBEDX509_LIBRARY=$MBEDX509_LIB -DMBEDCRYPTO_LIBRARY=$MBEDCRYPTO_LIB ..
        else
            cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF \
                -DBUILD_TESTING=OFF -DENABLE_DEBUG_LOGGING=OFF -DCLEAR_MEMORY=OFF \
                -DENABLE_ZLIB_COMPRESSION=OFF -DCRYPTO_BACKEND=mbedTLS ..
        fi
        cmake --build .

        cd $SSHUL_ROOT

        if [ ! -f $SSH2_LIB ]; then
            echo "build libssh2.a failed"
            exit 1
        fi
    fi

    CMAKE_EXTRA_ARGS="-DLIBSSH2_INCPATH=$SSH2_INC -DLIBSSH2_LIBPATH=$SSH2_ROOT/build/src $CMAKE_EXTRA_ARGS"
fi

echo "build sshul"
if [ ! -d build ]; then
    mkdir build 
    cd build
    # echo "-- $CMAKE_EXTRA_ARGS"
    cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_SKIP_RPATH=TRUE $CMAKE_EXTRA_ARGS ..
    cd ..
fi
cmake --build build

if [ $? -ne 0 ]; then
    echo "build sshul failed"
else
    echo "build sshul done"
fi
