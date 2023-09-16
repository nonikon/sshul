cmake_minimum_required(VERSION 3.13)

project(sshul LANGUAGES C)

set(LIBSSH2_INCPATH "" CACHE PATH "libssh2 include path")
set(LIBSSH2_LIBPATH "" CACHE PATH "libssh2 library path")
set(LIBMBED_LIBPATH "" CACHE PATH "mbedtls library path")
set(LIBZLIB_LIBPATH "" CACHE PATH    "zlib library path")

# Get Git current commit id
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
endif()

set(sshul_sources
    main.c
    match.c
    config.c
    ssh_session.c
    json.c
    xlist.c
    xstring.c
)
if(WIN32)
    list(APPEND sshul_sources sshul.manifest)
endif()

add_executable(sshul ${sshul_sources})
target_include_directories(sshul PRIVATE ${LIBSSH2_INCPATH})
if(NOT ${GIT_COMMIT_ID})
    target_compile_definitions(sshul PRIVATE GIT_COMMIT_ID="${GIT_COMMIT_ID}")
endif()
target_link_directories(sshul PRIVATE ${LIBSSH2_LIBPATH} ${LIBMBED_LIBPATH} ${LIBZLIB_LIBPATH})
if(WIN32)
    target_link_libraries(sshul ws2_32)
endif()
if(MSVC)
    target_link_libraries(sshul libssh2 zlibstatic)
    target_compile_definitions(sshul PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(sshul ssh2 z m)
    target_compile_options(sshul PRIVATE -Wall)
endif()
target_link_libraries(sshul mbedcrypto)
