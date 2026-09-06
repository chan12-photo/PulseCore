# PulseCore Case Study

PulseCore is a C++20/Linux event-processing engine built to demonstrate systems programming
judgment: explicit protocol design, non-blocking TCP I/O, epoll-based readiness handling, bounded
resource management, worker hand-off, graceful shutdown, repeatable testing, and evidence-based
performance work.

It is intentionally not a trading system or a financial product. The project focuses on the engine
layer that receives framed TCP messages, dispatches them through workers, and writes validated
responses while keeping ownership and overload behavior explicit.

## What It Demonstrates

- C++ RAII and move semantics for file descriptor and payload ownership
- Explicit binary framing instead of writing structs directly to the network
- TCP fragmentation, coalescing, partial read, and partial write handling
- Linux `epoll`, `eventfd`, and `signalfd` integration
- Opaque monotonic `ConnectionId` values to avoid fd reuse bugs
- Bounded queues, buffers, live connections, and per-connection in-flight requests
- Runtime CLI controls for epoll worker and resource limits
- Per-connection response ordering across asynchronous worker completion
- Sanitizer, Docker, benchmark, and profiling evidence

## Architecture

The server has one reactor thread that owns socket registration, connection state, final writes, and
connection removal. Worker threads never write directly to sockets. They receive value-type work
items and publish value-type responses back to the reactor through an `eventfd` wake-up path.

Each TCP frame has a 20-byte big-endian header with magic, version, message type, payload length, and
request id. The decoder validates the envelope, while the request handler interprets payload bytes.

Two request types are implemented:

- `ECHO`: returns the request payload unchanged for network and framing baselines
- `WORK`: runs bounded deterministic CPU work over a seed payload and returns an 8-byte digest

## Correctness Evidence

Current verification date: 2026-09-06.

- macOS local presets: dev, release, ASan/UBSan, TSan, and profile all pass 60/60 tests
- Ubuntu 24.04 Docker release run passes 77/77 tests, including Linux-only epoll and CLI smoke tests
- Docker smoke checks run both echo and deterministic work benchmark modes
- `git diff --check` is part of the local check script

The tests cover protocol golden bytes, fragmented and coalesced frames, malformed input, partial
writes, byte budgets, queue saturation, worker pool shutdown, stale response discard, response
ordering, in-flight request limits, max connection limits, input/output buffer limits, epoll server
CLI validation, signal shutdown, and TCP work request/response paths.

## Performance Evidence

The benchmark starts an in-process epoll server, launches client threads, drives TCP loopback traffic,
validates every response, and reports throughput plus p50/p95/p99/max round-trip latency.

The first measured optimization reduced redundant payload copies at existing ownership boundaries.
On the documented Ubuntu 24.04 Docker environment, median benchmark throughput improved by 31.49% for
the 16 KiB payload run and 15.41% for the 64 KiB payload run.

Profiling was done with Linux `perf` using software events because the Docker Desktop/LinuxKit
environment did not expose hardware counters. The limitation is documented instead of being hidden.

## Trade-Offs

PulseCore favors conservative overload behavior over partial degradation. When a bounded resource is
exhausted, the affected connection is closed instead of allowing unbounded memory growth or blocking
the reactor.

The worker queue is a mutex and condition-variable baseline, not a lock-free queue. That keeps the
initial concurrency model inspectable and measurable before introducing lower-level data structures.

The deterministic `WORK` handler is a synthetic workload. It exists to exercise the worker and
backpressure path repeatably, not to pretend the engine already implements a business domain.

## Reproduce

```bash
./scripts/check_local.sh
```

```bash
./scripts/check_linux_docker.sh
```

```bash
cmake --preset release
cmake --build --preset release
./build/release/pulsecore_epoll_benchmark --clients 4 --requests-per-client 1000 --payload-size 64 --workers 2 --message-type echo
./build/release/pulsecore_epoll_benchmark --clients 4 --requests-per-client 1000 --payload-size 64 --workers 2 --message-type work --work-iterations 1000
```
