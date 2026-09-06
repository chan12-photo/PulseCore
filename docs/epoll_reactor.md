# epoll Reactor

The first epoll implementation is a single-reactor echo server for Linux.

It is intentionally still simple:

- one reactor thread
- level-triggered epoll
- non-blocking listener
- non-blocking accepted sockets
- one reactor-owned `ConnectionRegistry`
- raw fd numbers are not used as stable connection identity
- decoded requests are submitted to a bounded worker queue
- worker completions wake the reactor with `eventfd`
- configured shutdown signals wake the reactor with `signalfd`
- per-connection response ordering is preserved with sequence numbers
- live connections, per-connection buffers, and per-connection in-flight requests are bounded
- per-event read/write byte budgets limit work done for one connection at a time
- `EPOLLOUT` is enabled only while a connection has pending output

## Identity

Client events store the opaque `ConnectionId` in `epoll_event.data.u64`. `ConnectionId` starts at 1, while the listener uses key 0.

This avoids treating a reusable fd number as the identity of a logical client connection.

## Backpressure

The worker queue is globally bounded, and each connection also has a configurable in-flight request
limit. A request is considered in flight after it is accepted by the reactor and before its ordered
response is queued or the connection is closed.

The first overload policy is conservative: queue saturation or per-connection in-flight saturation
closes the affected connection.

The same `pulsecore_client` binary can send echo requests or deterministic work requests to the epoll
server. Work requests exercise the worker path with bounded CPU work and digest validation.

## Runtime Configuration

The `pulsecore_epoll_server` app exposes the main reactor limits as CLI options:

```bash
./build/release/pulsecore_epoll_server 9000 \
  --workers 4 \
  --queue-capacity 2048 \
  --max-connections 1024 \
  --max-in-flight 128 \
  --max-read-bytes 65536 \
  --max-write-bytes 65536 \
  --max-input-buffer 65556 \
  --max-output-buffer 65556
```

These flags map directly to `EpollEchoServerOptions`, so manual demos and benchmark experiments can
use the same resource policy knobs as the integration tests.

## Current Limitations

This is not the final C2 architecture yet.

- shutdown closes live connections instead of draining every pending response
- fairness is byte-budget based, not priority or latency scheduled

Those policies can be tightened after the reactor/worker path is green.
