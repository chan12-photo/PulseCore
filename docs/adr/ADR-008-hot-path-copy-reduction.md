# ADR-008: Hot-Path Copy Reduction

## Status

Accepted.

## Context

The epoll benchmark showed that PulseCore can validate end-to-end framed echo traffic over TCP
loopback, but the asynchronous path still copied payload storage at avoidable hand-off points.

Before this change:

- decoded messages were copied from `Connection::ReadAvailable()` results into worker `WorkItem`s
- the default worker handler copied echo/work payloads into responses
- freshly encoded response frames were copied into an empty connection output buffer

These copies are most visible for large payloads, where the benchmark spends a meaningful amount of
time moving bytes through the request/worker/response path.

## Decision

PulseCore keeps the existing `HandleRequest(const Message&)` API for callers that do not own the
request.

The asynchronous worker path now uses `HandleOwnedRequest(Message)`, so owned requests can move
payload storage into responses. The epoll reactor also moves decoded messages into worker items.

`Connection::QueueOutput()` now moves a freshly encoded frame into the output buffer when there is no
pending output.

## Evidence

Measured in the same Ubuntu 24.04 Docker container on the local arm64 host.

| Payload | Before median requests/sec | After median requests/sec | Change |
| --- | ---: | ---: | ---: |
| 16 KiB | 39442.55 | 51863.24 | +31.49% |
| 64 KiB | 29890.05 | 34495.21 | +15.41% |

Raw commands and runs are recorded in `docs/benchmark.md`.

## Consequences

The optimization is limited to ownership boundaries that were already present, so it does not change
the wire protocol, response ordering, queue behavior, or blocking reference server behavior.

Custom worker handlers now receive request messages by value. Handlers that only inspect requests can
still accept `const Message&` through `std::function`, while handlers that transform payloads can take
ownership.

The measurement remains a local loopback benchmark. It is useful as portfolio evidence for an
optimization decision, not as a production capacity claim.
