#!/bin/sh

# this script will download libssh2 and libmbedtls source to build sshul staticly.
# if you have already installed one of OpenSSL, Libgcrypt, mbedTLS,
#     just modify 'CRYPTO' and comment 'LIBMBED' below.
# if you have already installed libssh2, just run command "make".

# one of OpenSSL, Libgcrypt, mbedTLS
CRYPTO="OpenSSL"
# comment this line to link share 'CRYPTO' library
LIBMBED="mbedtls-2.16.11"
# comment this line to link share libssh2 library
LIBSSH2="libssh2-1.9.0"

CFLAGS=
LDFLAGS=
SSHUL_ROOT=$(pwd)

which cmake > /dev/null
if [ $? -ne 0 ]; then
    echo "cmake not found"
    exit 1
fi

if [ $LIBMBED ]; then
    # download and build static libmbedtls

    MBED_ROOT="$SSHUL_ROOT/mbedtls-$LIBMBED"
    MBED_INC="$MBED_ROOT/include"
    MBEDTLS_LIB="$MBED_ROOT/build/library/libmbedtls.a"
    MBEDX509_LIB="$MBED_ROOT/build/library/libmbedx509.a"
    MBEDCRYPTO_LIB="$MBED_ROOT/build/library/libmbedcrypto.a"

    # download
    if [ ! -f $MBED_ROOT/CMakeLists.txt ]; then
        if [ ! -f $LIBMBED.tar.gz ]; then
            wget https://github.com/ARMmbed/mbedtls/archive/refs/tags/$LIBMBED.tar.gz
            if [ $? -ne 0 ]; then
                echo "download $LIBMBED.tar.gz failed"
                exit 1
            fi
        fi

        tar -xzf $LIBMBED.tar.gz

        if [ ! -f $MBED_ROOT/CMakeLists.txt ]; then
            echo "unpack $LIBMBED.tar.gz failed"
            exit 1
        fi
    fi

    # build
    if [ ! -f $MBEDCRYPTO_LIB ]; then
        mkdir $MBED_ROOT/build 2>/dev/null
        cd $MBED_ROOT/build

        echo "use cmake to build libmbedtls"
        cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
            -DENABLE_ZLIB_SUPPORT=OFF -DINSTALL_MBEDTLS_HEADERS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF ..
        cmake --build .

        cd $SSHUL_ROOT

        if [ ! -f $MBEDCRYPTO_LIB ]; then
            echo "build libmbedtls.a failed"
            exit 1
        fi
    fi

    CRYPTO="mbedTLS"
    LDFLAGS="$MBEDCRYPTO_LIB $LDFLAGS"
else
    case $CRYPTO in
        OpenSSL)
            LDFLAGS="-lcrypto $LDFLAGS"
            ;;
        Libgcrypt)
            LDFLAGS="-lgcrypt $LDFLAGS"
            ;;
        mbedTLS)
            LDFLAGS="-lmbedcrypto $LDFLAGS"
            ;;
        *)
            echo "unsupported crypto \"$CRYPTO\""
            exit 1
            ;;
    esac
fi

if [ $LIBSSH2 ]; then
    # download and build static libssh2

    SSH2_ROOT="$SSHUL_ROOT/libssh2-$LIBSSH2"
    SSH2_INC="$SSH2_ROOT/include"
    SSH2_LIB="$SSH2_ROOT/build/src/libssh2.a"

    # download
    if [ ! -f $SSH2_ROOT/CMakeLists.txt ]; then
        if [ ! -f $LIBSSH2.tar.gz ]; then
            wget https://github.com/libssh2/libssh2/archive/refs/tags/$LIBSSH2.tar.gz
            if [ $? -ne 0 ]; then
                echo "download $LIBSSH2.tar.gz failed"
                exit 1
            fi
        fi

        tar -xzf $LIBSSH2.tar.gz

        if [ ! -f $SSH2_ROOT/CMakeLists.txt ]; then
            echo "unpack $LIBSSH2.tar.gz failed"
            exit 1
        fi
    fi

    # build
    if [ ! -f $SSH2_LIB ]; then
        mkdir $SSH2_ROOT/build 2>/dev/null
        cd $SSH2_ROOT/build

        echo "use cmake to build libssh2"
        if [ $LIBMBED ]; then
            cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF \
                -DBUILD_TESTING=OFF -DENABLE_DEBUG_LOGGING=OFF -DCLEAR_MEMORY=OFF \
                -DENABLE_ZLIB_COMPRESSION=OFF -DCRYPTO_BACKEND=$CRYPTO -DMBEDTLS_INCLUDE_DIR=$MBED_INC \
                -DMBEDTLS_LIBRARY=$MBEDTLS_LIB -DMBEDX509_LIBRARY=$MBEDX509_LIB -DMBEDCRYPTO_LIBRARY=$MBEDCRYPTO_LIB ..
        else
            cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF \
                -DBUILD_TESTING=OFF -DENABLE_DEBUG_LOGGING=OFF -DCLEAR_MEMORY=OFF \
                -DENABLE_ZLIB_COMPRESSION=OFF -DCRYPTO_BACKEND=$CRYPTO ..
        fi
        cmake --build .

        cd $SSHUL_ROOT

        if [ ! -f $SSH2_LIB ]; then
            echo "build libssh2.a failed"
            exit 1
        fi
    fi

    CFLAGS="-I$SSH2_INC $CFLAGS"
    LDFLAGS="$SSH2_LIB $LDFLAGS"
else
    LDFLAGS="-lssh2 $LDFLAGS"
fi

echo "build sshul"
echo "-- LDFLAGS=$LDFLAGS"
echo "-- CFLAGS=$CFLAGS"
make LDFLAGS="$LDFLAGS" CFLAGS="$CFLAGS" BUILD_TYPE="release"

if [ $? -ne 0 ]; then
    echo "build sshul failed"
else
    echo "build sshul done, run 'make install' to install"
fi

exit 0