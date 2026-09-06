#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${PULSECORE_DOCKER_IMAGE:-ubuntu:24.04}"

docker run --rm \
  -v "$repo_root":/src:ro \
  -w /src \
  "$image" \
  bash -lc '
    set -euo pipefail
    apt-get update >/tmp/pulsecore-apt-update.log
    apt-get install -y --no-install-recommends \
      ca-certificates \
      cmake \
      g++ \
      git \
      ninja-build \
      >/tmp/pulsecore-apt-install.log

    cmake -S /src -B /tmp/pulsecore-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DPULSECORE_BUILD_TESTS=ON
    cmake --build /tmp/pulsecore-release
    ctest --test-dir /tmp/pulsecore-release --output-on-failure
    /tmp/pulsecore-release/pulsecore_epoll_benchmark \
      --clients 2 \
      --requests-per-client 10 \
      --payload-size 16 \
      --workers 2
  '
