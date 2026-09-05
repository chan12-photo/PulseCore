# ADR-002: Binary Protocol Framing

## Status

Accepted.

## Context

TCP is a byte stream and does not preserve application message boundaries. A single `send` can be split across multiple `recv` calls, and multiple sends can be coalesced into one receive buffer.

PulseCore therefore needs explicit framing before non-blocking I/O or epoll are introduced.

## Decision

PulseCore frames start with a fixed 20-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic |
| 4 | 2 | version |
| 6 | 2 | message type |
| 8 | 4 | payload length |
| 12 | 8 | request id |

All integer fields are encoded in big-endian byte order. C++ structs are not sent directly over the network.

The initial magic is `0x50554C53`, the initial version is `1`, and the initial maximum payload size is 64 KiB.

The decoder reports malformed frames to the connection layer. The Core policy is to close the offending connection instead of attempting byte-stream resynchronization.

Multiple requests may be in flight on a single connection in later phases, so responses are matched by request ID rather than by assumed completion order.

## Consequences

This makes fragmented and coalesced TCP input testable before the event loop becomes more complex.

The protocol is intentionally small. It is enough to prove framing, validation, request ID preservation, and explicit serialization without pretending to be FIX or a production financial protocol.
