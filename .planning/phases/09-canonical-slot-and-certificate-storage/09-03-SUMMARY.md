---
phase: 09-canonical-slot-and-certificate-storage
plan: 03
subsystem: consensus-storage
tags: [crdt, certificate-store, canonical-slot, atomic-delta, startup-compatibility]

requires:
  - phase: 09-01
    provides: Canonical 64-hex consensus slot derivation for nonce and bridge subjects
provides:
  - Atomic v2 certificate slot records paired with winning transaction indexes
  - Complete-delta CRDT validation before element-level merge
  - Write-once conflict and byte-identical replay semantics
  - Pre-side-effect startup rejection for legacy or malformed certificate namespaces
affects: [10-honest-certificate-formation, certificate-recovery, transaction-validation]

tech-stack:
  added: []
  patterns:
    - Namespace-scoped complete-delta validation runs before existing element filters
    - Authoritative slot payload and winner index are published in one CRDT batch
    - Existing protocol state is compatibility-checked before subscriptions, timers, filters, or recovery

key-files:
  created:
    - test/src/blockchain/consensus_certificate_store_test.cpp
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/crdt/crdt_data_filter.hpp
    - src/crdt/impl/crdt_data_filter.cpp
    - src/crdt/crdt_datastore.hpp
    - src/crdt/impl/crdt_datastore.cpp
    - src/crdt/globaldb/globaldb.hpp
    - src/crdt/globaldb/globaldb.cpp
    - test/src/blockchain/CMakeLists.txt

key-decisions:
  - "Store the complete deterministic certificate at /cert/v2/slot/<slot_id> and only its slot ID at /cert/v2/tx/<winning_tx_hash>."
  - "Treat only a byte-identical slot certificate plus matching winner index as an idempotent replay; partial or differing state fails closed."
  - "Resolve transaction-hash certificate reads through the winner index and re-derive both slot and winner from the authoritative certificate."
  - "Reject every non-exact key under /cert/ before any consensus startup side effect; v2.0 performs no migration or dual read."

patterns-established:
  - "Certificate pair invariant: exactly one canonical slot key and one canonical winner-index key whose values derive from the same fully validated certificate."
  - "Startup compatibility gate: inspect durable protocol state immediately after dependencies and work journal become available."

requirements-completed:
  - CERT-01
  - CERT-02
  - CERT-03
  - COMP-02

duration: 36 min
completed: 2026-07-23
---

# Phase 09 Plan 03: Canonical Certificate Storage Summary

**Consensus certificates now persist as one validated atomic v2 slot/index pair with exact replay semantics, conflict diagnostics, and fail-fast legacy-state startup protection.**

## Performance

- **Duration:** 36 min
- **Started:** 2026-07-23T13:51:07Z
- **Completed:** 2026-07-23T14:26:56Z
- **Tasks:** 3
- **Files modified:** 10

## Accomplishments

- Added thread-safe, namespace-scoped complete-delta filters that can reject an entire sibling pair before existing per-element CRDT filters run, without affecting unrelated namespaces.
- Replaced transaction-keyed certificate writes with deterministic `/cert/v2/slot/<slot_id>` payloads and `/cert/v2/tx/<winning_tx_hash>` indexes emitted in one batch.
- Enforced fully validated derivations, typed integrity/conflict failures, byte-identical idempotency, slot-only callbacks/recovery, and winner-index certificate lookup.
- Rejected malformed, partial, mismatched, invalidly signed, or security-surface-mutated replicated pairs without making either logical v2 record visible.
- Added a compatibility scan before consensus subscriptions, timer startup, filter/listener registration, topic listening, or recovery; any legacy or malformed `/cert/` key stops startup with an offending-key diagnostic.

## Task Commits

Each task was committed atomically:

1. **Task 1: Scaffold certificate-store tests and add pair-aware pre-merge validation** - `d8628dff` (feat)
2. **Task 2: Publish authoritative slot certificates with write-once winning indexes** - `649e2c3b` (feat)
3. **Task 3: Fail startup on legacy certificate state before consensus side effects** - `b7d8177c` (feat)

Verification follow-up:

- `497c3059` - Exercises mutations across subject, proposer/account identity, proposal ID, nonce, transaction hash, embedded transaction, registry, and vote surfaces.

## Files Created/Modified

- `src/crdt/crdt_data_filter.hpp` / `src/crdt/impl/crdt_data_filter.cpp` - Register, snapshot, execute, and unregister complete-delta validation callbacks before element filtering.
- `src/crdt/crdt_datastore.hpp` / `src/crdt/impl/crdt_datastore.cpp` - Expose delta-filter lifecycle through the datastore.
- `src/crdt/globaldb/globaldb.hpp` / `src/crdt/globaldb/globaldb.cpp` - Expose the pair-aware filter seam to consensus storage.
- `src/blockchain/Consensus.hpp` / `src/blockchain/Consensus.cpp` - Define v2 namespaces, typed store failures, atomic writes, pair validation, slot-only handling/recovery, indexed reads, and startup compatibility checks.
- `test/src/blockchain/CMakeLists.txt` - Adds the focused certificate-store target.
- `test/src/blockchain/consensus_certificate_store_test.cpp` - Covers pair filtering, atomic submit, replay, conflict, signed-surface mutation, and startup compatibility.

## Decisions Made

- The slot record is authoritative; the transaction-hash record contains only the canonical slot ID and is never routed to certificate handlers.
- Deterministic protobuf bytes define exact replay equality without changing the certificate protobuf or signing bytes.
- Pair validation reuses complete certificate semantics and independently re-derives the canonical slot and winner before merge.
- Startup compatibility is fail-closed on query/key-decoding failures and on every non-exact key below `/cert/`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Quorum recomputation included votes that failed signature validation**

- **Found during:** Task 2 invalid-signature delta verification
- **Issue:** `TallyVotes` computed approved weight from verified votes but passed the original unverified vote set into `EvaluateQuorum`, allowing a malformed signature to satisfy the later quorum check.
- **Fix:** Retained only verified votes for quorum evaluation and required serialized total/approved weights to match the recomputed certificate weights.
- **Files modified:** `src/blockchain/Consensus.cpp`
- **Verification:** Invalid-signature pair rejection passes; the full certificate-store target and pending-lifecycle regression both pass.
- **Committed in:** `649e2c3b`

---

**Total deviations:** 1 auto-fixed (1 correctness/security bug).
**Impact on plan:** The fix was required for the planned complete-certificate validation guarantee and did not change wire formats or signing bytes.

## Issues Encountered

- The plan referenced a standalone `crdt_atomic_transaction_test` target that does not exist in this build graph. The same three atomic-transaction cases are part of the combined `crdt_test` target, so verification used `crdt_test --gtest_filter='*AtomicTransaction*'`.
- The new fixture initially lacked a secure-storage factory, causing deterministic test accounts to fail. It now installs `MemorySecureStorage`.
- Deterministic serialization initially returned while a protobuf stream still owned the backing string; switching to a scoped pre-sized `ArrayOutputStream` removed the lifetime fault.

## User Setup Required

None - no external service configuration required.

## Verification

- Combined build — PASS: `crdt_test`, `consensus_certificate_store_test`, `consensus_pending_lifecycle_test`, and `consensus_slot_key_test`.
- CRDT atomic transaction regression — PASS, 3/3 tests.
- Complete certificate-store target — PASS, 11/11 tests.
- Pair/Delta certificate slice — PASS, 5/5 tests.
- Legacy/Startup compatibility slice — PASS, 3/3 tests.
- Consensus pending-lifecycle regression — PASS, 7/7 tests.
- Canonical slot-key regression — PASS, 11/11 tests.
- `git diff --check` — PASS.

## Next Phase Readiness

- The durable certificate store now enforces one canonical finality record per slot and supplies a safe winner-hash lookup path.
- Phase 10 can prevent honest concurrent competing-certificate formation on top of the fail-closed storage conflict boundary.
- No blockers.

## Self-Check: PASSED

- All ten implementation/test files exist and all four implementation/verification commits are present.
- Focused and adjacent regression suites pass.
- CERT-01, CERT-02, CERT-03, and COMP-02 are covered by executable storage and startup tests.
- Pre-existing user changes in `ProofSystem`, `src/account/GeniusNode.cpp`, `.claude/`, `src/account/log_bridge_race.txt`, and `test_failed` remain unstaged and untouched.

---
*Phase: 09-canonical-slot-and-certificate-storage*
*Completed: 2026-07-23*
