# ADR-011: Max Connection Limit

## Status

Accepted.

## Context

PulseCore already bounds payload sizes, connection buffers, worker queue capacity, and per-connection
in-flight requests. Without a live connection limit, a client storm can still consume file descriptors
and per-connection memory before any payload or queue limit applies.

## Decision

`EpollEchoServerOptions` includes `max_connections`, defaulting to 1024. The value must be greater
than zero.

When the listener accepts a socket and the live connection count is already at the limit, the server
immediately lets that newly accepted socket close without registering it with the connection registry
or epoll.

## Consequences

The epoll server has an explicit upper bound on live connection state.

The overload policy remains conservative and local: existing accepted connections continue, while the
new connection above the limit is closed.

The limit is counted against connections owned by the server process. Kernel listen backlog behavior
can still affect whether a client observes connect success before the server accepts and closes the
socket.
