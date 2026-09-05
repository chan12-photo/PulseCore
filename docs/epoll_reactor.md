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
- per-connection response ordering is preserved with sequence numbers
- `EPOLLOUT` is enabled only while a connection has pending output

## Identity

Client events store the opaque `ConnectionId` in `epoll_event.data.u64`. `ConnectionId` starts at 1, while the listener uses key 0.

This avoids treating a reusable fd number as the identity of a logical client connection.

## Current Limitations

This is not the final C2 architecture yet.

- no graceful shutdown signal path
- no per-connection fairness budget beyond the accept budget
- queue-full policy closes the affected connection

Those pieces are added after the reactor/worker path is green.
