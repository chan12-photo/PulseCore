# ADR-009: Per-Connection In-Flight Limit

## Status

Accepted.

## Context

The worker queue is globally bounded, but a single hot connection can still submit many decoded
requests before earlier responses are ready. Because PulseCore preserves per-connection response
order, completed responses may also wait in a per-connection reorder buffer behind an earlier slow
request.

Without a per-connection limit, one client can consume a disproportionate share of global queue
capacity and reorder-buffer memory.

## Decision

PulseCore adds `max_in_flight_requests_per_connection` to `EpollEchoServerOptions`.

A request is considered in flight after the reactor accepts it for worker submission and before the
connection's ordered response path advances past that sequence number. Before submitting each decoded
request, the reactor checks:

```text
next_request_sequence - next_response_sequence < max_in_flight_requests_per_connection
```

If the limit is reached, the reactor closes the affected connection. The default limit is 1024
requests per connection.

## Consequences

One busy client can no longer grow its own outstanding request count or response reorder buffer
without bound.

The first overload policy remains intentionally conservative: close the overloaded connection rather
than trying to buffer unbounded work or synthesize partial overload responses.

The limit is per connection, not a global admission controller or latency fairness guarantee. Global
queue capacity still protects the worker pool across all connections.
