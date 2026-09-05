# ADR-006: Graceful Shutdown Signal Path

## Status

Accepted.

## Context

PulseCore needs a shutdown path that avoids unsafe work inside POSIX signal handlers. The reactor already waits on file descriptors with `epoll`, so Linux signal delivery can be integrated into that same readiness loop.

## Decision

The Linux epoll server supports configured shutdown signals through `signalfd`.

When shutdown signals are configured, the server:

- builds a signal set from the configured signal numbers
- blocks those signals with `pthread_sigmask`
- creates a non-blocking, close-on-exec `signalfd`
- registers that fd in epoll with a reserved event key
- drains `signalfd_siginfo` records on readiness
- sets the stop flag from the reactor thread
- closes live connections before `Run()` returns
- restores the previous signal mask when the server object is destroyed

The `pulsecore_epoll_server` app configures SIGINT and SIGTERM as shutdown signals.

## Consequences

Ctrl-C and SIGTERM can stop the epoll server without running application shutdown logic inside an async signal handler.

The current shutdown policy is simple: stop the reactor and close live client connections. It does not promise to drain every queued request or pending response before exit.
