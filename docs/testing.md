# Testing

PulseCore keeps correctness evidence close to the code. The project uses GoogleTest for unit and
loopback integration tests, CTest presets for repeatable local runs, sanitizer presets for memory and
thread checks, and GitHub Actions for Linux CI.

## Local Commands

Full local check:

```bash
./scripts/check_local.sh
```

Linux Docker check:

```bash
./scripts/check_linux_docker.sh
```

Development build:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Release build:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan --output-on-failure
```

ThreadSanitizer:

```bash
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan --output-on-failure
```

Profiling build:

```bash
cmake --preset profile
cmake --build --preset profile
ctest --preset profile --output-on-failure
```

Linux-only full test run:

```bash
cmake -S . -B build/linux-release -DCMAKE_BUILD_TYPE=Release -DPULSECORE_BUILD_TESTS=ON
cmake --build build/linux-release
ctest --test-dir build/linux-release --output-on-failure
```

Benchmark smoke:

```bash
./build/linux-release/pulsecore_epoll_benchmark --clients 2 --requests-per-client 10 --payload-size 16 --workers 2 --message-type echo
./build/linux-release/pulsecore_epoll_benchmark --clients 2 --requests-per-client 10 --payload-size 16 --workers 2 --message-type work --work-iterations 100
```

## Current Evidence

Last local verification date: 2026-09-06.

| Environment | Command | Result |
| --- | --- | --- |
| macOS dev | `ctest --preset dev --output-on-failure` | 66/66 passed |
| macOS release | `ctest --preset release --output-on-failure` | 66/66 passed |
| macOS ASan/UBSan | `ctest --preset asan-ubsan --output-on-failure` | 66/66 passed |
| macOS TSan | `ctest --preset tsan --output-on-failure` | 66/66 passed |
| macOS profile | `ctest --preset profile --output-on-failure` | 66/66 passed |
| Ubuntu 24.04 Docker release | `./scripts/check_linux_docker.sh` | 86/86 passed; echo/work benchmark smoke passed |

The macOS runs exclude Linux-only epoll tests because `epoll`, `eventfd`, and `signalfd` are Linux
APIs. The Linux Docker run includes the epoll server tests.

## CI Matrix

GitHub Actions runs:

- GCC development build and tests
- Clang development build and tests
- GCC release build and tests
- GCC release echo and work benchmark smoke
- Clang ASan/UBSan build and tests
- Clang TSan build and tests

## Coverage Areas

Unit tests cover:

- `UniqueFd` move-only fd ownership
- protocol encoding and decoding, including golden big-endian header bytes
- fragmented and coalesced frames
- malformed frame rejection
- non-blocking read/write behavior
- partial write preservation after `EAGAIN`
- read/write byte budgets
- output buffer high-water rejection
- bounded queue close and saturation behavior
- worker pool completion and shutdown behavior
- deterministic work request encoding, digest generation, and invalid payload rejection
- opaque connection registry behavior

Integration tests cover:

- blocking TCP echo request/response
- blocking TCP work request/response
- blocking server and client CLI help and invalid option validation
- malformed input over TCP
- client disconnect handling
- multiple frames over one connection
- multiple clients over the epoll reactor
- work request/response over the epoll reactor
- epoll server CLI help and invalid option validation
- stale worker result discard after connection removal
- max connection limit enforcement
- input buffer limit enforcement over the epoll reactor
- output buffer limit enforcement over the epoll reactor
- small per-event I/O budgets
- per-connection response ordering when workers complete out of order
- per-connection in-flight saturation
- half-close response flushing
- configured signal shutdown through `signalfd`
- malformed epoll client isolation while the server keeps running
