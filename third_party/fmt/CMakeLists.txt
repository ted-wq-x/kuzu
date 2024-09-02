if (POLICY CMP0063)
    cmake_policy(SET CMP0063 NEW)
endif ()

add_library(kuzu_fmt STATIC format.cc)

target_include_directories(kuzu_fmt  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
