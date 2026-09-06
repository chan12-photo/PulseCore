# ADR-010: Deterministic Work Request

## Status

Accepted.

## Context

Echo requests are useful for validating framing, ordering, and network I/O, but they do not exercise a
meaningful worker-side CPU path. PulseCore needs a repeatable workload that can be driven by tests and
benchmarks without depending on external systems or nondeterministic sleeps.

## Decision

`WORK` requests now encode a 4-byte big-endian iteration count followed by seed bytes. The handler
rejects payloads shorter than 4 bytes and rejects iteration counts above 1,000,000.

Accepted work requests run a deterministic bounded hash-style loop over the iteration count and seed,
then return an 8-byte big-endian digest in a `WORK` response.

The benchmark can choose `--message-type echo` or `--message-type work`. Work mode uses the same epoll
server path but validates digest responses instead of echoed payloads.

## Consequences

The worker pool now has a real bounded CPU workload for tests, benchmark smoke checks, and portfolio
evidence.

The digest is intentionally not cryptographic. Its job is repeatability and enough computation to make
worker scheduling, queueing, and backpressure easier to observe.

Invalid work request payloads are semantic request errors, not framing errors, so the handler returns
an error response instead of asking the decoder to close the connection.
