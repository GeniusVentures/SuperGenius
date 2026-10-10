# =============================================================================
# Anthropic OSS Scanner enrollment build for SuperGenius (issue #10, slice 1)
# =============================================================================
# Mirrors the Linux x86_64 Release CI job (.github/workflows/cmake.yml) with
# every authenticated step replaced by its anonymous equivalent:
#   - CI container pull (ghcr + org token)  -> anonymous ghcr pull
#   - GitHub CLI release download (org credentials) -> plain anonymous curl of
#     the public release-asset endpoints
#   - actions/checkout submodules:recursive  -> gitlink-SHA-pinned fetches of
#     the public submodule repos
#
# Scanner contract: anonymous, unauthenticated builder; Debian 12 amd64 host;
# 45 min / 16 CPU / 64 GB / 120 GB budget; offline-capable test phase (this
# Dockerfile performs no network access after the build layer completes).
#
# NOTE: tar excludes below are written unquoted; bash leaves a glob unmatched
# by any file in the working directory literal, which is the same semantics
# the quoted CI form guarantees (see cmake.yml "Download zkLLVM release").
# =============================================================================

FROM ghcr.io/geniusventures/almalinux-8:latest

# Base ships clang, cmake, ninja, git, git-lfs, Vulkan headers, dbus and
# gnome-keyring (see CI "Configure Linux host" comment). The dnf line is the
# exact CI safety-net set; curl/git/ca-certificates/tar/gzip are appended as
# an idempotent safety net for the anonymous asset downloads below.
RUN dnf install -y vulkan-loader-devel vulkan-headers ninja-build which perl perl-IPC-Cmd curl git ca-certificates tar gzip \
 && dnf clean all

SHELL ["/bin/bash", "-euxo", "pipefail", "-c"]

# CI COMPILER_PIN: Alma 8's g++ 8.5 cannot compile the zkLLVM crypto3 constexpr
# code consumed through ProofSystem (see "Pin host clang" in cmake.yml).
ENV CC=clang \
    CXX=clang++ \
    CMAKE_GENERATOR=Ninja \
    THIRDPARTY_BUILD_DIR=/opt/thirdparty/build/Linux/Release/x86_64 \
    ZKLLVM_BUILD_DIR=/opt/zkLLVM/build/Linux/Release/x86_64

# -----------------------------------------------------------------------------
# Anonymous public release assets (verified anonymously reachable 2026-10-10).
# The release tag `Linux-x86_64-develop-Release` moves with develop; the asset
# name `Linux-x86_64-Release.tar.gz` is stable. Tarball layout (verified by
# direct inspection of the published assets) is build/Linux/Release/x86_64/
# under the extract root == CI's $BUILD_DIRECTORY formula
# build/${target}/${build-type}/${abi}.
# -----------------------------------------------------------------------------
RUN mkdir -p /opt/thirdparty \
 && curl -fsSL --retry 3 -o /tmp/thirdparty.tar.gz \
      "https://github.com/GeniusVentures/thirdparty/releases/download/Linux-x86_64-develop-Release/Linux-x86_64-Release.tar.gz" \
 && tar -xzf /tmp/thirdparty.tar.gz -C /opt/thirdparty \
 && rm -f /tmp/thirdparty.tar.gz

RUN mkdir -p /opt/zkLLVM \
 && curl -fsSL --retry 3 -o /tmp/zkllvm.tar.gz \
      "https://github.com/GeniusVentures/zkLLVM/releases/download/Linux-x86_64-develop-Release/Linux-x86_64-Release.tar.gz" \
 && tar --exclude=*bin/assigner --exclude=*bin/clang-zkllvm --exclude=*bin/llvm-link-zkllvm --exclude=*bin/transpiler \
      -xzf /tmp/zkllvm.tar.gz -C /opt/zkLLVM \
 && rm -f /tmp/zkllvm.tar.gz

# -----------------------------------------------------------------------------
# Source tree. The scanner clones the superproject NON-recursively and builds
# with the repo root as context, so COPY brings the working tree including
# .git (the gitlink SHAs used below live in the HEAD tree) while submodule
# paths arrive as empty directories that the next RUN materializes.
# -----------------------------------------------------------------------------
COPY . /src
WORKDIR /src

# -----------------------------------------------------------------------------
# Submodule materialization, anonymous and pinned. Branch-head clones are
# FORBIDDEN (they would build whatever moved to the branch tip); instead each
# submodule is fetched at the exact gitlink SHA recorded in the superproject
# tree, so branch-head drift cannot silently change what is built. `docs` is
# skipped: it is not part of the build graph (see add_subdirectory list in
# build/CommonBuildParameters.cmake).
#
# git-lfs is NOT fetched: .gitattributes LFS-covers only *.cap test fixtures
# and the focused test subset below does not deserialize capture files.
# -----------------------------------------------------------------------------
RUN declare -A SUBS=( \
      [gRPCForSuperGenius]=https://github.com/GeniusVentures/gRPCForSuperGenius.git \
      [GeniusKDF]=https://github.com/GeniusVentures/GeniusKDF.git \
      [ProofSystem]=https://github.com/GeniusVentures/ProofSystem.git \
      [SGProcessingManager]=https://github.com/GeniusVentures/SGProcessingManager.git \
      [evmrelay]=https://github.com/GeniusVentures/evmrelay.git \
    ) \
 && for name in "${!SUBS[@]}"; do \
      sha="$(git ls-tree HEAD -- "$name" | awk '{print $3}')"; \
      if [ -z "$sha" ]; then \
        echo "FATAL: gitlink SHA missing for submodule '$name' (is .git in the build context?)" >&2; \
        exit 1; \
      fi; \
      echo "==> $name @ $sha"; \
      rm -rf "$name"; \
      git init "$name"; \
      ( cd "$name" \
        && git remote add origin "${SUBS[$name]}" \
        && if ! git fetch --depth 1 origin "$sha"; then \
             echo "WARN: shallow SHA fetch refused for $name; falling back to default-branch fetch + pinned checkout"; \
             git fetch origin; \
           fi \
        && git checkout "$sha" ) \
        || { echo "FATAL: anonymous fetch/pinned checkout failed for $name @ $sha from ${SUBS[$name]} — refusing to build unpinned" >&2; exit 1; }; \
    done

# -----------------------------------------------------------------------------
# Configure — identical shape to the CI "Configure CMake" line for the Linux
# x86_64 Release matrix leg (COMPILER_PIN + BUILD_DIR args + SGNS_PRINT_LOGS).
# -----------------------------------------------------------------------------
RUN cmake -S /src/build/Linux -B /src/build/Linux/Release/x86_64 \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DTHIRDPARTY_BUILD_DIR=$THIRDPARTY_BUILD_DIR \
      -DZKLLVM_BUILD_DIR=$ZKLLVM_BUILD_DIR \
      -DSGNS_PRINT_LOGS=ON

# -----------------------------------------------------------------------------
# Focused build. The 45-minute scanner budget cannot fit a full --build . of
# every target, so this builds a representative unit-test set whose dependency
# closure still spans src/, ProofSystem, SGProcessingManager and evmrelay:
#   network_registry_test, network_membership_filter_test  (network layer,
#     registered by addtest() in test/src/networkregistry/CMakeLists.txt)
#   blake2_test  (crypto layer, first addtest() in test/src/crypto/ — the
#     plan-draft name "crypto_test" does not exist as a CTest name)
# Build more: replace the --target list with plain
#   cmake --build /src/build/Linux/Release/x86_64 -j "$(nproc)"
# -----------------------------------------------------------------------------
RUN cmake --build /src/build/Linux/Release/x86_64 \
      --target network_registry_test network_membership_filter_test blake2_test \
      -j "$(nproc)"

# -----------------------------------------------------------------------------
# Focused ctest, offline after build: binaries and fixtures are already in the
# image; this layer performs no network fetch. -LE requires_foundry mirrors
# the CI exclusion of Foundry-dependent e2e suites. No dbus-run-session /
# gnome-keyring wrapping or MNN GTEST_FILTER logic: those exist in CI only
# for the full suite and the aarch64 Vulkan issue; this focused x86_64 Release
# subset selects no processing_* suites.
# -----------------------------------------------------------------------------
RUN ctest --test-dir /src/build/Linux/Release/x86_64 \
      -C Release \
      --output-on-failure \
      -R '^(network_registry_test|network_membership_filter_test|blake2_test)$' \
      -LE requires_foundry

# =============================================================================
# Anonymous resource inventory (verified public 2026-10-10 via anonymous
# api.github.com / ghcr token flow; zero tokens, zero private deps, zero
# secrets):
#   1. Base image:  ghcr.io/geniusventures/almalinux-8:latest (anonymous pull)
#   2. thirdparty:  https://github.com/GeniusVentures/thirdparty/releases/download/Linux-x86_64-develop-Release/Linux-x86_64-Release.tar.gz
#   3. zkLLVM:      https://github.com/GeniusVentures/zkLLVM/releases/download/Linux-x86_64-develop-Release/Linux-x86_64-Release.tar.gz
#   4. Submodules:  https://github.com/GeniusVentures/gRPCForSuperGenius.git
#                   https://github.com/GeniusVentures/GeniusKDF.git
#                   https://github.com/GeniusVentures/ProofSystem.git
#                   https://github.com/GeniusVentures/SGProcessingManager.git
#                   https://github.com/GeniusVentures/evmrelay.git
#   (git, ca-certificates, tar, gzip and curl come from the base image /
#    dnf; the CI image performs repo checkouts so git is present — stated
#    assumption, re-installed idempotently above as a safety net.)
# =============================================================================
