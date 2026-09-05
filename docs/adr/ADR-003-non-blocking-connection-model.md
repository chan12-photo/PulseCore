# ADR-003: Non-blocking Connection Model

## Status

Accepted.

## Context

The blocking TCP reference proves the protocol over a simple socket path, but the final Core architecture needs non-blocking sockets before epoll can be introduced.

Non-blocking I/O changes the shape of the code. `recv` and `send` can make partial progress, return `EAGAIN` or `EWOULDBLOCK`, or observe peer lifecycle events. The connection must therefore keep application-level input and output state between readiness events.

## Decision

PulseCore introduces a `Connection` object before the epoll reactor.

The connection:

- owns its fd through `UniqueFd`
- has an opaque `ConnectionId` that is separate from the raw fd
- reads currently available bytes without blocking
- keeps partial input in `FrameDecoder`
- returns all complete messages decoded from the current input buffer
- stores pending output bytes and a write offset
- reports `WouldBlock`, `PeerClosed`, and `ProtocolError` distinctly
- enforces an output buffer limit before queuing another response

The reactor will later own all `Connection` objects and use these statuses to decide whether to keep reading, enable write interest, close a connection, or apply backpressure.

## Consequences

This isolates the hardest partial-read and partial-write state from epoll. The epoll layer can remain responsible for readiness and ownership, while `Connection` remains responsible for per-socket byte-stream state.

The model also prevents a future worker from treating a raw fd as a stable connection identity.
