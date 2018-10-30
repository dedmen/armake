cmake_minimum_required (VERSION 3.8)

if(USE_64BIT_BUILD)
    set(INTERCEPT_EXTENSION_NAME "intercept_x64")
else()
    set(INTERCEPT_EXTENSION_NAME "intercept")
endif()

file(GLOB_RECURSE INTERCEPT_SOURCES RELATIVE ..\\ *.h *.hpp *.c *.cpp)

set(INTERCEPT_NAME "armake")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}armake")

file(GLOB INTERCEPT_SOURCES *.h *.hpp *.c *.cpp)
file(GLOB INTERCEPT_SOURCES2 ${CMAKE_CURRENT_SOURCE_DIR}\\..\\lib\\*.c)

SOURCE_GROUP("main" FILES ${INTERCEPT_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}\\..\\lib\\minilzo.c ${CMAKE_CURRENT_SOURCE_DIR}\\..\\lib\\sha1.c)

add_executable(armake ${INTERCEPT_SOURCES})
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}\\..\\lib\\)

set_target_properties(armake PROPERTIES PREFIX "")
set_target_properties(armake PROPERTIES FOLDER armake)

if(CMAKE_COMPILER_IS_GNUCXX)
    set_target_properties(armake PROPERTIES LINK_SEARCH_START_STATIC 1)
    set_target_properties(armake PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()
