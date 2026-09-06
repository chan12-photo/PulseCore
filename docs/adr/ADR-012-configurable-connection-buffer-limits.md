# ADR-012: Configurable Connection Buffer Limits

## Status

Accepted.

## Context

`Connection` already enforces input and output buffer limits, but the epoll server used the default
limits implicitly. That made the production policy less visible and made integration tests rely on
unit-level `Connection` coverage for buffer overflow behavior.

## Decision

`EpollEchoServerOptions` now accepts `ConnectionLimits`. The server validates that input and output
buffer limits are greater than zero, then passes those limits into `ConnectionRegistry` so every newly
accepted connection receives the configured bounds.

The epoll integration tests configure a small output limit and verify that a response frame that would
exceed the limit closes the affected connection.

## Consequences

The epoll server's memory policy is easier to test and explain.

Default behavior is unchanged because the default `ConnectionLimits` values still allow one maximum
payload frame plus its header.

Future load-shedding policy can adjust connection limits at the server configuration boundary instead
of changing `Connection` internals.
