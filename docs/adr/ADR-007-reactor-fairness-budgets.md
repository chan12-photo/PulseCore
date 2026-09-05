# ADR-007: Reactor Fairness Budgets

## Status

Accepted.

## Context

The reactor must not let one hot connection monopolize the event loop. Before this step, `Connection::ReadAvailable()` and `Connection::WriteAvailable()` continued until `EAGAIN`, peer close, or error.

## Decision

PulseCore adds per-call byte budgets to non-blocking connection I/O.

`ReadAvailable(max_read_bytes)` drains decoded frames, then reads at most the configured byte count from the socket before returning to the reactor.

`WriteAvailable(max_write_bytes)` writes at most the configured byte count from the pending output buffer before returning.

The Linux epoll server exposes these as options and defaults both budgets to 64 KiB per readiness event.

## Consequences

One connection can still make steady progress, but it must yield back to the reactor after a bounded amount of I/O.

The first fairness policy is deliberately simple. It does not yet implement priority scheduling, latency classes, or per-connection request quotas.
