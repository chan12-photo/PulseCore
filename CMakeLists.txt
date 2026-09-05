cmake_minimum_required(VERSION 3.20)

project(
  PulseCore
  VERSION 0.1.0
  DESCRIPTION "C++20 Linux event-processing engine portfolio project"
  LANGUAGES CXX
)

option(PULSECORE_BUILD_TESTS "Build PulseCore tests" ON)
option(PULSECORE_ENABLE_WARNINGS "Enable project compiler warnings" ON)

find_package(Threads REQUIRED)

add_library(pulsecore_core
  src/network/connection.cpp
  src/network/blocking_tcp.cpp
  src/protocol/codec.cpp
  src/common/unique_fd.cpp
)

target_compile_features(pulsecore_core PUBLIC cxx_std_20)

target_include_directories(pulsecore_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

if(PULSECORE_ENABLE_WARNINGS)
  target_compile_options(pulsecore_core
    PRIVATE
      $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
      $<$<CXX_COMPILER_ID:MSVC>:/W4>
  )
endif()

add_executable(pulsecore_server
  apps/server/main.cpp
)

target_link_libraries(pulsecore_server
  PRIVATE
    pulsecore_core
    Threads::Threads
)

add_executable(pulsecore_client
  apps/client/main.cpp
)

target_link_libraries(pulsecore_client
  PRIVATE
    pulsecore_core
)

if(PULSECORE_ENABLE_WARNINGS)
  target_compile_options(pulsecore_server
    PRIVATE
      $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
      $<$<CXX_COMPILER_ID:MSVC>:/W4>
  )
  target_compile_options(pulsecore_client
    PRIVATE
      $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
      $<$<CXX_COMPILER_ID:MSVC>:/W4>
  )
endif()

if(PULSECORE_BUILD_TESTS)
  include(CTest)
  enable_testing()

  include(FetchContent)

  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
  )

  FetchContent_MakeAvailable(googletest)

  add_executable(pulsecore_unit_tests
    tests/unit/nonblocking_connection_test.cpp
    tests/unit/protocol_codec_test.cpp
    tests/unit/unique_fd_test.cpp
  )

  target_link_libraries(pulsecore_unit_tests
    PRIVATE
      pulsecore_core
      GTest::gtest_main
  )

  if(PULSECORE_ENABLE_WARNINGS)
    target_compile_options(pulsecore_unit_tests
      PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
  endif()

  include(GoogleTest)
  gtest_discover_tests(pulsecore_unit_tests)

  add_executable(pulsecore_integration_tests
    tests/integration/blocking_echo_test.cpp
  )

  target_link_libraries(pulsecore_integration_tests
    PRIVATE
      pulsecore_core
      GTest::gtest_main
      Threads::Threads
  )

  if(PULSECORE_ENABLE_WARNINGS)
    target_compile_options(pulsecore_integration_tests
      PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
  endif()

  gtest_discover_tests(pulsecore_integration_tests)
endif()
