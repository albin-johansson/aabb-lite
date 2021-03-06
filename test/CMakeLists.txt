cmake_minimum_required(VERSION 3.15)
project(abby-test)

set(TEST_SOURCES
        unittest/test_main.cpp
        unittest/tree_test.cpp
        unittest/vec2_test.cpp
        unittest/aabb_test.cpp)

add_executable(${ABBY_TEST_TARGET} ${TEST_SOURCES})

set_target_properties(${ABBY_TEST_TARGET} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)

add_library(libDoctest INTERFACE)
target_include_directories(libDoctest INTERFACE doctest)
include(doctest)

target_include_directories(${ABBY_TEST_TARGET}
        PUBLIC unittest
        PUBLIC lib
        PUBLIC ${INCLUDE_DIR})

add_library(libAABBCC
        lib/AABB.h
        lib/AABB.cc)

target_link_libraries(${ABBY_TEST_TARGET}
        PUBLIC libDoctest
        PUBLIC libAABBCC)
