cmake_minimum_required(VERSION 3.20)
project(vector_join_reproduction LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VJ_FETCH_HNSWLIB "Fetch the pinned hnswlib revision" OFF)
set(HNSWLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../upstream/hnswlib" CACHE PATH
    "Directory containing hnswlib/hnswlib.h")

if(VJ_FETCH_HNSWLIB)
  include(FetchContent)
  FetchContent_Declare(
    hnswlib
    GIT_REPOSITORY https://github.com/nmslib/hnswlib.git
    GIT_TAG ca426729609b3221563047ceb269cc1213e0d376)
  FetchContent_MakeAvailable(hnswlib)
  set(HNSWLIB_INCLUDE_DIR "${hnswlib_SOURCE_DIR}")
endif()

if(NOT EXISTS "${HNSWLIB_INCLUDE_DIR}/hnswlib/hnswlib.h")
  message(FATAL_ERROR
    "hnswlib not found. Clone it to codes/upstream/hnswlib or configure "
    "with -DVJ_FETCH_HNSWLIB=ON")
endif()

add_library(vector_join src/vector_join.cpp)
target_include_directories(vector_join
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${HNSWLIB_INCLUDE_DIR})
target_compile_options(vector_join PRIVATE -Wall -Wextra -Wpedantic)

add_executable(vector_join_bench src/main.cpp)
target_link_libraries(vector_join_bench PRIVATE vector_join)

include(CTest)
if(BUILD_TESTING)
  add_executable(vector_join_tests tests/test_vector_join.cpp)
  target_link_libraries(vector_join_tests PRIVATE vector_join)
  add_test(NAME vector_join_tests COMMAND vector_join_tests)
endif()
