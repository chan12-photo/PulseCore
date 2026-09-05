# Non-blocking Connection

The non-blocking connection state is the next step after the blocking TCP reference.

This layer does not use epoll yet. It only proves that one socket can be driven safely when operations may return `EAGAIN` or `EWOULDBLOCK`.

## Responsibilities

- own a connection fd through `UniqueFd`
- keep an opaque `ConnectionId` separate from the raw fd
- be stored by a reactor-owned `ConnectionRegistry`
- read available bytes without blocking
- preserve partial input in a frame decoder
- return all complete protocol messages currently buffered
- track pending output bytes and write offset
- avoid unbounded output growth through a configured limit
- preserve pending output after partial writes and `EAGAIN`
- limit read/write work with per-call byte budgets

## Fairness Budgets

`ReadAvailable` and `WriteAvailable` accept per-call byte budgets. The defaults are 64 KiB for reads and 64 KiB for writes.

The reactor passes these budgets on every readiness event so one busy connection cannot spend unbounded time inside a single event callback.

## Current Statuses

Read:

- `kOk`: progress was made, or at least one message was decoded
- `kWouldBlock`: no bytes were currently available
- `kPeerClosed`: peer closed or reset the connection
- `kProtocolError`: malformed frame detected

Write:

- `kOk`: output buffer is empty
- `kWouldBlock`: socket cannot accept more bytes now
- `kPeerClosed`: peer closed or reset the connection

The epoll reactor uses these statuses to decide whether to keep reading, pause a connection, enable `EPOLLOUT`, or close the connection.
