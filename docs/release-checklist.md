# Release Checklist

Use this checklist before making the repository public or sending it as a portfolio link.

## Preflight

```bash
git status --short
git log --oneline -10
git diff --check
```

- The working tree should be clean.
- Do not commit local handoff notes, long planning transcripts, screenshots, or private machine
  setup notes.
- Keep public documentation focused on the implemented system, verification commands, benchmark
  methodology, and trade-offs.

## Correctness Checks

```bash
./scripts/check_local.sh
```

Expected current result:

- macOS dev: 66/66 tests passed
- macOS release: 66/66 tests passed
- macOS ASan/UBSan: 66/66 tests passed
- macOS TSan: 66/66 tests passed
- macOS profile: 66/66 tests passed

```bash
./scripts/check_linux_docker.sh
```

Expected current result:

- Ubuntu 24.04 Docker release: 86/86 tests passed
- Echo benchmark smoke passed
- Work benchmark smoke passed

## Manual Smoke

Blocking reference server:

```bash
./build/dev/pulsecore_server 9000
./build/dev/pulsecore_client 9000 hello
./build/dev/pulsecore_client 9000 seed --message-type work --work-iterations 1000
```

Linux epoll server:

```bash
./build/release/pulsecore_epoll_server 9000 \
  --workers 4 \
  --queue-capacity 2048 \
  --max-connections 1024 \
  --max-in-flight 128
```

Loopback benchmark:

```bash
./build/release/pulsecore_epoll_benchmark \
  --clients 4 \
  --requests-per-client 1000 \
  --payload-size 64 \
  --workers 2 \
  --message-type echo

./build/release/pulsecore_epoll_benchmark \
  --clients 4 \
  --requests-per-client 1000 \
  --payload-size 64 \
  --workers 2 \
  --message-type work \
  --work-iterations 1000
```

## Evidence To Record

- Commit SHA used for the final run
- Date of verification
- macOS version and compiler version for local checks
- Docker image used for Linux checks
- Test counts from `docs/testing.md`
- Benchmark command lines and raw output
- Any profiling limitation, especially when using Docker Desktop or virtualized Linux

## Publish

```bash
git remote -v
git push -u origin main
```

After pushing:

- Confirm GitHub Actions is green.
- Re-open the README from GitHub and make sure command blocks render correctly.
- Keep future optimization claims tied to the exact benchmark command and environment that produced
  the numbers.
