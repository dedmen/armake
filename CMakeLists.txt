cmake_minimum_required (VERSION 3.8)
project (armake)

option(USE_64BIT_BUILD "USE_64BIT_BUILD" OFF)
option(USE_STATIC_LINKING "USE_STATIC_LINKING" ON)

if(USE_STATIC_LINKING)
    message("WARNING: Linking statically")
    set(INTERCEPT_LINK_TYPE "static")
else()
    message("WARNING: Linking dynamically")
    set(INTERCEPT_LINK_TYPE "dynamic")
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC" AND "${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
    set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /std:c++latest" )
    set( USE_64BIT_BUILD ON)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /std:c++latest" )
endif()

message("GENERATOR USED: '${CMAKE_GENERATOR}'")
message("COMPILER USED: '${CMAKE_CXX_COMPILER_ID}'")

set(CMAKE_CL_64 ${USE_64BIT_BUILD})

if(USE_64BIT_BUILD)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/win64/")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/win32/")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


SET(CMAKE_INCLUDE_CURRENT_DIR ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

cmake_minimum_required (VERSION 3.8)

if(USE_64BIT_BUILD)
    set(INTERCEPT_EXTENSION_NAME "intercept_x64")
else()
    set(INTERCEPT_EXTENSION_NAME "intercept")
endif()

file(GLOB_RECURSE INTERCEPT_SOURCES "${PROJECT_SOURCE_DIR}/src/*.h" "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.c" "${PROJECT_SOURCE_DIR}/lib/*.c")

set(INTERCEPT_NAME "armake")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}armake")

SOURCE_GROUP("main" FILES ${INTERCEPT_SOURCES})

#Wolfssl

add_definitions(-DNO_DES3 -DWOLFSSL_KEY_GEN -DNO_DSA
                -DNO_MD4 -DNO_PSK -DNO_PWDBASED -DNO_RABBIT -DNO_HC128 -DNO_ERROR_STRINGS -DNO_OLD_TLS -DWOLFSSL_USER_IO
                -DNO_SESSION_CACHE -DOPENSSL_EXTRA
                -DWOLFSSL_STATIC_RSA #We need atleast 1 cipher suite or it fails to compile, even if we don't need that stuff
                )

set(wolfssl_DIR ${CMAKE_SOURCE_DIR}/wolfssl)

# Add header directories to include paths
include_directories(${wolfssl_DIR})

SET(wolfssl_sources
            ${wolfssl_DIR}/wolfcrypt/src/aes.c
            ${wolfssl_DIR}/wolfcrypt/src/arc4.c
            ${wolfssl_DIR}/wolfcrypt/src/asm.c
            ${wolfssl_DIR}/wolfcrypt/src/asn.c
            ${wolfssl_DIR}/wolfcrypt/src/blake2b.c
            ${wolfssl_DIR}/wolfcrypt/src/camellia.c
            ${wolfssl_DIR}/wolfcrypt/src/chacha.c
            ${wolfssl_DIR}/wolfcrypt/src/chacha20_poly1305.c
            ${wolfssl_DIR}/wolfcrypt/src/cmac.c
            ${wolfssl_DIR}/wolfcrypt/src/coding.c
            ${wolfssl_DIR}/wolfcrypt/src/compress.c
            ${wolfssl_DIR}/wolfcrypt/src/cpuid.c
            ${wolfssl_DIR}/wolfcrypt/src/curve25519.c
            ${wolfssl_DIR}/wolfcrypt/src/des3.c
            ${wolfssl_DIR}/wolfcrypt/src/dh.c
            ${wolfssl_DIR}/wolfcrypt/src/dsa.c
            ${wolfssl_DIR}/wolfcrypt/src/ecc.c
            ${wolfssl_DIR}/wolfcrypt/src/ecc_fp.c
            ${wolfssl_DIR}/wolfcrypt/src/ed25519.c
            ${wolfssl_DIR}/wolfcrypt/src/error.c
            ${wolfssl_DIR}/wolfcrypt/src/fe_low_mem.c
            ${wolfssl_DIR}/wolfcrypt/src/fe_operations.c
            ${wolfssl_DIR}/wolfcrypt/src/ge_low_mem.c
            ${wolfssl_DIR}/wolfcrypt/src/ge_operations.c
            ${wolfssl_DIR}/wolfcrypt/src/hash.c
            ${wolfssl_DIR}/wolfcrypt/src/hc128.c
            ${wolfssl_DIR}/wolfcrypt/src/hmac.c
            ${wolfssl_DIR}/wolfcrypt/src/idea.c
            ${wolfssl_DIR}/wolfcrypt/src/integer.c
            ${wolfssl_DIR}/wolfcrypt/src/logging.c
            ${wolfssl_DIR}/wolfcrypt/src/md2.c
            ${wolfssl_DIR}/wolfcrypt/src/md4.c
            ${wolfssl_DIR}/wolfcrypt/src/md5.c
            ${wolfssl_DIR}/wolfcrypt/src/memory.c
            ${wolfssl_DIR}/wolfcrypt/src/pkcs12.c
            ${wolfssl_DIR}/wolfcrypt/src/pkcs7.c
            ${wolfssl_DIR}/wolfcrypt/src/poly1305.c
            ${wolfssl_DIR}/wolfcrypt/src/pwdbased.c
            ${wolfssl_DIR}/wolfcrypt/src/rabbit.c
            ${wolfssl_DIR}/wolfcrypt/src/random.c
            ${wolfssl_DIR}/wolfcrypt/src/ripemd.c
            ${wolfssl_DIR}/wolfcrypt/src/rsa.c
            ${wolfssl_DIR}/wolfcrypt/src/sha.c
            ${wolfssl_DIR}/wolfcrypt/src/sha256.c
            ${wolfssl_DIR}/wolfcrypt/src/sha3.c
            ${wolfssl_DIR}/wolfcrypt/src/sha512.c
            ${wolfssl_DIR}/wolfcrypt/src/signature.c
            ${wolfssl_DIR}/wolfcrypt/src/srp.c
            ${wolfssl_DIR}/wolfcrypt/src/tfm.c
            ${wolfssl_DIR}/wolfcrypt/src/wc_encrypt.c
            ${wolfssl_DIR}/wolfcrypt/src/wc_port.c
            ${wolfssl_DIR}/wolfcrypt/src/wolfevent.c
            ${wolfssl_DIR}/wolfcrypt/src/wolfmath.c
            ${wolfssl_DIR}/src/crl.c
            ${wolfssl_DIR}/src/internal.c
            ${wolfssl_DIR}/src/wolfio.c
            ${wolfssl_DIR}/src/keys.c
            ${wolfssl_DIR}/src/ocsp.c
            ${wolfssl_DIR}/src/sniffer.c
            ${wolfssl_DIR}/src/ssl.c
            ${wolfssl_DIR}/src/tls.c
            ${wolfssl_DIR}/src/tls13.c
           )

add_executable(armake ${INTERCEPT_SOURCES} ${wolfssl_sources})
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}\\lib\\)
target_link_libraries(armake ws2_32)
set_target_properties(armake PROPERTIES PREFIX "")
set_target_properties(armake PROPERTIES FOLDER armake)

if(CMAKE_COMPILER_IS_GNUCXX)
    set_target_properties(armake PROPERTIES LINK_SEARCH_START_STATIC 1)
    set_target_properties(armake PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()
