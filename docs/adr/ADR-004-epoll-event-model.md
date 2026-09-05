# ADR-004: epoll Event Model

## Status

Accepted.

## Context

PulseCore needs to move from the blocking TCP reference to a Linux readiness-based event loop. The project is Linux-centered, so the Core event API is `epoll`.

The non-blocking `Connection` object already handles partial reads, partial writes, `EAGAIN`, protocol errors, peer close, and output buffering.

## Decision

PulseCore uses a single level-triggered epoll reactor as the initial event model.

The reactor:

- owns the listener fd and epoll fd through `UniqueFd`
- accepts clients with `accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)`
- stores connections in a reactor-owned `ConnectionRegistry`
- puts opaque `ConnectionId` values in `epoll_event.data.u64`
- treats key 0 as the listener and connection IDs as non-zero
- enables `EPOLLOUT` only while a connection has pending output
- removes malformed or closed connections through the reactor path

Edge-triggered epoll, multi-reactor designs, CPU pinning, and worker wake-up optimizations are not part of this step.

## Consequences

The event loop can serve multiple clients without a thread per connection while preserving the existing per-connection read/write tests.

The current `Stop()` uses a short epoll timeout instead of an eventfd wake-up. That is acceptable for the first reactor step. A later worker response path will introduce an eventfd-style wake-up if needed.
