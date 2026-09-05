# Benchmark

PulseCore includes a Linux-only loopback benchmark executable:

```bash
./build/release/pulsecore_epoll_benchmark --clients 4 --requests-per-client 1000 --payload-size 64 --workers 2
```

The benchmark starts an in-process epoll server, launches client threads, sends framed echo requests over TCP loopback, validates every response, and reports end-to-end throughput plus round-trip latency percentiles.

## Options

- `--clients N`: number of client threads
- `--requests-per-client N`: synchronous request/response exchanges per client
- `--payload-size N`: echo payload bytes per request
- `--workers N`: worker threads used by the epoll server

Latency is measured in each client thread from immediately before `SendMessage()` to immediately
after the response has been decoded and validated. Percentiles use nearest-rank selection over all
client requests in the run.

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
requests_per_second=84000.89
round_trip_frame_mib_per_second=13.46
latency_p50_us=46.83
latency_p95_us=65.88
latency_p99_us=87.12
latency_max_us=129.79
```

This is a local smoke baseline, not a production capacity claim.

## Hot-Path Copy Reduction

Date: 2026-09-06

Environment: same Ubuntu 24.04 Docker container on local arm64 host.

Change measured:

- move decoded request messages into the worker queue instead of copying them
- let the default worker handler move echo/work payload storage into the response
- move a freshly encoded frame into an empty connection output buffer instead of copying it

The benchmark was run three times before and after the change, then compared by median value.

16 KiB payload command:

```bash
./pulsecore_epoll_benchmark --clients 4 --requests-per-client 500 --payload-size 16384 --workers 2
```

| Variant | Requests/sec runs | Median requests/sec | Median MiB/sec |
| --- | ---: | ---: | ---: |
| Before | 49015.75, 32735.43, 39442.55 | 39442.55 | 1234.08 |
| After | 51863.24, 53673.44, 49966.69 | 51863.24 | 1622.70 |

64 KiB payload command:

```bash
./pulsecore_epoll_benchmark --clients 4 --requests-per-client 1000 --payload-size 65536 --workers 2
```

| Variant | Requests/sec runs | Median requests/sec | Median MiB/sec |
| --- | ---: | ---: | ---: |
| Before | 29890.05, 26014.58, 33205.08 | 29890.05 | 3737.40 |
| After | 34495.21, 34698.55, 34467.86 | 34495.21 | 4313.22 |

Result: the measured median improved by 31.49% for the 16 KiB payload run and 15.41% for the
64 KiB payload run.
