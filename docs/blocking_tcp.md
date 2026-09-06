# Blocking TCP Reference

The blocking TCP implementation is the Week 2 reference path. It is intentionally simple and processes one accepted connection at a time.

Its job is to prove:

- listener lifecycle: `socket`, `bind`, `listen`, `accept`
- client lifecycle: `socket`, `connect`, `send`, `recv`
- protocol frames can be sent over TCP
- one connection may contain multiple frames
- request IDs are preserved in responses
- echo and deterministic work requests share the same blocking reference path
- malformed input closes only the offending connection
- peer disconnect returns cleanly from the blocking reference server

The blocking server is not the final architecture. It exists so later non-blocking and epoll implementations can be compared against a small working reference.

## Run

Start the server:

```bash
./build/dev/pulsecore_server 9000
```

Send one echo request:

```bash
./build/dev/pulsecore_client 9000 hello
```

Expected output:

```text
response type=echo_response request_id=1 payload_hex=68656c6c6f
```

Send one deterministic work request:

```bash
./build/dev/pulsecore_client 9000 seed --message-type work --work-iterations 1000
```

Expected output shape:

```text
response type=work_response request_id=1 payload_hex=<8-byte digest>
```
