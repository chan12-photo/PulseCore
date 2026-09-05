# Blocking TCP Reference

The blocking TCP implementation is the Week 2 reference path. It is intentionally simple and processes one accepted connection at a time.

Its job is to prove:

- listener lifecycle: `socket`, `bind`, `listen`, `accept`
- client lifecycle: `socket`, `connect`, `send`, `recv`
- protocol frames can be sent over TCP
- one connection may contain multiple frames
- request IDs are preserved in responses
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
response type=2 request_id=1 payload="hello"
```
