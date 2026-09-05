# Profiling

PulseCore was profiled with Linux `perf` against the epoll loopback benchmark.

## Environment

Date: 2026-09-06

Environment: privileged Ubuntu 24.04 Docker container on Docker Desktop/LinuxKit, local arm64 host.

Limitation: LinuxKit exposed software events such as `task-clock`, but hardware counters such as
`cycles`, `instructions`, branches, and cache events were reported as not supported. Run the same
commands on a native Linux host for hardware-counter evidence.

## Build

```bash
cmake -S . -B build/profile -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPULSECORE_BUILD_TESTS=ON
cmake --build build/profile --target pulsecore_epoll_benchmark
```

## perf stat

Command:

```bash
perf stat -d ./build/profile/pulsecore_epoll_benchmark --clients 4 --requests-per-client 5000 --payload-size 65536 --workers 2
```

Result:

```text
clients=4
workers=2
requests_per_client=5000
payload_bytes=65536
total_requests=20000
elapsed_seconds=0.59
requests_per_second=33875.24
round_trip_frame_mib_per_second=4235.70

1047.29 msec task-clock
47584 context-switches
78 cpu-migrations
12518 page-faults
0.591745167 seconds time elapsed
0.349428000 seconds user
0.758640000 seconds sys
```

Unsupported hardware counters:

```text
cycles
instructions
branches
branch-misses
L1-dcache-loads
L1-dcache-load-misses
LLC-loads
LLC-load-misses
```

## perf record

Command:

```bash
perf record -e task-clock -F 199 -g -o perf.data -- ./build/profile/pulsecore_epoll_benchmark --clients 4 --requests-per-client 5000 --payload-size 65536 --workers 2
perf report --stdio -i perf.data --sort=dso,symbol --percent-limit=2 --no-children --call-graph=none
```

Program result:

```text
clients=4
workers=2
requests_per_client=5000
payload_bytes=65536
total_requests=20000
elapsed_seconds=0.56
requests_per_second=35594.32
round_trip_frame_mib_per_second=4450.65
```

Sampling result:

```text
Samples: 193 of event 'task-clock'
Total Lost Samples: 0

Overhead  Shared Object      Symbol
16.58%    libc.so.6          0x00000000000a1a50
8.81%     [kernel.kallsyms]  __arch_copy_from_user
8.29%     [kernel.kallsyms]  __arch_copy_to_user
7.77%     [kernel.kallsyms]  __wake_up_sync_key
7.77%     [kernel.kallsyms]  el0_svc
6.22%     libc.so.6          recv
4.15%     [kernel.kallsyms]  try_to_wake_up
3.11%     [kernel.kallsyms]  arch_counter_get_cntvct
3.11%     libc.so.6          memcmp
```

## Interpretation

The post-optimization profile is dominated by TCP loopback syscall work, kernel/user copies, and
wakeups. The remaining visible application-level work in sampled call stacks was around
`Connection::ReadAvailable`, `Connection::WriteAvailable`, `EpollEchoServer::SubmitWork`, and
`EpollEchoServer::UpdateInterest`.

This supports keeping the first optimization focused on redundant application-level copies without
claiming that the benchmark has reached a kernel, NIC, or production deployment limit.
