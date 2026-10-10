# SuperGenius `.oss-scanner/` — Anthropic OSS Scanner Enrollment (Slice 1)

Artifacts for enrolling SuperGenius in Anthropic's OSS Scanner
(GeniusNetwork issue #10 follow-up; quick-task track per the 261009-rrt
evaluation). Slice 1 = the per-project build recipe + scoped threat model.
The upstream enrollment PR (slice 2) is maintainer/CLA-gated and NOT part of
this change.

## Contents

| File | Purpose |
|------|---------|
| `Dockerfile` | Anonymous, pinned, offline-after-build recipe the scanner executes (mirrors the Linux x86_64 Release CI leg of `.github/workflows/cmake.yml`) |
| `threat_model.md` | Scoped threat model for the scanner build context (issue #10 checklist line) |
| `README.md` | This file — validation state and reproduce commands |

## How the Dockerfile satisfies the scanner constraints

- **Anonymous** — zero credentials: anonymous ghcr base pull
  (`ghcr.io/geniusventures/almalinux-8:latest`), anonymous GitHub release
  asset downloads for `thirdparty`/`zkLLVM` (tag
  `Linux-x86_64-develop-Release`, asset `Linux-x86_64-Release.tar.gz`), and
  anonymous public submodule fetches. No `ARG`/`ENV` carries a token.
- **Pinned** — submodules are fetched at the exact **gitlink SHA** recorded
  in the superproject tree (`git ls-tree HEAD`), never at branch heads;
  release assets are tag-pinned (tag mutability noted as accepted residual
  risk in `threat_model.md`, checksums are future hardening).
- **Offline after build** — the final `ctest` layer runs binaries already
  baked into the image; no network fetch occurs after the build layer.
- **45-minute-focused** — builds and runs a representative test set
  (`network_registry_test`, `network_membership_filter_test`, `blake2_test`)
  whose dependency closure spans `src/`, ProofSystem, SGProcessingManager and
  evmrelay, instead of every target. Widen via the `--target` knob comment
  in the Dockerfile.

## Validation state (2026-10-10, Windows host)

**Partial (docker-build) validation.** Docker Desktop daemon was started and
a capped `docker build` ran for 15 minutes. Verified layers (buildkit logs):

| Layer | Result |
|-------|--------|
| anonymous ghcr base pull (`FROM`) | PASS (44.3s) |
| `dnf install` CI safety-net set + fetch tooling | PASS (34.9s) |
| anonymous thirdparty release download + extract | PASS (10.2s) |
| anonymous zkLLVM release download + excluded-extract | PASS (32.3s) |
| `COPY . /src` context transfer | host-specific stall — this working tree carries ~40GB of build outputs and populated submodules; a fresh non-recursive scanner clone is far smaller |
| configure / focused build / focused ctest | **Deferred to the scanner** (not reached within the cap) |

**Deferred — exact commands for later** (run from the SuperGenius repo root,
ideally from a fresh non-recursive clone):

```
docker build -f .oss-scanner/Dockerfile -t sg-oss-scanner .
docker run --rm sg-oss-scanner ctest --test-dir /src/build/Linux/Release/x86_64 -C Release --output-on-failure -R '^(network_registry_test|network_membership_filter_test|blake2_test)$' -LE requires_foundry
```

Also deferred (maintainer/CLA-gated per issue #10): running the scanner
repo's `tools/validate.py` and `tools/check` locally, and opening the
upstream enrollment PR.

## Target-name substitution note

The drafting plan referenced a `crypto_test` target. No CTest target of that
name exists; `test/src/crypto/` registers `blake2_test`, `hasher_test`,
`keccak_test`, `sha256_test`. The first of those (`blake2_test`) was used in
the build/ctest selections.

## Build-directory layout note

Both release tarballs lay out as `build/Linux/Release/x86_64/`
(target/build-type/abi — CI's `$BUILD_DIRECTORY` formula). The
`THIRDPARTY_BUILD_DIR`/`ZKLLVM_BUILD_DIR` values in the Dockerfile follow
this verified layout, confirmed by direct inspection of the published
assets.
