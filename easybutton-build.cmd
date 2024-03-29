@echo off

set libmbed=%~dp0mbedtls-2.28.4
set libssh2=%~dp0libssh2-1.11.0
set libzlib=%~dp0zlib-1.3
set build_type=Release

where cmake > nul
if %errorlevel%==1 (
    echo cmake not found
    pause
    exit /b 1
)

where nmake > nul
if %errorlevel%==1 (
    where mingw32-make > nul
    if %errorlevel%==1 (
        echo both nmake and mingw32-make not found
        pause
        exit /b 1
    )
    set usenmake=0
    set cmakegen="MinGW Makefiles"
) else (
    set usenmake=1
    set cmakegen="NMake Makefiles"
)

@REM builld libmbedtls

if not exist %libmbed%\CMakeLists.txt (
    echo %libmbed%\CMakeLists.txt not found
    pause
    exit /b 1
)

set mbed_inc=%libmbed%\include
if %usenmake%==1 (
    set mbedtls_lib=%libmbed%\build\library\mbedtls.lib
    set mbedx509_lib=%libmbed%\build\library\mbedx509.lib
    set mbedcrypto_lib=%libmbed%\build\library\mbedcrypto.lib
) else (
    set mbedtls_lib=%libmbed%\build\library\libmbedtls.a
    set mbedx509_lib=%libmbed%\build\library\libmbedx509.a
    set mbedcrypto_lib=%libmbed%\build\library\libmbedcrypto.a
)

if not exist %mbedcrypto_lib% (
    echo build mbedtls

    mkdir %libmbed%\build > nul
    cd %libmbed%\build

    cmake -G %cmakegen% -DCMAKE_BUILD_TYPE=%build_type% ^
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

@REM build zlib

if not exist %libzlib%\CMakeLists.txt (
    echo %libzlib%\CMakeLists.txt not found
    pause
    exit /b 1
)

set zlib_inc=%libzlib%\build\include
if %usenmake%==1 (
    set zlib_lib=%libzlib%\build\lib\zlibstatic.lib
) else (
    @REM TODO
    set zlib_lib=%libzlib%\build\lib\libz.a
)

if not exist %zlib_lib% (
    echo build zlib

    mkdir %libzlib%\build > nul
    cd %libzlib%\build

    cmake -G %cmakegen% -DCMAKE_BUILD_TYPE=%build_type% ^
        -DCMAKE_INSTALL_PREFIX=%libzlib%\build -DSKIP_INSTALL_FILES=ON ..
    cmake --build .
    cmake --install .

    cd ..\..

    if not exist %zlib_lib% (
        echo build zlib failed
        pause
        exit /b 1
    )
)

@REM build libssh2

if not exist %libssh2%\CMakeLists.txt (
    echo %libssh2%\CMakeLists.txt not found
    pause
    exit /b 1
)

set ssh2_inc=%libssh2%\include
if %usenmake%==1 (
    set ssh2_lib=%libssh2%\build\src\libssh2.lib
) else (
    set ssh2_lib=%libssh2%\build\src\libssh2.a
)

if not exist %ssh2_lib% (
    echo build libssh2

    mkdir %libssh2%\build > nul
    cd %libssh2%\build

    cmake -G %cmakegen% -DCMAKE_BUILD_TYPE=%build_type% -DBUILD_SHARED_LIBS=OFF ^
        -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DENABLE_DEBUG_LOGGING=OFF ^
        -DCLEAR_MEMORY=OFF -DENABLE_ZLIB_COMPRESSION=ON -DCRYPTO_BACKEND=mbedTLS ^
        -DMBEDTLS_INCLUDE_DIR=%mbed_inc% -DMBEDTLS_LIBRARY=%mbedtls_lib% ^
        -DMBEDX509_LIBRARY=%mbedx509_lib% -DMBEDCRYPTO_LIBRARY=%mbedcrypto_lib% ^
        -DZLIB_ROOT=%libzlib%\build ..
    cmake --build .

    cd ..\..

    if not exist %ssh2_lib% (
        echo build libssh2 failed
        pause
        exit /b 1
    )
)

echo build sshul

if not exist build\CMakeCache.txt (
    mkdir build
    cd build
    cmake -G %cmakegen% -DCMAKE_BUILD_TYPE=%build_type% -DLIBSSH2_INCPATH=%ssh2_inc% ^
        -DLIBSSH2_LIBPATH=%libssh2%\build\src -DLIBMBED_LIBPATH=%libmbed%\build\library ^
        -DLIBZLIB_LIBPATH=%libzlib%\build\lib ..
    cd ..
)
cmake --build build

pause