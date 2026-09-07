# Worker Pool

PulseCore routes accepted protocol messages through a bounded worker queue before responses are written back by the reactor.

## Queue Semantics

`BoundedQueue<T>` is a blocking consumer queue with a non-blocking producer API:

- `TryPush` returns `false` immediately when the queue is full
- `TryPush` returns `false` after `Close`
- `Pop` blocks until an item is available or the queue is closed
- `Close` wakes blocked consumers
- queued items are drained before `Pop` reports shutdown

The reactor uses `TryPush` only, so it does not block on worker saturation.

## Worker Semantics

`WorkerPool` consumes `WorkItem` values and emits `WorkResult` values through a completion callback.
The work handler receives each decoded request by value, allowing the default request handler to
move payload storage into the response on the asynchronous path.
If a work handler throws, the worker converts that failure into an error response for the same request
id instead of letting the exception escape the worker thread.

Each item carries:

- opaque `ConnectionId`
- per-connection sequence number
- decoded protocol request

Each result carries the same connection identity and sequence number plus the protocol response.

The reactor enforces a per-connection in-flight request limit before submitting each item. This
prevents one hot connection from monopolizing the global worker queue and response reorder buffer.

## Reactor Response Path

The Linux epoll server uses an `eventfd` to wake the reactor when workers complete requests.

Worker threads append completed results to a mutex-protected queue and write to the `eventfd`. The reactor drains the `eventfd`, moves completed results into reactor ownership, drops stale results for closed connections, and queues responses for live connections.

Responses are held in a per-connection sequence map until every earlier response is available. This preserves client-visible response order even when worker threads finish requests out of order.

## Current Limitations

- queue-full and per-connection in-flight saturation policies close the affected connection
- no timeout/cancellation for long-running custom work handlers
- shutdown closes live connections before worker completion results are drained
