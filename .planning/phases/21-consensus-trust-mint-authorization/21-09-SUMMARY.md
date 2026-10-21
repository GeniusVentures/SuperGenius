---
phase: 21-consensus-trust-mint-authorization
plan: "09"
subsystem: auth
tags: [consensus-trust, gap-closure, grandfather-probe, content-binding, defer-bounded, wr-01, cr-03, wr-02, trus-01]

# Dependency graph
requires:
  - phase: 21-consensus-trust-mint-authorization/21-04
    provides: the D-01 grandfather probe + classified verdicts + authorization_defer_tracker_ whose contracts this plan hardens
  - phase: 21-consensus-trust-mint-authorization/21-03
    provides: base::BoundedDeferTracker (first-seen deadlines) reused as the CR-03 bound
provides:
  - Content-bound D-01 grandfather probe (WR-01 closure): only the certificate whose own serialized bytes are the durable /cert/<slot> record skips strict authorization; every other certificate at that slot — forged registry, attacker quorum, lower hash — takes the strict verified-registry path and is rejected
  - Lower-hash overwrite vector closed at authorization time (T-21-0903): a conflicting certificate at a finalized slot never Approves, so FilterCertificate's :3550 overwrite branch is unreachable for it
  - Defer-bounded grandfathered registry-load failure (CR-03 closure): the same authorization_defer_tracker_ under the same registry-CID key as the strict path; in-window never-memoized Stalled, past-window deterministic Reject — no unbounded whole-delta stall remains on any certificate path
  - WR-02 closure: TransactionManager::SetLegacyMintDeferWindow warns-and-ignores negative windows, mirroring its consensus twin; misconfigured config can no longer insta-tombstone late-certificate v1 history
  - TRUST-01 legitimacy: the requirement's Complete mark is now backed by strict path (21-04) + content-bound probe + bounded grandfathered stall + adversarial/compat proofs
affects: [25-verification-spine, 24-bridge-ingestion-safety]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Content binding by serialized-hash equality at the authorization boundary: the durable record must BE the candidate (ParseFromArray + SerializedCertificateHash on stored bytes vs candidate serialization — the SubmitCertificate/FilterCertificate discipline); parse failure or hash mismatch fails closed onto the strict path"
    - "One defer tracker, one key, two authorization paths: strict and grandfathered registry-load failures share authorization_defer_tracker_ under the registry-CID key, so one unfetchable CID carries one first-seen bound across both"
    - "Setter-level negative-window guards (the consensus twin mirrored verbatim in spirit): guard at the one home every caller flows through, zero stays legal as the sanctioned test override"

key-files:
  created: []
  modified:
    - src/blockchain/Consensus.cpp
    - src/blockchain/Consensus.hpp
    - src/transaction/TransactionManager.cpp
    - src/transaction/TransactionManager.hpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - test/src/transaction_sync/legacy_mint_sync_admission_test.cpp

key-decisions:
  - "Grandfathering binds by CONTENT, not slot-record existence: the probe parses the durable record and requires SerializedCertificateHash equality with the candidate's serialization — the record that is not THIS certificate proves nothing about the candidate (Pitfall-4 anti-pre-seed reasoning extended from 'a record exists' to 'the record IS the certificate')"
  - "The candidate is serialized exactly the way the record was written (certificate.SerializeToString, the SubmitCertificate path); ConsensusCertificate has no map fields so C++ protobuf serialization is canonical and parse-re-serialize roundtrips byte-identically — verified live by the restart-grandfather compat at unit AND real-socket level"
  - "CR-03 routes through the SAME tracker and SAME registry-CID key as the strict path (one CID, one first-seen bound); accepted D-03 residual stated in-code: a final certificate whose dependency never syncs becomes Reject past the deliberately generous default window"
  - "The lower-hash overwrite is closed INDIRECTLY (T-21-0903): no change to FilterCertificate, the equivocation guard, or the :3550 rule — closing authorization at ValidateCertificate closes the overwrite for every path that consults it (proven by the no-overwrite test asserting Reject + stored bytes intact + filter kReject)"
  - "Honest genesis CID must be captured BEFORE MakeForgedRegistryCid's raw put: the forged update lands at the same registry CRDT key and advances the registry's cached CID, silently handing later GetRegistryCid callers the forged one (fixture-ordering trap found at RED)"

patterns-established:
  - "Adversarial lower-hash engineering without brute force: re-sign timestamp-shifted variants of the conflicting certificate until one serializes below the stored winner's hash (each draw is a coin flip; bounded loop with a found-assert)"
  - "Pre/post clang-tidy comparison on all changed files (including tests) by materializing the pre-plan file in place against the filtered single-arch DB — proves zero NEW findings rather than eyeballing"

requirements-completed: [TRUST-01]

# Metrics
duration: 31min
completed: 2026-10-21
---

# Phase 21 Plan 09: TRUST-01 Gap Closure (WR-01 content binding + CR-03 defer bound + WR-02 guard) Summary

**Both code-confirmed TRUST-01 grandfather-path bypasses from 21-VERIFICATION.md gap 3 are closed — the grandfather probe now binds by serialized-hash content equality so a newly crafted certificate at an already-finalized slot (forged registry, attacker quorum, lower hash) takes the strict verified-registry path and is Rejected, the grandfathered registry-load stall is bounded by the same D-03 tracker/key as the strict path, and the WR-02 negative-window guard lands on SetLegacyMintDeferWindow — restoring TRUST-01's Complete mark legitimately with the D-01 content-equal compat twin green at unit and real-socket level**

## Performance

- **Duration:** 31 min (2026-10-21T18:48:02Z → 2026-10-21T19:18:43Z)
- **Tasks:** 3 (all TDD: RED → GREEN)
- **Files modified:** 6 (0 created)

## Accomplishments

- **WR-01 content-bound probe (Task 1, TDD):** the D-01 probe (Consensus.cpp:3639-3685) still reads the canonical slot record with the write path's key derivation, but now parses it via `ParseFromArray` (the SubmitCertificate precedent) and grants `grandfathered_final` ONLY on `SerializedCertificateHash( stored bytes ) == SerializedCertificateHash( candidate serialization )`. Parse failure or hash mismatch fails closed onto the strict verified-registry path — the record that is not THIS certificate proves nothing about the candidate. The NOT_FOUND → strict and non-NOT_FOUND probe-error → Stalled branches are byte-identical to before; `grandfathered_finality_probes` now counts content-bound hits only; Consensus.hpp's counter comment and ValidateCertificate Doxygen state the content-bound contract. FilterCertificate, the equivocation guard, and the lower-hash rule are untouched.
- **CR-03 defer-bounded grandfathered load failure (Task 2, TDD):** after `LoadUnverifiedRegistryByCid` fails on the grandfathered path, `authorization_defer_tracker_.Defer( certificate.registry_cid() )` — the SAME tracker and SAME registry-CID key the strict path uses — maps an expired first-seen deadline to a deadline-named deterministic `Check::Reject` and an in-window failure to today's never-memoized `Check::Stalled`. In-code note records the accepted D-03 residual (a final certificate whose dependency never syncs becomes Reject past the deliberately generous default window). No certificate-path registry stall is unbounded anymore.
- **WR-02 negative-window guard (Task 3, TDD):** `TransactionManager::SetLegacyMintDeferWindow` mirrors its consensus twin: `window.count() < 0` logs `ignoring negative legacy mint defer window` and returns without touching `legacy_mint_defer_tracker_` — the current window is retained. Zero stays legal (the sanctioned test override). Guards the one home every caller flows through, including the GeniusNode `authorization_defer_window_sec` config path.
- **Adversarial + compat tests (VER-01 comments naming attacker authority, expected rejection, recorded pre-fix behavior):** `CraftedCertificateAtFinalizedSlotRejected` (shared-subject C1/C2 at the attacker's own finalized slot, forged CID raw-put, disjoint signer set named as the reason the equivocation guard cannot fire; pre-fix Approve — the WR-01 attack), `ConflictingLowerHashCertificateAtFinalizedSlotDoesNotOverwrite` (timestamp-shifted re-signed variants engineered below the stored winner's hash; asserts ValidateCertificate Reject + FilterCertificate kReject + stored bytes intact; pre-fix Approve + kAccept through the overwrite rule), `GrandfatheredLoadFailureRejectsAfterDeferWindow` (content-equal genuine certificate, unloadable CID, zero window → Stalled then deterministic Reject; pre-fix Stalled forever), `GrandfatheredLoadFailureStallsWithinWindow` (default window → Stalled with zero verdict-cache entries, direct and accepted-scan), `NegativeLegacyMintDeferWindowIgnored` (1 s armed, -1 s fed → immediate re-evaluation still Stalls; post-window still rejects through the RETAINED 1 s bound; pre-fix immediate Reject + tombstone).
- **Fixture refactor (one home):** `MakeCertificateForRegistry` delegates to new `MakeSignedCertificateOverSubject` (caller-supplied subject + timestamp bump) so two certificates can share one slot's subject bytes while differing in proposer/registry/signers; `ReadCertificateAtKey` reads the durable record through the probe's own `db_->Get` seam.

## Task Commits

1. **Task 1: Content-bind the D-01 grandfather probe (WR-01, tdd)** — `09c7203f6` (test: RED — crafted cert at finalized slot Approves today via existence-only probe; lower-hash conflicting cert Approves + filter-accepts the overwrite; 37/37 existing green) + `886343e96` (feat: GREEN — parse + SerializedCertificateHash equality gate; hpp contract wording; suite 39/39)
2. **Task 2: Bound the grandfathered load stall via the D-03 tracker (CR-03, tdd)** — `d72b6d7cb` (test: RED — grandfathered load failure Stalls forever, deadline case fails; within-window compat guard passes) + `8bb7599bd` (feat: GREEN — shared tracker/key defer bound; suite 41/41)
3. **Task 3: WR-02 negative-window guard (tdd)** — `838a32797` (test: RED — immediate second-evaluation Reject through the negative window; 7/7 existing green) + `601fe5bf2` (feat: GREEN — warn-and-ignore guard mirroring the consensus twin; suite 8/8)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/blockchain/Consensus.cpp` — content-bound probe (parse + hash-equality gate, fail-closed comment) + defer-bounded grandfathered load failure with the accepted-residual note.
- `src/blockchain/Consensus.hpp` — `grandfathered_finality_probes` comment states content-bound semantics; ValidateCertificate Doxygen states the content-binding + defer-bound grandfather contract (existence-only claim removed).
- `src/transaction/TransactionManager.cpp` — `count() < 0` warn-and-return guard in `SetLegacyMintDeferWindow`.
- `src/transaction/TransactionManager.hpp` — setter Doxygen documents the negative-window ignore.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — four new adversarial/compat cases (37 → 41), `MakeSignedCertificateOverSubject` shared body, `ReadCertificateAtKey` seam.
- `test/src/transaction_sync/legacy_mint_sync_admission_test.cpp` — `NegativeLegacyMintDeferWindowIgnored` guard case (7 → 8).

## Decisions Made

- Content binding uses serialized-hash equality (the plan's prescribed minimum, matching VERIFICATION gap-3 wording and 21-REVIEW WR-01) rather than running the strict verified load on the grandfather path — finality's proof is exactly "these bytes are durably final", which keeps D-01 restart/catch-up compat byte-identical for genuine records.
- The WR-01 and CR-03 fixes compose: content binding shrinks the grandfathered load-failure path to certificates whose bytes are already durable, making the defer bound's residual (final cert with never-syncing dependency → Reject) narrow and acceptable.
- The lower-hash overwrite test engineers its hash ordering by re-signing timestamp-shifted variants (each a valid certificate) instead of touching any production ordering rule — no test-only hooks in production code.

## Requirements Status

`requirements: [TRUST-01]` marked COMPLETE — legitimacy restatement: the Complete mark in REQUIREMENTS.md, recorded by 21-01/21-04 but left unsupported per 21-VERIFICATION.md gap 3, is now legitimate. The certificate path's registry-by-CID authorization is verified end to end: the strict path rejects forged/self-certified CIDs with no durable record (21-04, re-proven by this plan's multi-node trio), the grandfather path now grants its skip ONLY to the certificate whose own bytes are the durable record (WR-01 closed: crafted-cert-at-finalized-slot Rejected, lower-hash overwrite unreachable), and every registry stall on either path is bounded by the shared D-03 tracker (CR-03 closed: expired → deterministic Reject). Compat survives at both levels: the content-equal unit twin and the real-socket restart/catch-up trio stay green.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test-fixture bug] Honest genesis CID captured before the forged raw-put**
- **Found during:** Task 1 RED
- **Issue:** C1 built with `registry->GetRegistryCid()` AFTER `MakeForgedRegistryCid` referenced the FORGED registry — the forged update lands at the same registry CRDT key and advances the registry's cached CID, so the "honest winner" tallied against the attacker's registry and failed quorum, making the lower-hash test fail for the wrong reason.
- **Fix:** Capture `honest_registry_cid` before the forged put in both new tests (fixture-ordering change only).
- **Files modified:** test/src/blockchain/consensus_pending_lifecycle_test.cpp
- **Committed in:** 09c7203f6

**2. [Rule 3 - Test-seam] Durable-record byte assertions needed the probe's own read path**
- **Found during:** Task 1 RED
- **Issue:** Reading the record via the raw datastore (`GetDataStore()->get`) errors for CRDT-written keys; the plan's "record still holds C1's bytes" assertion needed a positive read.
- **Fix:** `ReadCertificateAtKey` test-access helper reading through `manager->db_->Get` — the same seam the probe and FilterCertificate use.
- **Files modified:** test/src/blockchain/consensus_pending_lifecycle_test.cpp
- **Committed in:** 09c7203f6

**3. [Plan-text note] "expired on first sight" wording vs first-seen anchoring**
- **Found during:** Task 3 design
- **Issue:** The plan's recorded pre-fix behavior says a negative window makes Defer() "return expired on first sight"; the tracker anchors first-seen deadlines and returns false on the anchoring call, so the observable pre-fix failure is the FIRST RE-EVALUATION rejecting (deadline already in the past), not the very first call.
- **Fix:** none to production code — the guard and the test's shape are exactly as the plan prescribes; the RED test discriminates on the immediate second evaluation (Stall expected, Reject pre-fix) and sharpens with the retained-window expiry.
- **Files modified:** none

---

**Total deviations:** 2 auto-fixed test-fixture issues + 1 plan-text note
**Impact on plan:** None on scope or semantics.

## Issues Encountered

- clang-tidy is not on the default PATH; used `/opt/homebrew/opt/llvm/bin/clang-tidy` with the filtered single-arch DB at /tmp/sgns-tidy-db (21-04/21-08 methodology). Pre/post comparison on ALL FOUR changed C++ files (pre-plan versions materialized in place): zero own-file findings before AND after — zero new findings in changed regions; the ~10k/14k raw counts are thirdparty-header suppression noise identical in shape to 21-04's.
- Pre-existing unrelated working-tree state (SGProcessingManager submodule dirt, build/OSX/Release-ASan/, run.txt) present before this plan started — untouched, never staged.

## Verification

- `ctest -R "consensus_pending_lifecycle|legacy_mint_sync_admission|validator_admission_equivocation|legacy_mint_admission" --test-dir build/OSX/Release` — 4/4 targets passed; full matrices: consensus_pending_lifecycle 41/41 (37 pre-existing + 4 new), legacy_mint_sync_admission 8/8 (7 + guard case), adjacent unregression green.
- Optional multi-node TRUST-01 trio (real sockets, serialized): `ForgedRegistryCidCertificateIsRejectedWithNoDurableRecord`, `RestartedFinalCertificateRevalidatesThroughGrandfatherProbe`, `FreshNodeCatchUpStallsThenConvergesWithNoPinnedReject` — 3/3 PASSED (24.3 s) under the content-bound probe: restart/catch-up D-01 compat holds at the integration level and the forged CID is still rejected with no durable record.
- clang-format `--dry-run --Werror` clean on all six changed files.
- clang-tidy pre/post comparison (see Issues Encountered): zero new findings on changed regions.
- Acceptance greps: probe-site `SerializedCertificateHash` at Consensus.cpp:3666-3667 beside the SubmitCertificate (:2862) and FilterCertificate (:3550) uses; `authorization_defer_tracker_.Defer` reachable from BOTH the grandfathered (:3709) and strict (:3750) branches; `count() < 0` guard in SetLegacyMintDeferWindow (TM.cpp:4644) mirroring Consensus.cpp:690; Consensus.hpp no longer claims existence-only grandfathering.
- VERIFICATION.md gap-3 missing bullets satisfied verbatim: content binding (hash equality) in the probe; defer-bounded grandfathered failure; adversarial crafted-cert-at-finalized-slot rejected; conflicting-certificate overwrite does not succeed via lower-hash ordering.
- TDD gates: every task has its `test(...)` RED commit before its `feat(...)` GREEN commit (09c7203f6→886343e96, d72b6d7cb→8bb7599bd, 838a32797→601fe5bf2); each RED failed on the named attack and each GREEN turned exactly it green.

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - the content-binding gate and the defer bound are fully wired enforcement on live production paths; no placeholder data.

## Threat Flags

None - T-21-0901 implemented and unit-proven adversarially (content binding, fail-closed parse/mismatch); T-21-0902 implemented (shared tracker/key, in-window never-memoized Stalled, expired deterministic Reject); T-21-0903 closed indirectly exactly as registered (no-overwrite test asserts Reject + intact stored bytes + filter kReject); T-21-0904 implemented (warn-and-ignore at the setter); logs name the rule and carry no key material. No security surface beyond the plan's threat model.

## TRUST-01 Legitimacy Restatement (success criterion)

requirements-completed: [TRUST-01] — the Complete mark in REQUIREMENTS.md, recorded by 21-01/21-04 and left unsupported per 21-VERIFICATION.md gap 3, is restored legitimately: the strict verified-registry path (21-04), the content-bound grandfather probe (WR-01 closed here), and the defer-bounded grandfathered stall (CR-03 closed here) together close the public-network attack surface — every certificate at a finalized slot authorizes against verified registries unless the durable record IS the certificate, and every stall is bounded. WR-02 closed; WR-03/WR-04/WR-05/IN-01..04 remain documented Phase 25 residuals, untouched here.

## Self-Check: PASSED

All 6 modified key files plus the SUMMARY exist on disk; all 6 task commits (09c7203f6, 886343e96, d72b6d7cb, 8bb7599bd, 838a32797, 601fe5bf2) found in git history on gsd/phase-21-consensus-trust-mint-authorization.

---

*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-21*
