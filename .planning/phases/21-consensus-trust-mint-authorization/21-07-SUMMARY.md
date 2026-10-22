---
phase: 21-consensus-trust-mint-authorization
plan: "07"
subsystem: auth
tags: [trust-02, validator-admission-cap, equivocation-detection, element-filter, active-vote-ledger, d-04-cheap-guards, trus-02]

# Dependency graph
requires:
  - phase: 21-consensus-trust-mint-authorization/21-01
    provides: the verified-registry path (LoadVerifiedRegistryByCid / LoadUnverifiedRegistryByCid) this audit measures admission against
  - phase: 21-consensus-trust-mint-authorization/21-03
    provides: config-wired max_new_validators_per_update (SetMaxNewValidatorsPerUpdate applied at GeniusNode.cpp:860) whose enforcement this plan proves
  - phase: 21-consensus-trust-mint-authorization/21-04
    provides: the hardened certificate ingress (FilterCertificate verdict model, grandfather probe) where the equivocation guard lands
provides:
  - TRUST-02 audit deliverable (.planning/notes/2026-10-validator-admission-equivocation-audit.md) — F1-F4 findings with evidence, named attacker authorities, and dispositions; residuals named for the Phase 25 spine
  - ACTIVE-addition cap enforcement proof — config-wired value flows into the InsertNewValidators cap loop (over-cap truncates at exactly cap; wired 3 overrides default 10; sub-cap honest updates insert all)
  - Cross-certificate equivocation guard — signature-verified common-signer proof at the conflicting-certificate branch rejects the candidate and writes a durable marker reusing the ActiveVoteRecord ledger conventions
  - validator_admission_equivocation_test — five-case adversarial+compat matrix (VER-01 discipline)
affects: [25-verification-spine (F1/F3 residuals), TRUST-02-closure, VER-01]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Provable equivocation = two signature-verified votes by one signer over two different proposals at one canonical slot; the durable existing certificate IS the prior commitment (its bytes were read from the local store)"
    - "Durable audit markers reuse the ledger's record type and storage conventions under a SIBLING private prefix so the active-vote recovery prefix scan never decodes them"
    - "Guard cost discipline (D-04): one ledger read + one write, conflict branch only — no scanning, no quorum recomputation"
    - "Member-path cap testing through a friend TestAccess on a CRDTFixture GlobalDB (setter value provably flows; no static extraction needed)"

key-files:
  created:
    - .planning/notes/2026-10-validator-admission-equivocation-audit.md
    - test/src/blockchain/validator_admission_equivocation_test.cpp
  modified:
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/ValidatorRegistry.hpp
    - test/src/blockchain/consensus_pending_lifecycle_test.cpp
    - test/src/blockchain/CMakeLists.txt

key-decisions:
  - "The durable prior commitment for signer S is S's signature-verified vote inside the durably-stored existing certificate — not the local ActiveVoteRecord, which structurally holds only the LOCAL node's vote (DecodeActiveVoteRecord enforces voter_id == account_address_), so gating the reject on it would make the guard dead for remote-signer equivocation, exactly the byzantine case F2 names; the ledger IS consulted for corroborating local-commitment evidence (logged in the marker line)"
  - "Markers live under /consensus/equivocation/<slot>/<signer> (sibling of ACTIVE_VOTE_BASE_PATH_KEY), not literally under it: RecoverActiveVotes prefix-scans /consensus/vote/ and would decode-fail every marker with a per-boot error log; same storage conventions (GlobalDB::Buffer put, one ActiveVoteRecord-shaped payload — the signer's verified prior vote), no recovery interference (audit finding F4)"
  - "Verbatim-copied votes never prove equivocation (same proposal_id on both sides is a copy, not a second commitment); differing mint hashes imply differing proposal_ids, so the equal-id early return is belt-and-braces"
  - "The cap was tested through the real member path (friend TestAccess + CRDTFixture registry) instead of extracting a static helper: InsertNewValidators is pure over its inputs plus config members, and the member path additionally proves the 21-03 setter value flows — the stronger claim"
  - "TRUST-02 marked complete here: the audit document is the roadmap success criterion 2 deliverable and both D-04 guards are enforcement/adversarially proven with compat twins"

patterns-established:
  - "Multi-signer certificate fixture: MakeCertificateAtSlot(signers, nonce, tx_hash, embedded mint) — the pending_lifecycle MakeCertificateForRegistry precedent extended to arbitrary signer lists over one canonical slot"

requirements-completed: [TRUST-02]

# Metrics
duration: 100min
completed: 2026-10-21
---

# Phase 21 Plan 07: Admission Cap Enforcement + Equivocation Guard Summary

**TRUST-02 closed: the admission/equivocation audit ships with F1-F4 dispositions and both D-04 cheap guards proven — the config-wired ACTIVE-addition cap enforcement-tested at the member path, and a signature-verified common-signer equivocation proof at the conflicting-certificate branch that rejects the byzantine candidate and leaves a durable marker reusing the active-vote ledger's conventions, while disjoint-signer conflicts and honest flows keep today's behavior exactly**

## Performance

- **Duration:** 100 min (2026-10-21T08:46:05Z → 2026-10-21T10:26:10Z; includes a ~15 min full-tree build and 343 s serialized multi-account gate)
- **Tasks:** 3 (Task 2 TDD: RED → GREEN; Task 1 audit document; Task 3 compat twin completion)
- **Files modified:** 6 (2 created)

## Accomplishments

- **Audit document (Task 1, roadmap success criterion 2):** `.planning/notes/2026-10-validator-admission-equivocation-audit.md` — F1 (unbounded-across-epochs ACTIVE insertion: per-update cap mitigated with the enforcement proof, cross-epoch aggregate accepted as governance-bound residual), F2 (log-only equivocation: mitigated by this plan's guard), F3 (batch vote-summing netting: accepted with rationale — distinct slots are independent expressions and the same-slot offense is now caught by F2's guard; per-member attribution residual named), F4 (audit-added: marker placement must not poison the active-vote recovery scan — mitigated by the sibling prefix). Every finding carries file:line evidence, a named attacker authority, a disposition, and its compat consequence; the conclusion names the two residuals carried to Phase 25.
- **Equivocation guard (Task 2, TDD):** `ProveCrossCertificateEquivocation` — a signer whose vote signature verifies on BOTH certificates over their respective (different) proposals at one canonical slot is provable equivocation; the guard lives inside FilterCertificate's conflict branch only: reject the candidate + `RecordEquivocationMarker` (ledger-convention durable write + rule-named error log naming the signer, both proposals, and both mint tx hashes) + the ledger consult for local prior-commitment evidence. Cost bounded to one read + one write on the conflict branch (D-04 / T-21-0704).
- **Cap enforcement proof (Task 2):** the 21-03 config-wired cap provably flows — over-cap update inserts exactly `DefaultMaxNewValidatorsPerUpdate` (10, all REGULAR/ACTIVE, deterministic sorted truncation), wired value 3 overrides the default, through the real member path with the real setter.
- **Compat twins (Tasks 2+3):** disjoint-signer conflict keeps today's lower-hash ordering exactly (the expected decision is computed by mirroring the production hash rule and the marker absence is asserted — the guard's no-false-positive proof); sub-cap honest update inserts all new validators with vote-derived penalty bookkeeping.

## Task Commits

1. **Task 1: Admission/equivocation audit document** — `718afe123` (docs: F1-F4 findings + dispositions)
2. **Task 2: Cheap guards — cap enforcement proof + equivocation check (tdd)** — `d6ec63d32` (test: RED — 3 pass / 1 fail exactly as the audit's F2 describes; friend accessors + key-derivation scaffolding only) + `0ff2e46aa` (feat: GREEN — proof, marker, conflict-branch reject, stale lifecycle pin updated)
3. **Task 3: Adversarial + compat test target** — `3c717dca3` (test: sub-cap honest-update compat twin; 5/5 suite)

**Plan metadata:** (this commit)

## Files Created/Modified

- `.planning/notes/2026-10-validator-admission-equivocation-audit.md` — the TRUST-02 deliverable (68 lines): scope (hardened path), findings table, per-finding evidence/attack/disposition/compat, Phase 25 residual conclusion.
- `src/blockchain/Consensus.hpp` — `EQUIVOCATION_RECORD_BASE_PATH_KEY` constant (sibling prefix, Doxygen names the recovery-scan rationale), `EquivocationRecordStorageKey` static, `ProveCrossCertificateEquivocation` / `RecordEquivocationMarker` declarations, `ValidatorAdmissionEquivocationTestAccess` friend.
- `src/blockchain/Consensus.cpp` — the two guard functions (proof with dual signature verification incl. verbatim-copy exclusion; marker write reusing `BuildActiveVoteRecord` + ledger consult) and the conflict-branch reject; everything outside the branch byte-identical.
- `src/blockchain/ValidatorRegistry.hpp` — `ValidatorAdmissionEquivocationTestAccess` friend (member-path cap testing).
- `test/src/blockchain/validator_admission_equivocation_test.cpp` — new target (558 lines): five-case matrix with VER-01 comments (attacker authority, expected, actual pre-fix); CRDTFixture harness with genesis registry (two GENESIS validators, quorum 1/2), nonce slot-key handler mapping account+nonce to one slot, multi-signer certificate builder, durable /cert/<slot> writer, marker reader.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — `FilterCertificateTreatsSameMintAlternatesAsNormalAndDifferentMintQuorumsAsFaults` updated to the new contract: the deliberately-overlapping-validator fixture's two directions now expect the equivocation Reject + durable marker; the same-mint alternate keeps the hash-ordering path; TestAccess gains the marker-key derivation.
- `test/src/blockchain/CMakeLists.txt` — `addtest(validator_admission_equivocation_test …)` (blockchain_genesis, genius_node_test, rapidjson, base_crdt_test — the consensus_pending_lifecycle link set).

## Decisions Made

- The plan sketched the prior-commitment lookup as "DecodeActiveVoteRecord at that slot key"; taken literally that gate can only ever prove the LOCAL node's commitment (the decoder enforces `vote.voter_id() == account_address_`), which would make the guard dead for remote byzantine signers — the exact F2 attack. The implemented proof stands on the two verified signatures (the existing certificate's bytes came from the local store = the durable prior commitment); the ledger is still consulted and its local-commitment verdict is recorded in the marker's log line. Documented in the audit and here.
- Marker prefix is a sibling, not the literal active-vote prefix (audit F4): same storage conventions, zero recovery-scan interference.
- The lifecycle test's stale pin was updated rather than the guard weakened: that fixture deliberately makes validator 0 sign two conflicting certificates ("the critical Mint-equivocation diagnostic") and pinned the log-only response; T-21-0702 mandates the reject, and the disjoint-signer twin (this suite) plus the same-mint alternate (that suite) prove honest contention is untouched.
- Cap tested at the member path with a friend accessor instead of a static extraction: the member path proves the wired setter value flows (the plan's stronger option when reachable).

## Requirements Status

`requirements: [TRUST-02]` marked COMPLETE. "Admission growth bounds and cross-certificate equivocation detection are AUDITED, with findings documented": the audit document exists with F1-F4 dispositions (growth-bounds half: per-update cap config-wired since 21-03 and enforcement-proven here, cross-epoch residual dispositioned; equivocation half: detection now has teeth — proven equivocation rejects the conflicting certificate with durable evidence, and the guard's own DoS/honest-conflict threats T-21-0704/0705 are mitigated and twin-proven).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Consensus.hpp modified (not in files_modified)**
- **Found during:** Task 2 implementation
- **Issue:** New member functions must be declared in the class; the plan's files_modified omitted Consensus.hpp.
- **Fix:** Declarations + constant + friend added to Consensus.hpp (minimal, documented).
- **Committed in:** d6ec63d32 / 0ff2e46aa

**2. [Rule 1 - Bug avoidance] Marker prefix is a sibling of ACTIVE_VOTE_BASE_PATH_KEY, not under it**
- **Found during:** Task 2 design
- **Issue:** The plan said "reuse the ledger's storage conventions under ACTIVE_VOTE_BASE_PATH_KEY"; RecoverActiveVotes prefix-scans that key and would decode-fail every marker, logging a per-boot error per marker.
- **Fix:** `/consensus/equivocation/<slot>/<signer>` — identical conventions (GlobalDB::Buffer put, one ActiveVoteRecord-shaped payload), no scan interference. Recorded as audit finding F4.
- **Committed in:** 0ff2e46aa

**3. [Rule 3 - Blocking] Stale behavior pin in consensus_pending_lifecycle_test.cpp**
- **Found during:** Task 2 GREEN consensus gate (1/37 failed)
- **Issue:** `FilterCertificateTreatsSameMintAlternatesAsNormalAndDifferentMintQuorumsAsFaults` deliberately overlaps validator 0 across two conflicting certificates and pinned the old log-only outcome (Accept for the lower-hash direction).
- **Fix:** Both overlap directions now expect the equivocation Reject + durable marker (asserted on the store); the same-mint alternate keeps the hash-ordering path; comments updated. This is the plan's mandated behavior change, not a fixture accommodation.
- **Files modified:** test/src/blockchain/consensus_pending_lifecycle_test.cpp
- **Committed in:** 0ff2e46aa

**4. [Plan-shape] Prior-commitment proof stands on the two verified signatures, not a DecodeActiveVoteRecord gate**
- **Found during:** Task 2 design
- **Issue:** The ledger structurally holds only the LOCAL node's vote; gating the reject on it would never fire for remote signers (the byzantine case).
- **Fix:** Durable prior commitment = the signer's verified vote inside the durably-stored existing certificate; the ledger is consulted for corroborating evidence (logged). See Decisions.
- **Committed in:** 0ff2e46aa

**5. [RED-shape] Compile scaffolding rode the RED commit (precedent)**
- **Found during:** Task 2 RED authoring
- **Issue:** The tests reference private surfaces and the marker-key helper.
- **Fix:** RED commit carries friend accessors + the pure key-derivation helper with zero guard behavior — 3 pass / 1 fail (21-04/21-06 precedent).
- **Committed in:** d6ec63d32

---

**Total deviations:** 4 auto-fixed/design + 1 TDD-shape
**Impact on plan:** All required by compilability, the plan's own compat contracts, or honest guard semantics — no scope creep (ValidatorRegistry.cpp untouched: the member path was directly testable, the plan's preferred option).

## Issues Encountered

- NONCE_SUBJECT_TYPE is a namespace-scope constant (Consensus.hpp:41), not a ConsensusManager member — one-line compile fix during RED.
- clang-tidy cannot consume the dual-arch compile_commands.json; used the filtered single-arch DB at /tmp/sgns-tidy-db (21-01..21-06 methodology), extended with the new test TU. Zero findings on all new-code regions (Consensus.cpp guard functions + conflict branch, the test file itself) outside the codebase-wide accepted Recommended-group noise; the test file additionally shows only protobuf-generated-header noise beyond that.
- Full-tree build ~15 min (Consensus.hpp reaches most TUs); multi_account gate 343 s serialized — both run to completion rather than subsetted.

## Verification

- TDD gates: RED `d6ec63d32` (3 pass / 1 fail — the equivocation adversarial fails exactly as F2 describes: critical log then Accept, no marker) precedes GREEN `0ff2e46aa` (4/4).
- Task 2 gate `ctest -R "validator_admission_equivocation|validator_registry"` — 6/6; extended with consensus suites — 11/11 (5 consensus targets incl. the updated 37/37 lifecycle suite, 5 validator_registry targets, the new target).
- Task 3 gate `ctest -R validator_admission_equivocation` — 1/1 (5/5 cases, 558 lines ≥ min_lines 120).
- Regression on the guard's tree (real-socket certificate flows through the changed branch): `multi_node_finality_fault` 3/3, `multi_account` + `policy_lifetime_multi_account_test` 2/2 (343 s).
- Full-tree `cmake --build build/OSX/Release -j` — clean, no new warnings (only target names containing "error" match a warning grep).
- clang-format `--dry-run --Werror` clean on all five changed C++ files; clang-tidy zero findings on new-code regions (see Issues Encountered).
- Acceptance greps: "equivocation" present in Consensus.cpp (branch + helpers); the accept path outside the conflict branch byte-identical (git diff scoped — additions only inside the branch and in new functions); `max_new_validators_per_update` named in the cap test's config-wiring comment; audit doc 68 lines with per-finding "Disposition" entries.

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - the proof, marker, reject, and cap enforcement are all live production paths; no placeholder data.

## Threat Flags

None - all five threat-register dispositions implemented and test-proven (T-21-0701 cap enforcement-tested with the wired value, cross-epoch residual documented in the audit; T-21-0702 proven equivocation rejects with durable marker; T-21-0703 dispositioned per the audit as accepted-with-rationale, no speculative rewrite; T-21-0704 conflict-branch-only cost, one read + one write; T-21-0705 disjoint-signer compat twin + the lifecycle same-mint alternate keep honest contention on today's path). No new security surface beyond the plan's model.

## Next Phase Readiness

- Phase 21 complete: TRUST-01 (21-04), TRUST-02 (this plan), MINT-02/MINT-04 (21-02), MINT-03 (21-05/21-06) all closed; the phase's in-phase VER-01 obligation is met across all domains.
- Phase 25 spine should pick up the two audit residuals: cross-epoch aggregate admission growth (F1) and per-member batch penalty attribution (F3), plus the audit doc as the baseline for the equivocation evidence trail (markers are durable local records, per signer per slot).

## Self-Check: PASSED

All 6 key files exist on disk; all 4 task commits (718afe123, d6ec63d32, 0ff2e46aa, 3c717dca3) found in git history.

---
*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-21*
