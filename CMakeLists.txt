cmake_minimum_required(VERSION 3.20)
project(apex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

add_library(apex_core src/matcher.cpp src/synth.cpp)
target_include_directories(apex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(apex_core PUBLIC Threads::Threads)
target_compile_options(apex_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(apex src/main.cpp)
target_link_libraries(apex PRIVATE apex_core)
target_compile_options(apex PRIVATE -Wall -Wextra -Wpedantic)

add_executable(apex_tests tests/test_matcher.cpp)
target_link_libraries(apex_tests PRIVATE apex_core)
target_compile_options(apex_tests PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME apex_tests COMMAND apex_tests)
