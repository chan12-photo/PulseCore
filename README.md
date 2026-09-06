# PulseCore

PulseCore is a C++20/Linux event-processing engine portfolio project.

The Core goal is to build a TCP server engine with explicit binary framing, non-blocking I/O, an epoll reactor, bounded queues, worker threads, graceful shutdown, tests, sanitizers, benchmarks, perf profiling, and one evidence-based optimization.

This repository is currently at the Linux epoll reactor, deterministic work handler, benchmark, perf
profiling, and first measured optimization step.

## Current Scope

Implemented:

- CMake project skeleton
- GoogleTest unit test setup
- move-only `UniqueFd` RAII wrapper
- explicit 20-byte binary protocol header
- protocol encoder/decoder unit tests
- blocking TCP echo server/client reference
- loopback TCP integration tests
- malformed frame and client disconnect coverage
- non-blocking connection read/write state tests
- configurable connection input/output buffer limits
- opaque monotonic connection registry
- bounded work queue
- worker pool
- deterministic bounded `WORK` request handler
- Linux epoll server with `eventfd` worker response wake-up
- Linux `signalfd` shutdown path for SIGINT/SIGTERM in the epoll server app
- max connection limit for the epoll server
- per-connection response ordering for asynchronous worker results
- per-connection in-flight request limit
- per-event read/write byte budgets for reactor fairness
- Linux epoll loopback benchmark executable
- echo and deterministic work benchmark modes
- Linux perf profiling notes for the benchmark
- evidence-based hot-path copy reduction optimization
- Linux epoll integration tests
- shared request handler for blocking and future reactor paths
- initial project scope ADR
- binary protocol framing ADR
- non-blocking connection model ADR
- epoll event model ADR
- worker pool and response path ADR
- graceful shutdown signal path ADR
- reactor fairness budget ADR
- hot-path copy reduction ADR
- per-connection in-flight limit ADR
- deterministic work request ADR
- max connection limit ADR
- configurable connection buffer limit ADR

Future native-Linux follow-up:

- hardware-counter profiling on a non-virtualized Linux host

## Build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

## Full Local Check

```bash
./scripts/check_local.sh
```

Linux-only epoll tests and benchmark smoke can also be reproduced from macOS with Docker:

```bash
./scripts/check_linux_docker.sh
```

## Run Blocking Echo Reference

```bash
./build/dev/pulsecore_server 9000
```

```bash
./build/dev/pulsecore_client 9000 hello
```

## Run epoll Echo Server On Linux

```bash
./build/dev/pulsecore_epoll_server 9000
```

```bash
./build/dev/pulsecore_client 9000 hello
```

## Run epoll Loopback Benchmark On Linux

The benchmark reports throughput and round-trip latency percentiles.

```bash
cmake --preset release
cmake --build --preset release
./build/release/pulsecore_epoll_benchmark --clients 4 --requests-per-client 1000 --payload-size 64 --workers 2 --message-type echo
```

## Profiling

See `docs/profiling.md` for the Linux `perf stat` and `perf record` evidence gathered against the
epoll loopback benchmark.

```bash
cmake --preset profile
cmake --build --preset profile --target pulsecore_epoll_benchmark
```

## Documentation

- `docs/case-study.md`: portfolio narrative, evidence summary, and trade-offs
- `docs/architecture.md`: component map, request lifecycle, invariants, and backpressure policy
- `docs/protocol.md`: binary frame format and protocol rules
- `docs/testing.md`: local verification commands, CI matrix, and covered behaviors
- `docs/benchmark.md`: benchmark methodology, baseline, and before/after optimization data
- `docs/profiling.md`: Linux `perf stat` and `perf record` notes
- `docs/adr/`: short architecture decision records

## Release Build

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

## Sanitizers

```bash
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan
```

```bash
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

## Non-goals Before C3

- Trading simulator
- EdgeVision runtime
- Qt device controller
- lock-free queue
- memory pool
- `io_uring`
- DPDK
- Kafka/Redis/cloud deployment
- HFT claims
