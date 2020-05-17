@echo off

set libmbed=%~dp0%mbedtls-2.16.6
set libssh2=%~dp0%libssh2-1.9.0

where cmake > nul

if %errorlevel%==1 (
    echo cmake not found
    pause
    exit /b 1
)

where mingw32-make > nul

if %errorlevel%==1 (
    echo mingw32 not found
    pause
    exit /b 1
)

if not exist %libmbed%\CMakeLists.txt (
    echo libmbedtls not found
    pause
    exit /b 1
)

set mbed_inc=%libmbed%\include
set mbedtls_lib=%libmbed%\build\library\libmbedtls.a
set mbedx509_lib=%libmbed%\build\library\libmbedx509.a
set mbedcrypto_lib=%libmbed%\build\library\libmbedcrypto.a

if not exist %mbedcrypto_lib% (
    echo build mbedtls

    mkdir %libmbed%\build > nul
    cd %libmbed%\build

    cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ^
        -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DENABLE_ZLIB_SUPPORT=OFF ^
        -DINSTALL_MBEDTLS_HEADERS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF ..
    cmake --build .

    cd ..\..

    if not exist %mbedcrypto_lib% (
        echo build libmbedtls failed
        pause
        exit /b 1
    )
)

if not exist %libssh2%\CMakeLists.txt (
    echo libssh2 not found
    pause
    exit /b 1
)

set ssh2_inc=%libssh2%\include
set ssh2_lib=%libssh2%\build\src\libssh2.a

if not exist %ssh2_lib% (
    echo build libssh2

    mkdir %libssh2%\build > nul
    cd %libssh2%\build

    cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF ^
        -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DENABLE_DEBUG_LOGGING=OFF ^
        -DCLEAR_MEMORY=OFF -DENABLE_ZLIB_COMPRESSION=OFF -DCRYPTO_BACKEND=mbedTLS ^
        -DMBEDTLS_INCLUDE_DIR=%mbed_inc% -DMBEDTLS_LIBRARY=%mbedtls_lib% ^
        -DMBEDX509_LIBRARY=%mbedx509_lib% -DMBEDCRYPTO_LIBRARY=%mbedcrypto_lib% ..
    cmake --build .

    cd ..\..

    if not exist %ssh2_lib% (
        echo build libssh2 failed
        pause
        exit /b 1
    )
)

echo build sshul
mingw32-make CC=gcc MAKE_VERSION=release ^
     CFLAGS=%ssh2_inc% LDFLAGS="%ssh2_lib% %mbedcrypto_lib% -lws2_32"

if %errorlevel% == 1 (
    echo build sshul failed
) else (
    echo build sshul done
)

pause