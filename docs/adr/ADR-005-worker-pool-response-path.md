# ADR-005: Worker Pool Response Path

## Status

Accepted.

## Context

The epoll reactor must not block while processing application work. It also must not confuse a reusable fd with a logical client connection, and it must preserve response order for requests sent on the same connection.

## Decision

PulseCore introduces a bounded `BoundedQueue<T>` and a `WorkerPool`.

The reactor submits decoded requests with `TrySubmit`, which delegates to the bounded queue and returns immediately if the queue is full. Worker items include the opaque `ConnectionId` and a per-connection sequence number.

Worker threads run the shared request handler and publish `WorkResult` values through a completion callback. On Linux, the epoll server stores completed results in a protected queue and wakes the reactor with `eventfd`.

The reactor owns final response ordering. It stores completed responses by sequence number and only queues contiguous responses into the connection output buffer.

## Consequences

Slow application work no longer runs on the reactor thread.

Queue saturation is explicit instead of silently growing memory. The first policy is conservative: when the worker queue is full, the affected connection is closed.

Stale worker results are harmless because they are tagged with `ConnectionId` and dropped if the connection no longer exists.

The design adds a mutex-protected cross-thread completion queue. That is simpler and easier to verify than a lock-free structure at this stage.
