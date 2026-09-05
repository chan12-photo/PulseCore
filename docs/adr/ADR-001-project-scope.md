# ADR-001: Project Scope and Non-goals

## Status

Accepted.

## Context

PulseCore is a C++20/Linux systems programming portfolio project. Its goal is to demonstrate TCP framing, non-blocking I/O, an epoll-based reactor, bounded worker queues, graceful shutdown, testing, sanitizer use, benchmarking, profiling, and one evidence-based optimization.

The project should complement the Java BankCore project rather than duplicate it. BankCore demonstrates business correctness around transactions and persistence. PulseCore demonstrates resource ownership, socket lifecycle, concurrency, and performance engineering.

## Decision

The Core project stops at a Linux event-processing engine:

- TCP request/response transport
- explicit binary protocol serialization
- non-blocking connection state
- single-reactor epoll event loop
- bounded request and response paths
- worker pool
- graceful shutdown
- unit and integration tests
- ASan, UBSan, and TSan evidence
- reproducible benchmark and perf evidence

Trading, EdgeVision, Qt, Kafka, Redis, io_uring, DPDK, custom allocators, memory pools, lock-free queues, and multi-reactor designs are out of scope before the C3 gate.

## Consequences

This keeps the project small enough to finish while still proving important C++ and Linux systems skills. The project will favor correctness, clear ownership, and reproducible measurement over impressive technology names.

If time becomes tight, optional extensions are removed before correctness tests, sanitizer runs, benchmark evidence, or documentation.
