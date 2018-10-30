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

add_executable(armake ${INTERCEPT_SOURCES})
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}\\lib\\)

set_target_properties(armake PROPERTIES PREFIX "")
set_target_properties(armake PROPERTIES FOLDER armake)

if(CMAKE_COMPILER_IS_GNUCXX)
    set_target_properties(armake PROPERTIES LINK_SEARCH_START_STATIC 1)
    set_target_properties(armake PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()
