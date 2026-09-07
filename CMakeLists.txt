cmake_minimum_required(VERSION 3.21)

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
  src/core/handler.cpp
  src/network/connection_registry.cpp
  src/network/connection.cpp
  src/network/worker_pool.cpp
  src/network/blocking_tcp.cpp
  src/protocol/codec.cpp
  src/common/unique_fd.cpp
)

target_compile_features(pulsecore_core PUBLIC cxx_std_20)

target_include_directories(pulsecore_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(pulsecore_core
  PUBLIC
    Threads::Threads
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(pulsecore_core
    PRIVATE
      src/network/epoll_echo_server.cpp
  )

  target_compile_definitions(pulsecore_core
    PUBLIC
      PULSECORE_HAS_EPOLL=1
  )
endif()

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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(pulsecore_epoll_server
    apps/epoll_server/main.cpp
  )

  target_link_libraries(pulsecore_epoll_server
    PRIVATE
      pulsecore_core
      Threads::Threads
  )

  if(PULSECORE_ENABLE_WARNINGS)
    target_compile_options(pulsecore_epoll_server
      PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
  endif()

  add_executable(pulsecore_epoll_benchmark
    apps/benchmark/epoll_loopback.cpp
  )

  target_link_libraries(pulsecore_epoll_benchmark
    PRIVATE
      pulsecore_core
      Threads::Threads
  )

  if(PULSECORE_ENABLE_WARNINGS)
    target_compile_options(pulsecore_epoll_benchmark
      PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
  endif()
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
    tests/unit/bounded_queue_test.cpp
    tests/unit/connection_registry_test.cpp
    tests/unit/handler_test.cpp
    tests/unit/nonblocking_connection_test.cpp
    tests/unit/protocol_codec_test.cpp
    tests/unit/unique_fd_test.cpp
    tests/unit/worker_pool_test.cpp
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

  add_test(NAME BlockingServerCliHelp COMMAND pulsecore_server --help)
  add_test(NAME BlockingServerCliRejectsBadPort COMMAND pulsecore_server not-a-port)
  add_test(NAME BlockingServerCliRejectsNegativePort COMMAND pulsecore_server -1)
  add_test(NAME ClientCliHelp COMMAND pulsecore_client --help)
  add_test(NAME ClientCliRejectsMissingPort COMMAND pulsecore_client)
  add_test(NAME ClientCliRejectsNegativePort COMMAND pulsecore_client -1)
  add_test(
    NAME ClientCliRejectsBadMessageType
    COMMAND pulsecore_client 9000 --message-type nope
  )
  add_test(
    NAME ClientCliRejectsBadWorkIterations
    COMMAND pulsecore_client 9000 --message-type work --work-iterations nope
  )
  add_test(
    NAME ClientCliRejectsNegativeWorkIterations
    COMMAND pulsecore_client 9000 --message-type work --work-iterations -1
  )
  set_tests_properties(
    BlockingServerCliRejectsBadPort
    BlockingServerCliRejectsNegativePort
    ClientCliRejectsMissingPort
    ClientCliRejectsNegativePort
    ClientCliRejectsBadMessageType
    ClientCliRejectsBadWorkIterations
    ClientCliRejectsNegativeWorkIterations
    PROPERTIES WILL_FAIL TRUE
  )

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(pulsecore_epoll_integration_tests
      tests/integration/epoll_echo_test.cpp
    )

    target_link_libraries(pulsecore_epoll_integration_tests
      PRIVATE
        pulsecore_core
        GTest::gtest_main
        Threads::Threads
    )

    if(PULSECORE_ENABLE_WARNINGS)
      target_compile_options(pulsecore_epoll_integration_tests
        PRIVATE
          $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
          $<$<CXX_COMPILER_ID:MSVC>:/W4>
      )
    endif()

    gtest_discover_tests(pulsecore_epoll_integration_tests PROPERTIES TIMEOUT 10)

    add_test(NAME EpollServerCliHelp COMMAND pulsecore_epoll_server --help)
    add_test(
      NAME EpollServerCliRejectsUnknownOption
      COMMAND pulsecore_epoll_server --unknown-option
    )
    add_test(
      NAME EpollServerCliRejectsBadPort
      COMMAND pulsecore_epoll_server 999999
    )
    add_test(
      NAME EpollServerCliRejectsZeroWorkers
      COMMAND pulsecore_epoll_server --workers 0
    )
    add_test(
      NAME EpollServerCliRejectsNegativeWorkers
      COMMAND pulsecore_epoll_server --workers -1
    )
    add_test(
      NAME EpollBenchmarkCliRejectsNegativeWorkers
      COMMAND pulsecore_epoll_benchmark --workers -1
    )
    set_tests_properties(
      EpollServerCliRejectsUnknownOption
      EpollServerCliRejectsBadPort
      EpollServerCliRejectsZeroWorkers
      EpollServerCliRejectsNegativeWorkers
      EpollBenchmarkCliRejectsNegativeWorkers
      PROPERTIES WILL_FAIL TRUE
    )
  endif()
endif()
