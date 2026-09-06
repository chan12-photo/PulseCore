# Architecture

PulseCore is a small C++20/Linux event-processing engine. It accepts TCP clients, decodes explicit
binary frames, dispatches work through a bounded worker queue, and writes ordered responses through a
single epoll reactor.

## Components

```text
TCP clients
  |
  v
non-blocking sockets
  |
  v
EpollEchoServer reactor
  |
  +--> ConnectionRegistry
  |      owns Connection objects and hides raw fd reuse behind opaque ConnectionId values
  |
  +--> Connection
  |      handles partial reads, partial writes, frame decoding, output buffering, and byte budgets
  |
  +--> BoundedQueue<WorkItem>
  |      accepts reactor submissions with a non-blocking TryPush API
  |
  +--> WorkerPool
  |      runs request handlers off the reactor thread
  |
  +<-- eventfd
         wakes the reactor when worker results are ready
```

The blocking TCP server and client remain as a simple reference path. They share the same protocol
codec and request handler, but they do not use epoll or the worker pool.

The request handler supports two request families: echo requests, which return the payload unchanged,
and work requests, which run a deterministic bounded CPU workload and return an 8-byte digest. The
work path gives the worker pool and backpressure tests a repeatable non-trivial workload without
making external service calls or relying on timing-sensitive behavior.

## Request Lifecycle

1. The listener accepts a client with `accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)`.
2. The reactor registers the socket with epoll and stores an opaque `ConnectionId` in
   `epoll_event.data.u64`.
3. `Connection::ReadAvailable()` drains available bytes up to the configured read budget.
4. Complete frames become `protocol::Message` values.
5. The reactor submits each message to `WorkerPool` with a per-connection sequence number.
6. Worker threads run the request handler and publish `WorkResult` values.
7. Worker completion writes to `eventfd`, waking the reactor.
8. The reactor drops stale results whose `ConnectionId` no longer exists.
9. Live responses are stored by sequence number and flushed to the connection only when every earlier
   response is available.
10. `Connection::WriteAvailable()` writes pending output up to the configured write budget.

## Core Invariants

- Raw fd numbers are not treated as stable client identity.
- The reactor owns socket registration, connection removal, and final writes.
- Worker threads never write to sockets directly.
- Reactor submission to the worker queue is non-blocking.
- Live connections, input payloads, output buffers, worker queue capacity, and per-connection
  in-flight requests are bounded.
- Client-visible response order is preserved per connection.
- Stale worker results are discarded instead of being delivered to a reused fd.
- Shutdown wakes the reactor with `signalfd` for configured signals and `eventfd` for explicit stop.

## Backpressure Policy

The first overload policy is intentionally conservative:

- malformed frame: close the affected connection
- max connection limit reached: accept and close the new connection
- worker queue full: close the affected connection
- per-connection in-flight limit reached: close the affected connection
- output buffer high-water mark exceeded: close the affected connection

The policy favors clear resource bounds and simple failure behavior over partial degradation.

## Performance Path

The benchmark drives the same epoll server over TCP loopback with multiple synchronous clients. It
can exercise either echo requests or deterministic work requests, then reports throughput, MiB/sec,
and round-trip latency percentiles.

The first measured optimization reduced redundant application-level copies across existing ownership
boundaries. The benchmark recorded median improvements of 31.49% for a 16 KiB payload run and 15.41%
for a 64 KiB payload run in the documented Docker environment.
