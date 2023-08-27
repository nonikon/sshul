cmake_minimum_required(VERSION 3.4)

project(sshul LANGUAGES C)

set(LIBSSH2_INCPATH "" CACHE PATH "libssh2 include path")
set(LIBSSH2_LIBPATH "" CACHE PATH "libssh2 library path")
set(LIBMBED_LIBPATH "" CACHE PATH "mbedtls library path")

set(sshul_sources
    main.c
    match.c
    config.c
    db.c
    ssh_session.c
    json.c
    xlist.c
    xstring.c
    xhash.c
    md5.c
)

add_executable(sshul ${sshul_sources})
target_include_directories(sshul PRIVATE ${LIBSSH2_INCPATH})
target_link_directories(sshul PRIVATE ${LIBSSH2_LIBPATH} ${LIBMBED_LIBPATH})
if(WIN32)
    target_link_libraries(sshul ws2_32)
endif()
if(MSVC)
    target_link_libraries(sshul libssh2)
    target_compile_definitions(sshul PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(sshul ssh2 m)
    target_compile_options(sshul PRIVATE -Wall)
endif()
target_link_libraries(sshul mbedcrypto)
