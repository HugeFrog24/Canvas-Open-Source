# Rebuilds canvas-sanitizer-harness:local (recovered from `docker history`):
# clang-18 with compiler-rt sanitizer runtimes, and libc++ 18 so std::string /
# std::vector layouts match the NDK's.
#   docker build -t canvas-sanitizer-harness:local app/src/test/sanitizers
FROM ubuntu:24.04
WORKDIR /build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        clang lld llvm libclang-rt-18-dev cmake ninja-build g++ python3 ca-certificates zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
USER root
RUN apt-get update && apt-get install -y --no-install-recommends libc++-18-dev libc++abi-18-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /harness
