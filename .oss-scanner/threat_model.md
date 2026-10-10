# OSS Scanner Build — Scoped Threat Model

## Scope Statement

This document covers the **Anthropic OSS Scanner build context only**: the
`/src` source tree plus `.oss-scanner/Dockerfile` as compiled and exercised
inside the scanner's anonymous, unauthenticated Debian 12 amd64 VM
(45 min / 16 CPU / 64 GB / 120 GB budget, offline after the build phase).

It does **not** cover SuperGenius at runtime, deployed-network threats,
consensus/economics, or operator key management — those belong to the
runtime threat models maintained with the node documentation.

**Third-party exclusion:** the prebuilt `thirdparty/` and `zkLLVM/` release
artifacts, and vendored third-party sources inside the submodules (gRPC,
Boost, RocksDB, MNN, etc., consumed via `THIRDPARTY_BUILD_DIR` /
`ZKLLVM_BUILD_DIR`), are out of scope for this model — they are separately
maintained dependencies fetched as pinned public artifacts under their own
security review. SuperGenius **first-party code** under `src/`, `test/`,
`node/`, and the CMake build system is in scope.

This artifact satisfies the "scoped threat model" checklist line of
GeniusNetwork issue #10.

## Trust Boundaries

| # | Boundary | Direction / Exposure | Control |
|---|----------|----------------------|---------|
| 1 | Scanner VM → SuperGenius source tree | Anthropic infrastructure executes our build recipe on a non-recursive clone of `dev_ossscanner`/`develop` | The Dockerfile must not reference any private or authenticated resource; every fetch is anonymous (see inventory in Dockerfile trailer). The scanner never receives credentials to misuse. |
| 2 | Dockerfile → network | Anonymous egress from the build container | Exactly four resource classes: ghcr base image pull, two GitHub release assets, five public submodule repos. No token, no `--mount=type=secret`, no private clone URL can appear (structural gate in the plan's Task 1 verify). |
| 3 | Build-time code execution | CMake scripts, protobuf/proto compilers, codegen and test-build tooling run with build privileges inside the container | Standard supply-chain surface: the same first-party code CI already compiles. The scanner adds no new privilege; container isolation bounds blast radius to the ephemeral VM. |
| 4 | Test execution → host | Focused ctest subset runs at the end of the image build | The selected tests (`network_registry_test`, `network_membership_filter_test`, `blake2_test`) perform no outbound network calls and no `*.cap` capture-file deserialization; they are offline-capable, matching the scanner's offline-after-build requirement. |

## Untrusted Inputs

What crosses a boundary into the code the scanner compiles and runs:

| Input | Pinned by | Residual risk |
|-------|-----------|---------------|
| Submodule sources (gRPCForSuperGenius, GeniusKDF, ProofSystem, SGProcessingManager, evmrelay) | **Gitlink SHA recorded in the superproject tree** — the Dockerfile fetches each repo at that exact SHA and hard-fails if the SHA is missing or unpinned. Branch-head drift cannot silently change what is built. | Requires a superproject commit to change → mitigated by repo write permissions. |
| thirdparty / zkLLVM release tarballs | Tag name `Linux-x86_64-develop-Release` (asset name `Linux-x86_64-Release.tar.gz` is stable) | **Accepted residual risk — severity medium.** The tag is mutable-by-release: a re-published tag could swap artifact bytes. Rationale for accepting: tags are org-controlled (write access required), the artifacts are consumed as link-time data by first-party code, and checksum pinning of the two assets is a straightforward future hardening recorded in the README. |
| Test fixtures compiled into the tree | The superproject commit itself | None beyond ordinary repo review. |
| Compiler / toolchain (clang, cmake, ninja, dnf packages) | `ghcr.io/geniusventures/almalinux-8:latest` base image + AlmaLinux 8 repos — org-controlled image, distro-controlled packages | Same trust the production CI already places in these sources. |

## Security Goals

1. **Anonymous reproducibility** — no credential is required or referenced at
   any point; the build must succeed for any unauthenticated builder.
2. **Offline-after-build test execution** — the final `ctest` layer performs
   zero network fetches; binaries and fixtures are baked into the image.
3. **Pinned dependency graph** — no floating branch clones anywhere; every
   source input resolves to a recorded commit SHA or a named release asset.
4. **Bounded build** — focused `--target` build and focused `-R` ctest keep
   the recipe inside the scanner's 45-minute budget; the knob to widen the
   set is documented in the Dockerfile.
5. **No secret exfiltration surface** — no `ENV`/`ARG` carries a token, no
   secret mounts, and nothing in the recipe writes outside `/src`, `/opt` and
   `/tmp`.

## Severity Guidance

| Boundary violation | Impact | Severity |
|--------------------|--------|----------|
| Release-asset tag mutation (thirdparty/zkLLVM) | Supply-chain compromise of the scanner build — hostile bytes linked into every test binary | **High** (accepted today at medium likelihood: org-controlled tags; checksum pinning planned) |
| Submodule gitlink tamper | Requires superproject commit access; already mitigated by repo permissions + PR review | **Mitigated** |
| Dockerfile itself carries secrets | None possible — structural gate asserts zero token/secret/auth patterns; nothing sensitive exists in the file | **Info** |
| Scanner VM compromise via build scripts | First-party CMake executes with container privileges only; ephemeral VM, no persistence | **Low** (standard build-time execution surface) |
| Test suite performs network calls | Violates scanner offline contract; selected subset is network-free by construction | **Low** |
