cmake_minimum_required(VERSION 3.20)
project(vector_join_reproduction LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(VJ_MEMORY_LIMIT_BYTES "2000000000" CACHE STRING "Linux process address-space cap; increase only on the remote experiment host")
if(NOT VJ_MEMORY_LIMIT_BYTES MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "VJ_MEMORY_LIMIT_BYTES must be a positive integer")
endif()

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

execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE VJ_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git status --porcelain WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE VJ_SOURCE_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE)
set(VJ_SOURCE_DIRTY false)
if(VJ_SOURCE_STATUS)
  set(VJ_SOURCE_DIRTY true)
endif()
set(VJ_HNSW_REVISION "unverified-local-headers")
if(EXISTS "${HNSWLIB_INCLUDE_DIR}/.git")
  execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${HNSWLIB_INCLUDE_DIR}
    OUTPUT_VARIABLE VJ_HNSW_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" VJ_BUILD_TYPE_UPPER)
set(VJ_BUILD_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${VJ_BUILD_TYPE_UPPER}} -std=c++20 -Wall -Wextra -Wpedantic")
configure_file(include/vector_join/build_info.hpp.in generated/vector_join/build_info.hpp @ONLY)

add_library(vector_join
  src/vector_join.cpp
  src/simjoin_paper.cpp
  src/algorithms/join_slide.cpp
  src/algorithms/kjoinslide.cpp
  src/dispatch.cpp
  src/algorithms/independent.cpp
  src/algorithms/hard_window_cache.cpp
  src/algorithms/fast_avj_soft.cpp
  src/algorithms/fast_avj_merged.cpp
  src/algorithms/fast_avj_adaptive.cpp)
target_include_directories(vector_join
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/generated
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${HNSWLIB_INCLUDE_DIR})
target_compile_options(vector_join PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(vector_join PUBLIC VJ_MEMORY_LIMIT_BYTES=${VJ_MEMORY_LIMIT_BYTES}ULL)
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
  add_executable(fast_search_tests tests/test_fast_search.cpp)
  target_include_directories(fast_search_tests PRIVATE include src ${HNSWLIB_INCLUDE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
  target_compile_definitions(fast_search_tests PRIVATE VJ_MEMORY_LIMIT_BYTES=${VJ_MEMORY_LIMIT_BYTES}ULL)
  target_compile_options(fast_search_tests PRIVATE -UNDEBUG)
  add_test(NAME fast_search_tests COMMAND fast_search_tests)
  add_executable(vector_join_tests tests/test_vector_join.cpp)
  target_link_libraries(vector_join_tests PRIVATE vector_join)
  target_compile_options(vector_join_tests PRIVATE -UNDEBUG)
  add_test(NAME vector_join_tests COMMAND vector_join_tests)
  add_executable(simjoin_paper_tests tests/test_simjoin_paper.cpp)
  target_link_libraries(simjoin_paper_tests PRIVATE vector_join)
  target_compile_options(simjoin_paper_tests PRIVATE -UNDEBUG)
  add_test(NAME simjoin_paper_tests COMMAND simjoin_paper_tests)
  if(VJ_BUILD_PYTHON)
    add_test(NAME vector_join_audit_regressions
      COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_regressions.py)
    add_test(
      NAME vector_join_python_bindings
      COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bindings.py
              $<TARGET_FILE_DIR:vector_join_py>)
    add_test(
      NAME vector_join_python_runners
      COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runners.py
              ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_FILE_DIR:vector_join_py>)
    set_tests_properties(vector_join_python_bindings vector_join_audit_regressions
      PROPERTIES ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:vector_join_py>")
  endif()
endif()
