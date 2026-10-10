cmake_minimum_required(VERSION 3.20)
project(vector_join_reproduction LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VJ_FETCH_HNSWLIB "Fetch the pinned hnswlib revision" OFF)
option(VJ_BUILD_PYTHON "Build the pybind11 Python module" ON)
option(VJ_FETCH_PYBIND11 "Fetch the pinned pybind11 release when unavailable" OFF)
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

add_library(vector_join
  src/vector_join.cpp
  src/dispatch.cpp
  src/algorithms/independent.cpp
  src/algorithms/simjoin_hard.cpp
  src/algorithms/fast_avj_soft.cpp
  src/algorithms/fast_avj_merged.cpp
  src/algorithms/fast_avj_adaptive.cpp)
target_include_directories(vector_join
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${HNSWLIB_INCLUDE_DIR})
target_compile_options(vector_join PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(vector_join PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(vector_join_bench src/main.cpp)
target_link_libraries(vector_join_bench PRIVATE vector_join)

if(VJ_BUILD_PYTHON)
  find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)
  find_package(pybind11 CONFIG QUIET)
  if(NOT pybind11_FOUND)
    execute_process(
      COMMAND ${Python_EXECUTABLE} -m pybind11 --cmakedir
      OUTPUT_VARIABLE pybind11_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE PYBIND11_CMAKEDIR_RESULT)
    if(PYBIND11_CMAKEDIR_RESULT EQUAL 0)
      find_package(pybind11 CONFIG REQUIRED)
    elseif(VJ_FETCH_PYBIND11)
      include(FetchContent)
      FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v3.0.1)
      FetchContent_MakeAvailable(pybind11)
    else()
      message(FATAL_ERROR
        "pybind11 is required for the Python module. Install requirements-build.txt "
        "or configure with -DVJ_FETCH_PYBIND11=ON or -DVJ_BUILD_PYTHON=OFF.")
    endif()
  endif()
  pybind11_add_module(vector_join_py src/python_bindings.cpp)
  target_link_libraries(vector_join_py PRIVATE vector_join)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(vector_join_tests tests/test_vector_join.cpp)
  target_link_libraries(vector_join_tests PRIVATE vector_join)
  add_test(NAME vector_join_tests COMMAND vector_join_tests)
  if(VJ_BUILD_PYTHON)
    add_test(
      NAME vector_join_python_bindings
      COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bindings.py
              $<TARGET_FILE_DIR:vector_join_py>)
    add_test(
      NAME vector_join_python_runners
      COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runners.py
              ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_FILE_DIR:vector_join_py>)
  endif()
endif()
