#!/bin/sh

# this script will download libssh2 source and build static library, then build sshul.
# you need to install one of OpenSSL, Libgcrypt, mbedTLS by youself and modify 'CRYPTO' below.
# if you have already installed libssh2, just run command "make".

LIBSSH2=libssh2-1.9.0
# OpenSSL, Libgcrypt, mbedTLS
CRYPTO=OpenSSL

case $CRYPTO in
    OpenSSL)
        LINKFLAGS="-lcrypto"
        ;;
    Libgcrypt)
        LINKFLAGS="-lgcrypt"
        ;;
    mbedTLS)
        LINKFLAGS="-lmbedcrypto"
        ;;
    *)
        echo "unsupported crypto \"$CRYPTO\""
        exit 1
        ;;
esac

if [ ! -f $LIBSSH2/CMakeLists.txt ]; then
    if [ ! -f $LIBSSH2.tar.gz ]; then
        wget -O $LIBSSH2.tar.gz https://www.libssh2.org/download/$LIBSSH2.tar.gz
        if [ $? -ne 0 ]; then
            echo "download $LIBSSH2.tar.gz failed"
            exit 1
        fi
    fi
    tar -xzf $LIBSSH2.tar.gz
fi

if [ ! -d $LIBSSH2 ]; then
    echo "unpack $LIBSSH2 failed"
    exit 1
fi

cd $LIBSSH2

if [ ! -f libssh2.a ]; then
    mkdir build 2>/dev/null
    # cmake
    which cmake > /dev/null
    if [ $? -ne 0 ]; then
        echo "cmake not found"
        exit 1
    fi
    echo "use cmake to build libssh2"
    cd build
    cmake -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
            -DENABLE_DEBUG_LOGGING=OFF ENABLE_ZLIB_COMPRESSION=OFF \
            -DCRYPTO_BACKEND=$CRYPTO ..
    cmake --build .
    cd ..
    mv build/src/libssh2.a .
    # automake
<<!
    echo "use automake to build libssh2"
    ./buildconf
    if [ $? -ne 0 ]; then
        echo "buildconf failed"
        exit 1
    fi
    ./configure --enable-shared=no --disable-debug --disable-examples-build \
            --with-crypto=openssl --prefix=$(pwd)/build
    make
    mv src/.libs/libssh2.a .
!
    if [ ! -f libssh2.a ]; then
        echo "build libssh2.a failed"
        exit 1
    fi
fi

cd ..

make LDFLAGS="$LIBSSH2/libssh2.a $LINKFLAGS" CFLAGS="-I$LIBSSH2/include" \
        MAKE_VERSION="release"

echo "build done, run 'make install' to install"

exit 0