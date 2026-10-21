---
phase: 21-consensus-trust-mint-authorization
verified: 2026-10-21T17:05:00Z
status: passed
score: 5/5 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 3/5
  gaps_closed:
    - "MINT-04 / CR-01 multi-output inflation: DecodeAndBindBurnFacts now requires exactly one output (PCIV.cpp:113-118) before the fact comparisons; nullopt discards ALL evidence; MultiOutputMintFirstOutputMatchesStillRejected adversarial test green"
    - "MINT-04 / CR-02 bridge-less endpoint: endpoints with empty bridge_contract_address OR empty accepted_topic0_hashes earn zero weight on evidence-required types (PCIV.cpp:598-606, scoped by RequiresPublicChainEvidence); BridgelessEndpointQuorumFails + EmptyTopic0SetDoesNotConfirm green; non-mint bridge-optional compat twin green"
    - "TRUST-01 / WR-01 existence-only grandfather probe: probe now parses the durable record and requires SerializedCertificateHash equality before grandfathering (Consensus.cpp:3656-3690); CraftedCertificateAtFinalizedSlotRejected and ConflictingLowerHashCertificateAtFinalizedSlotDoesNotOverwrite green; content-equal D-01 compat twin green at unit AND real-socket level"
    - "TRUST-01 / CR-03 unbounded grandfathered stall: grandfathered registry-load failure routes through authorization_defer_tracker_ under the same registry-CID key as the strict path (Consensus.cpp:3706-3724); expiry -> deterministic Reject; GrandfatheredLoadFailureRejectsAfterDeferWindow + GrandfatheredLoadFailureStallsWithinWindow green"
    - "MINT-04 adversarial evidence coverage: both bypass shapes now covered by tests naming attacker authority with recorded pre-fix behavior (gap 4, partial -> closed)"
    - "Adjacent WR-02 (folded into 21-09): negative-window warn-and-ignore guard on SetLegacyMintDeferWindow (TM.cpp:4644-4648); NegativeLegacyMintDeferWindowIgnored green"
  gaps_remaining: []
  regressions: []
---

# Phase 21: Consensus Trust & Mint Authorization Verification Report (Re-Verification)

**Phase Goal:** The certificate trust anchor holds on a public network — a registry loaded by CID is authorization-verified with a grandfather rule that keeps historical finality re-validating — and every mint/escrow authorization gap from the assessment's highest-credibility findings (#1, #3, #4, #5, #14) closes fail-closed without orphaning legitimate state.
**Verified:** 2026-10-21T17:05:00Z
**Status:** passed
**Re-verification:** Yes — after gap closure (21-08, 21-09)

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | TRUST-01: certificate path rejects forged/self-certified registry CID; pre-upgrade certs re-validate after restart; catch-up sync unchanged | ✓ VERIFIED | Strict path intact (Consensus.cpp:3726-3766: verified loader, permission_denied -> Reject, D-03 deadline). WR-01 CLOSED: probe (:3656-3690) parses the durable record via ParseFromArray and grants grandfathered_final ONLY on SerializedCertificateHash( stored bytes ) == SerializedCertificateHash( candidate serialization ); parse failure or hash mismatch fails closed onto the strict path. Write path consistency confirmed: SubmitCertificate stores certificate.SerializeToString bytes at the same key derivation (:2836-2850). CR-03 CLOSED: grandfathered load failure (:3706-3724) calls authorization_defer_tracker_.Defer( registry_cid ) — the SAME tracker and key as the strict path (:3750) — expired -> deadline-named Check::Reject, in-window -> never-memoized Check::Stalled. Tests run by this verifier: consensus_pending_lifecycle 41/41 (incl. CraftedCertificateAtFinalizedSlotRejected, ConflictingLowerHashCertificateAtFinalizedSlotDoesNotOverwrite asserting ValidateCertificate Reject + FilterCertificate kReject + stored bytes intact, both GrandfatheredLoadFailure cases); multi-node real-socket trio 3/3 (ForgedRegistryCidCertificateIsRejectedWithNoDurableRecord 5.7s, RestartedFinalCertificateRevalidatesThroughGrandfatherProbe 7.1s — content-equal restart compat holds at integration level, FreshNodeCatchUpStallsThenConvergesWithNoPinnedReject 7.1s). Consensus.hpp contract rewritten to the content-bound + defer-bound wording (:1189-1195). |
| 2 | TRUST-02: admission growth-bounds and cross-certificate equivocation audit documented with findings and dispositions | ✓ VERIFIED (regression) | Audit doc intact (.planning/notes/2026-10-validator-admission-equivocation-audit.md, 68 lines, F1-F4 + 5 dispositions). Equivocation guard untouched by gap plans (ProveCrossCertificateEquivocation :1336, wired at :3538-3541). validator_admission_equivocation_test re-run: Passed. |
| 3 | MINT-02: evidence validation fails closed on empty/local chain ids and empty source references; native vs bridge mint per-type consensus rules | ✓ VERIFIED (regression) | Fail-closed branches intact after 21-08's edits to the same file: empty source_reference on mint types -> fail closed (PCIV.cpp:475-477), local/scoped-local chain id -> fail closed (:496-501), non-mint local bypass preserved (:510). mint_evidence_binding_test 17/17 (all four identity adversarial cases green); public_chain_input_validator_slot_test Passed. |
| 4 | MINT-03: newly submitted self-signed legacy-v1 mint rejected at every ingress; pre-cutover outputs spendable; fresh node syncs READY; restart recovers; admission gating only | ✓ VERIFIED (regression) | Three ingresses intact (TransactionConsensusHandler.cpp:824, TransactionManager.cpp:2025, TM.cpp:3808); one-home predicate IsValueCarryingLegacyMint (TM.cpp:4626-4634). legacy_mint_admission_test Passed; legacy_mint_sync_admission_test 8/8 (now incl. the WR-02 guard case). Real-socket compat re-run by this verifier under the changed probe: multi_account_test Passed (308.8s), migration_sync_test Passed (90.5s). WR-02 closed: SetLegacyMintDeferWindow negative-window guard (TM.cpp:4644-4648), NegativeLegacyMintDeferWindowIgnored green (Stall retained, then Reject through the retained 1s bound). |
| 5 | MINT-04: mint whose token/amount/destination mismatches the burn receipt rejected even when every RPC endpoint honestly reports the receipt | ✓ VERIFIED | CR-01 CLOSED: DecodeAndBindBurnFacts rejects GetUTXOParameters().second.size() != 1 at the top (PCIV.cpp:113-118), BEFORE the topic0 selection and the three fact comparisons (:167/:173/:179); caller branch (:656-662) converts nullopt into discarding ALL evidence (reverted-receipt precedent); deserializer stays N-output-open (D-02 discipline). CR-02 CLOSED: guard at :598-606 — RequiresPublicChainEvidence( tx ) && ( bridge_contract_address.empty() \|\| accepted_topic0_hashes.empty() ) -> warn + continue, before the bridge block (:610) and the weight credit (:670); per-endpoint continue semantics mirror the topic-mismatch precedent. Tests run by this verifier, by name: MultiOutputMintFirstOutputMatchesStillRejected (proto-route two-output fixture, output[0] = attested facts + output[1] extra value) OK; BridgelessEndpointQuorumFails (asserts valid == false AND successful_weight == 0) OK; EmptyTopic0SetDoesNotConfirm OK; BridgelessEndpointStillConfirmsNonMintPublicType (scoping compat: valid == true, weight 75) OK; MatchingV2FactsValidate (single-output compat) OK; full matrix 17/17. Adjacent full pipeline bridge_race_fault_rpc_test 2/2 Passed (81s). |

**Score:** 5/5 truths verified

### Previously-Failed Gap Dispositions (context preserved from initial verification)

| Prior Gap | Prior Status | Disposition | Closure Evidence |
| --- | --- | --- | --- |
| Gap 1 — MINT-04 multi-output inflation (CR-01) | failed | CLOSED by 21-08 Task 1 (e357a4c4c RED -> 2132e0b9c GREEN) | PCIV.cpp:113-118 output-count guard before the fact comparisons; MultiOutputMintFirstOutputMatchesStillRejected green (run by name by this verifier) |
| Gap 2 — MINT-04 bridge-less endpoint (CR-02) | failed | CLOSED by 21-08 Task 2 (2ad50c9f4 RED -> 6b67302de GREEN, hygiene 8aa4759ab) | PCIV.cpp:598-606 scoped no-confirm guard covering BOTH empty fields; BridgelessEndpointQuorumFails + EmptyTopic0SetDoesNotConfirm + non-mint scoping twin green |
| Gap 3 — TRUST-01 existence-only probe (WR-01) + unbounded grandfathered stall (CR-03) | failed | CLOSED by 21-09 Tasks 1-2 (09c7203f6 -> 886343e96; d72b6d7cb -> 8bb7599bd) | Consensus.cpp:3656-3690 content binding (ParseFromArray + SerializedCertificateHash equality); :3706-3724 defer-bounded failure; four adversarial/compat cases green; multi-node restart/catch-up trio green (re-run by this verifier) |
| Gap 4 — MINT-04 adversarial evidence coverage | partial | CLOSED by 21-08 | Both bypass shapes covered by VER-01 tests naming attacker authority and recorded pre-fix behavior |

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `src/account/PublicChainInputValidator.cpp` | Exactly-one-output constraint at binding + bridge-less endpoint no-confirm guard | ✓ VERIFIED | Guards at :113-118 and :598-606; diff vs pre-gap base is +30 lines, 0 deletions — surgical, nothing else changed |
| `src/blockchain/Consensus.cpp` | Content-bound grandfather probe + defer-bounded grandfathered load failure | ✓ VERIFIED | :3656-3690 and :3706-3724; diff is exactly the two fixes + comments; FilterCertificate/equivocation/lower-hash rule untouched |
| `src/blockchain/Consensus.hpp` | Content-bound + defer-bound grandfather contract wording | ✓ VERIFIED | :1189-1195 (existence-only claim removed); counter comment :828-834 states content-bound semantics |
| `src/transaction/TransactionManager.cpp` | Negative-window guard on SetLegacyMintDeferWindow | ✓ VERIFIED | :4644-4648; diff on this file is exactly the 11-line guard |
| `test/src/account/mint_evidence_binding_test.cpp` | Adversarial cases for both bypass shapes + compat twins | ✓ VERIFIED | 17 cases; proto-route MakeMultiOutputMintV2 fixture (attacker's actual wire shape); MakeValidatorWithBridgeBinding one-home refactor |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | Crafted-cert-at-finalized-slot, lower-hash-overwrite, grandfathered-deadline, stall-within-window cases | ✓ VERIFIED | 41 cases; all four new cases substantive (real forged-CID raw-put, disjoint signer sets named, lower-hash engineering loop, durable-record byte assertions via ReadCertificateAtKey) |
| `test/src/transaction_sync/legacy_mint_sync_admission_test.cpp` | Negative-window-ignored guard test | ✓ VERIFIED | 8 cases; guard test discriminates Stall-retained vs pre-fix immediate Reject and re-verifies the retained window still expires |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| DecodeAndBindBurnFacts (PCIV.cpp:103) | MintTransactionV2::GetUTXOParameters().second.size() | output-count guard before the fact comparisons | ✓ WIRED | :113 rejects != 1; nullopt -> caller :656-662 discards all evidence |
| GatherVerificationEvidence endpoint loop | RequiresPublicChainEvidence (PCIV.cpp:70) | scoped bridge-less guard continue before weight credit | ✓ WIRED | :598-606 fires only for mint/mint-v2; covers both empty bridge address and empty topic0 set; continue-only, no whole-claim abort |
| ValidateCertificate grandfather probe | SerializedCertificateHash (stored bytes vs candidate serialization) | hash-equality gate before grandfathered_final = true | ✓ WIRED | :3663-3667; parse failure or mismatch leaves strict path; write path (:2836-2850) stores the same serialization discipline |
| ValidateCertificate grandfathered load failure | authorization_defer_tracker_.Defer( registry_cid ) | expired -> Check::Reject, else Check::Stalled | ✓ WIRED | :3709 (grandfathered) and :3750 (strict) — same tracker, same key, one first-seen bound per CID across both paths |
| TransactionManager::SetLegacyMintDeferWindow | count() < 0 warn-and-ignore | mirrored negative-window guard | ✓ WIRED | TM.cpp:4644-4648; zero stays legal |
| (regression) FilterTransaction v1 branch / three MINT-03 ingresses / equivocation guard / config wiring | — | — | ✓ WIRED | All re-anchored post-gap-fix: TM.cpp:824/:2025/:3808, Consensus.cpp:3538-3541, prior key links unchanged |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| --- | --- | --- | --- |
| MINT-04 full matrix incl. both new bypass closures + compat twins | `./test_bin/mint_evidence_binding_test` | 17/17 PASSED (1 ms) | ✓ PASS |
| CR-01/CR-02 new cases by name | `--gtest_filter='*MultiOutput*:*Bridgeless*:*EmptyTopic0*'` | 4/4 OK | ✓ PASS |
| TRUST-01 unit matrix incl. 4 new grandfather cases | `./test_bin/consensus_pending_lifecycle_test` | 41/41 PASSED (4.3 s) | ✓ PASS |
| WR-01/CR-03 new cases by name | `--gtest_filter='*Crafted*:*ConflictingLowerHash*:*GrandfatheredLoadFailure*'` | 4/4 OK | ✓ PASS |
| WR-02 guard case by name | `--gtest_filter='*NegativeLegacyMintDeferWindowIgnored*'` | OK (1.15 s) | ✓ PASS |
| Multi-node real-socket TRUST-01 trio under content-bound probe | `./test_bin/multi_node_finality_fault_test --gtest_filter='...Forged...:...Restarted...:...FreshNodeCatchUp...'` | 3/3 PASSED (20 s) | ✓ PASS |
| MINT-03 real-socket compat under changed probe | `ctest -R '^(multi_account_test\|migration_sync_test)$'` | 2/2 Passed (399 s) | ✓ PASS |
| Adjacent full pipeline (PCIV guards vs 11-node Foundry) | `./test_bin/bridge_race_fault_rpc_test` | 2/2 PASSED (81 s) | ✓ PASS |
| Regression battery (MINT-02/MINT-03/TRUST-02/D-03/strict loader) | `ctest -R "public_chain_input_validator\|bounded_defer\|validator_registry_verified_load\|legacy_mint_admission\|legacy_mint_sync_admission\|validator_admission_equivocation"` | 7/7 targets Passed | ✓ PASS |

**Tests run by this verifier:** 12 targets / 100+ cases, all passing (every gap-closure case additionally run by name). **Accepted from recorded evidence:** none required for the goal decision — the multi-node suites the initial verification skipped (TRUST-01 trio, multi_account, migration) were re-run directly this time.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| --- | --- | --- | --- | --- |
| TRUST-01 | 21-01, 21-03, 21-04, 21-09 | Registry-by-CID authorization verified; forged self-certification rejected; sync/history unbroken | ✓ SATISFIED | Strict path + content-bound probe + defer-bounded grandfathered stall; forged CID rejected with and without durable record; restart/catch-up compat green at unit and real-socket level. REQUIREMENTS.md Complete mark now supported. |
| TRUST-02 | 21-03, 21-07 | Admission growth bounds + equivocation audited, findings documented | ✓ SATISFIED | Audit doc + cap enforcement + equivocation guard; suite re-run green; untouched by gap plans. |
| MINT-02 | 21-02 | Fail-closed on empty/local chain ids + empty source refs; per-type consensus rules | ✓ SATISFIED | Fail-closed branches intact post-21-08; 17/17 green. |
| MINT-03 | 21-03, 21-05, 21-06 | Legacy-v1 reception disabled via admission gating only | ✓ SATISFIED | Three ingresses intact; unit suites green; multi_account + migration real-socket compat re-run green; WR-02 guard closes the misconfiguration data-loss warning. |
| MINT-04 | 21-02, 21-08 | Burn receipt facts bound to mint outputs; honest RPCs cannot authorize mismatch | ✓ SATISFIED | Single-output identity binding + multi-output closure + bridge-less-endpoint closure; all adversarial and compat cases green. REQUIREMENTS.md Complete mark now supported. |

Orphaned requirements: none — all five phase IDs appear in plan frontmatters (21-01..21-09) and REQUIREMENTS.md (:102-106).

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| --- | --- | --- | --- | --- |
| src/transaction/TransactionManager.cpp | 1548, 3282, 3675, 3906 | Pre-existing TODO markers in legacy regions | ℹ️ Info | Predate phase 21 (git log -S since Oct 1 finds no touching commit; not in gap-plan diff regions — the entire 21-09 TM.cpp diff is the 11-line WR-02 guard). Not blockers. |
| src/blockchain/Consensus.cpp | 3839 | Pre-existing TODO (reputation) | ℹ️ Info | Dated 2026-09-07 (e74bd8696); outside changed regions. |
| src/account/PublicChainInputValidator.cpp | — | Pre-existing cognitive-complexity finding raised 52 -> 56 by the mandated inline guard | ⚠️ Warning (documented) | 21-08 recorded this as the sole tidy delta; guard placement mirrors the topic-mismatch precedent — accepted, no NEW finding class. |
| Residuals WR-03/WR-04/WR-05/IN-01..04 | — | Documented Phase 25 residuals (batch preflight abort, retryable-forever corrupt delta class, local-only equivocation marker, expired-defer capacity hygiene) | ℹ️ Info | Unchanged from initial verification; none defeats a phase truth; named in the audit conclusion and 21-09 summary as Phase 25 scope. |

No TBD/FIXME/XXX markers in any changed region; no stub implementations; no console-only handlers.

### Human Verification Required

None — all gap closures are code-confirmed and reproduced by this verifier through direct test execution (unit + real-socket), including the multi-node restart/catch-up compat under the content-bound probe.

### Gaps Summary

No gaps remain. The four code-confirmed bypasses from the initial verification are closed fail-closed in the current codebase, each with an adversarial test naming the attacker authority that this verifier executed by name, plus compat twins proving legitimate behavior is unchanged:

1. **MINT-04 / CR-01:** the burn-fact binding now requires exactly one output before any fact comparison; the multi-output attack (output[0] = attested burn, output[1] extra value) is rejected with all evidence discarded, while single-output confirmation is byte-unchanged.
2. **MINT-04 / CR-02:** bridge-less and topic0-less endpoints earn zero confirmation weight on evidence-required mint types (scoped guard, per-endpoint continue), while non-mint public-chain types keep bridge-optional confirmation (pinned by a dedicated compat twin) and the full 11-node Foundry pipeline is undisturbed.
3. **TRUST-01 / WR-01:** the grandfather probe binds by content — the durable record must parse and hash-equal the candidate's serialization; a newly crafted certificate at an already-finalized slot (forged registry CID, disjoint signer set, even a deliberately engineered lower hash) takes the strict verified path and is Rejected without overwriting the stored winner.
4. **TRUST-01 / CR-03:** the grandfathered registry-load failure is bounded by the same D-03 tracker and registry-CID key as the strict path — in-window never-memoized Stalled, past-window deterministic Reject — so no certificate shape can stall a CRDT delta forever; the genuine restart/catch-up compat survives at both unit and real-socket level (re-run by this verifier).

The previously verified three (TRUST-02, MINT-02, MINT-03) are intact after the gap fixes: their code anchors are unchanged, their suites re-ran green, and the two real-socket compat suites most exposed to the probe change (multi_account, migration) passed. The adjacent WR-02 warning is also closed. The phase goal — the certificate trust anchor holding on a public network with fail-closed mint/escrow authorization and no orphaned legitimate state — is achieved. All five REQUIREMENTS.md Complete marks for this phase are now supported by verified evidence.

---

_Verified: 2026-10-21T17:05:00Z_
_Verifier: Claude (gsd-verifier)_
