# PulseCore Binary Protocol

PulseCore uses an explicit binary frame format. C++ structs are never written directly to the network.

All integer fields are encoded in big-endian byte order.

## Header

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 4 | magic | `0x50554C53`, ASCII `PULS` |
| 4 | 2 | version | unsigned 16-bit integer |
| 6 | 2 | message type | unsigned 16-bit integer |
| 8 | 4 | payload length | unsigned 32-bit integer |
| 12 | 8 | request id | unsigned 64-bit integer |

The header is exactly 20 bytes.

## Version

The initial protocol version is `1`.

## Message Types

| Value | Name |
|---:|---|
| 1 | Echo request |
| 2 | Echo response |
| 3 | Work request |
| 4 | Work response |
| 5 | Error response |

## Payload

The initial maximum payload size is 64 KiB.

Payload bytes are interpreted by the message handler. The frame decoder only validates the envelope and restores complete frames from arbitrary TCP byte chunks.

## Request IDs

Request IDs are scoped to a single connection. Multiple requests may be in flight on one connection, so responses are matched by request ID rather than by completion order.

## Malformed Input

The decoder reports malformed frames to the connection layer. The Core policy is to close the offending connection instead of attempting byte-stream resynchronization.

Malformed conditions include:

- invalid magic
- unsupported version
- unknown message type
- payload length above the configured maximum
- frame size overflow
