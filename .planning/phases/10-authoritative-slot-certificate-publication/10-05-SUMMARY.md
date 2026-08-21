---
phase: 10-authoritative-slot-certificate-publication
plan: "05"
status: complete
---

# Plan 10-05: Registry batch canonical-slot certificate lookup Summary

**Registry batches retain subject hashes as batch metadata while loading certificate authority only from proposal-derived canonical slots.**

## Accomplishments

- Retained an in-memory subject-hash to generic-slot association when a non-batch certificate is finalized.
- Replaced batch certificate reads with `/cert/<slot>` reads and exact payload-to-slot binding checks.
- Kept batch root calculation, selection, and stored subject-hash metadata unchanged; absent slot association fails closed and never attempts a legacy subject-hash lookup.
- Added a dedicated registry lookup CTest target covering canonical success plus legacy-only, malformed, mismatched, and unavailable evidence rejection.

## Rule-3 deviation

- Added `ValidatorRegistry` as a friend of `ConsensusManager` in `src/blockchain/Consensus.hpp`. `GetSlotKey` is private, and this is the smallest non-public-surface change that lets the registry use the required existing generic slot derivation without duplicating it.

## Verification

- `cmake -S build/OSX -B build/OSX/Release`
- `cmake --build build/OSX/Release --target validator_registry_certificate_lookup_test --parallel 1` — passed.
- `ctest --test-dir build/OSX/Release -R '^validator_registry_certificate_lookup_test$' --output-on-failure` — 1/1 passed.
- `git diff --check` — passed.
- The combined phase-focused CTest run built all three requested targets, but the pre-existing `consensus_pending_lifecycle_test` aborted in `AuthoritativeSlotLookupReturnsOnlyAnApprovedBoundCertificate`: its fixture constructs a proposal with `proposer_id=validator-pending-lifecycle` while signing with a different generated account address, so certificate validation rejects the signature. This plan does not modify that lifecycle fixture.

## Task Commits

1. **Task 1: Add registry slot certificate lookup regressions** — pending.
2. **Task 2: Migrate registry batch lookup to validated generic slots** — pending.
