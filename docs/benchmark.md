# Benchmark

PulseCore includes a Linux-only loopback benchmark executable:

```bash
./build/release/pulsecore_epoll_benchmark --clients 4 --requests-per-client 1000 --payload-size 64 --workers 2
```

The benchmark starts an in-process epoll server, launches client threads, sends framed echo requests over TCP loopback, validates every response, and reports end-to-end throughput.

## Options

- `--clients N`: number of client threads
- `--requests-per-client N`: synchronous request/response exchanges per client
- `--payload-size N`: echo payload bytes per request
- `--workers N`: worker threads used by the epoll server

## Baseline Run

Date: 2026-09-06

Environment: Ubuntu 24.04 Docker container on local arm64 host.

Build:

```bash
cmake --preset release
cmake --build --preset release
```

Command:

```bash
./build/release/pulsecore_epoll_benchmark --clients 4 --requests-per-client 1000 --payload-size 64 --workers 2
```

Result:

```text
clients=4
workers=2
requests_per_client=1000
payload_bytes=64
total_requests=4000
elapsed_seconds=0.05
requests_per_second=87491.38
round_trip_frame_mib_per_second=14.02
```

This is a local smoke baseline, not a production capacity claim.
