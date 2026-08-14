cmake_minimum_required(VERSION 3.20)
project(golden_state_replay_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(gsr_demo
  src/demo_domain.cpp
  src/demo_exporter.cpp
  src/replay_loader.cpp
)
target_include_directories(gsr_demo PUBLIC include)
if(MSVC)
  target_compile_options(gsr_demo PRIVATE /W4)
else()
  target_compile_options(gsr_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gsr_capture_demo tools/gsr_capture_demo.cpp)
target_link_libraries(gsr_capture_demo PRIVATE gsr_demo)

add_executable(gsr_smoke tools/gsr_smoke.cpp)
target_link_libraries(gsr_smoke PRIVATE gsr_demo)

include(CTest)
if(BUILD_TESTING)
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(gsr_gtest tests/golden_state_replay_test.cpp)
  target_link_libraries(gsr_gtest PRIVATE gsr_demo GTest::gtest_main)
  target_compile_definitions(
    gsr_gtest PRIVATE GSR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  include(GoogleTest)
  gtest_discover_tests(gsr_gtest)

  add_test(
    NAME gsr_smoke
    COMMAND gsr_smoke ${CMAKE_CURRENT_SOURCE_DIR}/examples/connected_call.gsr
  )
endif()
