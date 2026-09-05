# Connection Registry

The connection registry owns live non-blocking connections for the future reactor.

Raw file descriptors are not used as stable connection identities. Linux and other POSIX systems may quickly reuse a closed fd number for a new connection. If a worker response later used only that fd number, a stale response could be delivered to the wrong client.

PulseCore therefore assigns each accepted connection an opaque monotonic `ConnectionId`.

## Rules

- `ConnectionId` starts at 1 and is not reused during the process lifetime.
- accepted fds are set to non-blocking before registration completes.
- removing a connection destroys its `Connection`, closing the fd through `UniqueFd`.
- stale responses must look up the `ConnectionId`; if it is no longer present, the response is discarded.

The registry is owned by the future reactor thread. Worker threads must not mutate it directly.
