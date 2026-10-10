# syntax=docker/dockerfile:1
# Public, unauthenticated build of SuperGenius develop for Anthropic OSS Scanner.
FROM ghcr.io/geniusventures/almalinux-8:latest

ENV CI=1 \
    CC=/usr/bin/clang \
    CXX=/usr/bin/clang++ \
    CMAKE_GENERATOR=Ninja
ARG SCANNER_BUILD_JOBS=8

RUN dnf install -y curl ca-certificates tar gzip findutils \
 && dnf clean all

WORKDIR /src
COPY . /src

# Recursive submodule pinning is required: security-relevant code lives in
# GeniusKDF, ProofSystem, SGProcessingManager, evmrelay, etc.
RUN git config --global --add url."https://github.com/".insteadOf "git@github.com:" \
 && git submodule sync --recursive \
 && git submodule update --init --recursive \
 && git lfs pull

# These are public Linux x86_64 Release artifacts, byte-checked against the
# exact SHA-256 digests reported by GitHub's release API on 2026-10-09.
# Fetch them during the online build so the offline auditor gets usable libs.
RUN mkdir -p /src/thirdparty \
 && curl -fL --retry 3 --retry-delay 4 -o /tmp/thirdparty.tar.gz \
      "https://github.com/GeniusVentures/thirdparty/releases/download/Linux-x86_64-develop-Release/Linux-x86_64-Release.tar.gz" \
 && echo "bff18072d04f7c90c0c1552d3ecd64c0e3dc2952f9ca24405d9842be64a4f5d4  /tmp/thirdparty.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/thirdparty.tar.gz -C /src/thirdparty \
 && rm -f /tmp/thirdparty.tar.gz

RUN mkdir -p /src/zkLLVM \
 && curl -fL --retry 3 --retry-delay 4 -o /tmp/zkLLVM.tar.gz \
      "https://github.com/GeniusVentures/zkLLVM/releases/download/Linux-x86_64-develop-Release/Linux-x86_64-Release.tar.gz" \
 && echo "359ca5bbf4ac606be44fe4168419703e2e130a7a182eaceed2572bcfe65f0c11  /tmp/zkLLVM.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/zkLLVM.tar.gz -C /src/zkLLVM \
      --exclude='*bin/assigner' --exclude='*bin/clang-zkllvm' \
      --exclude='*bin/llvm-link-zkllvm' --exclude='*bin/transpiler' \
 && rm -f /tmp/zkLLVM.tar.gz

# Existing Linux CI installs cbindgen and the WASM Rust target.
RUN cargo install --locked cbindgen --version 0.28.0 \
 && rustup target add wasm32-unknown-emscripten

RUN cmake -S /src/build/Linux -B /src/build/Linux/Release/x86_64 -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=/usr/bin/clang \
      -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
      -DTHIRDPARTY_BUILD_DIR=/src/thirdparty/build/Linux/Release/x86_64 \
      -DZKLLVM_BUILD_DIR=/src/zkLLVM/build/Linux/Release/x86_64 \
      -DSGNS_PRINT_LOGS=ON \
      -DBUILD_TESTS=ON \
 && cmake --build /src/build/Linux/Release/x86_64 --parallel "${SCANNER_BUILD_JOBS}" \
 && cmake --install /src/build/Linux/Release/x86_64 \
 && ctest --test-dir /src/build/Linux/Release/x86_64 -N | grep -q securecrdt_interface_test

CMD ["/bin/bash"]
