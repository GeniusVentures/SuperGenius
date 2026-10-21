---
phase: 21-consensus-trust-mint-authorization
verified: 2026-10-21T00:00:00Z
status: gaps_found
score: 3/5 must-haves verified
overrides_applied: 0
gaps:
  - truth: "A mint whose token ID, amount, or destination mismatches the burn receipt is rejected even when every RPC endpoint honestly reports the receipt (MINT-04)"
    status: failed
    reason: "CR-01 (code-confirmed): the burn-fact binding compares only the FIRST output of a mint-v2. GetAmount/GetTokenID/GetPrimaryDestinationAddress all return utxo_params_.second.front() (MintTransactionV2.cpp:145-170), FromNew accepts arbitrarily many outputs (only rejects empty, :134), and PublicChainInputValidator::ValidateUTXOParameters requires only !outputs.empty() (PCIV.cpp:188). A submitter crafts output[0] = exactly the attested burn plus output[1..n] carrying extra value: every MINT-04 check passes on output[0] while the mint creates N outputs. No ingress constrains mint-v2 to a single output. Adversarial tests (mint_evidence_binding_test 13/13 green) only construct single-output mints, so the bypass is untested and open. No later milestone phase covers the mint output-set binding (Phase 24 = confirmation depth/corroboration; Phase 26 = escrow entitlement)."
    artifacts:
      - path: "src/account/PublicChainInputValidator.cpp"
        issue: "DecodeAndBindBurnFacts (:103-173) compares decoded facts against front()-only accessors; no output-count or output-set-sum constraint anywhere on the evidence path"
      - path: "src/account/MintTransactionV2.cpp"
        issue: "GetAmount/GetTokenID/GetPrimaryDestinationAddress (:145-170) read utxo_params_.second.front() only while FromNew (:118-137) accepts N outputs"
    missing:
      - "For evidence-required mint types, require exactly one output at binding/admission, or bind the full output set (sum(outputs.amount) == facts.amount AND every destination == facts.destination AND every token == facts.token)"
      - "Adversarial test: multi-output mint whose output[0] matches the receipt and output[1] carries extra value must be rejected"
  - truth: "RPC endpoint confirmation of a mint is conditioned on the bridge burn evidence (MINT-04 support condition)"
    status: failed
    reason: "CR-02 (code-confirmed): the entire log-matching block — bridge-contract address check, accepted-topic0 check, and the MINT-04 DecodeAndBindBurnFacts call — is nested under `if ( !ep.bridge_contract_address.empty() )` (PCIV.cpp:580-637). An endpoint configured with an empty bridge contract address skips all of it and is credited full consensus_weight at :640 on a receipt that merely exists with status == 0x1. With sufficient weight from such endpoints, a mint-v2 citing any successful public-chain tx hash gathers valid evidence. Reachable via GeniusNode::ConfigureRpcEndpoint. The MINT-02 fixes close the identity fail-opens but keep this per-endpoint fail-open. Phase 24 BRIDGE-02 (content corroboration, fabricated-quorum test) is adjacent but does not explicitly name this condition — kept as a gap, not deferred."
    artifacts:
      - path: "src/account/PublicChainInputValidator.cpp"
        issue: "Endpoint without bridge_contract_address falls through the guarded block to successful_weight += consensus_weight (:640) without any log/burn-fact check"
    missing:
      - "For RequiresPublicChainEvidence transactions, an endpoint with empty bridge_contract_address or empty accepted_topic0_hashes must not confirm (continue, no weight credit)"
      - "Adversarial test: quorum weight assembled from bridge-less endpoints confirming an unrelated receipt must fail"
  - truth: "A certificate path loading a validator registry by CID rejects a forged/self-certified registry (TRUST-01) — attack still viable through the grandfather path (WR-01) and the grandfathered stall is unbounded (CR-03)"
    status: failed
    reason: "Strict path verified working (Consensus.cpp:3694-3703, permission_denied -> Reject; multi-node adversarial test exists and unit pairs green 37/37). But: (CR-03, code-confirmed) the grandfathered path (:3673-3688) returns Check::Stalled on ANY registry-load error with NO defer bound, while the strict path is bounded (:3714) — any gossip peer can craft a certificate at a slot that already has a durable record referencing a nonexistent registry CID and stall its whole CRDT delta forever, re-opening the exact DoS the D-03 tracker closed. (WR-01, code-confirmed) the grandfather probe (:3650-3658) grants grandfathering by slot-record EXISTENCE only — the stored bytes are never compared to the candidate. An attacker reusing their own already-finalized slot (account:nonce is attacker-controlled) with a newly crafted certificate referencing a FORGED registry skips chain verification entirely, tallies against the unverified registry (:3739 uses the LoadUnverifiedRegistryByCid result), and with disjoint signers the equivocation guard does not fire and the lower-hash rule (:3550) can accept it over the stored winner. The phase goal's 'trust anchor holds on a public network' is therefore not achieved as stated."
    artifacts:
      - path: "src/blockchain/Consensus.cpp"
        issue: "Grandfather probe :3650-3658 is existence-only (no content binding); grandfathered load failure :3679-3687 returns Stalled with no authorization_defer_tracker_ bound"
    missing:
      - "Route the grandfathered registry-load failure through authorization_defer_tracker_ (expired -> deterministic Reject) so the stall is bounded exactly like the strict path"
      - "Bind the probe by content: parse the durable record and require it to be this certificate (serialized-hash equality) before skipping strict authorization, or run the strict verified load on the grandfather path too"
      - "Adversarial test: newly crafted certificate at an already-finalized slot referencing a forged registry CID must be rejected; conflicting-certificate-at-finalized-slot overwrite must not succeed via lower-hash ordering"
  - truth: "MINT-04 adversarial evidence covers the attacks a submitter can actually mount (assessment bar: the attack described is actually rejected)"
    status: partial
    reason: "Grouped with the two MINT-04 gaps above: single-output, bridge-configured-endpoint mismatches ARE rejected (13/13 tests), but the multi-output (CR-01) and bridge-less-endpoint (CR-02) attacks remain viable, so the requirement's close is not fail-closed against the reviewed attack set."
    artifacts:
      - path: "test/src/account/mint_evidence_binding_test.cpp"
        issue: "All cases construct single-output mints with bridge-configured endpoints; no case exercises CR-01/CR-02 shapes"
    missing:
      - "Test coverage for the two bypass shapes once fixed"
---

# Phase 21: Consensus Trust & Mint Authorization Verification Report

**Phase Goal:** The certificate trust anchor holds on a public network — a registry loaded by CID is authorization-verified with a grandfather rule that keeps historical finality re-validating — and every mint/escrow authorization gap from the assessment's highest-credibility findings (#1, #3, #4, #5, #14) closes fail-closed without orphaning legitimate state.
**Verified:** 2026-10-21
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | TRUST-01: certificate path rejects forged/self-certified registry CID; pre-upgrade certs re-validate after restart; catch-up sync unchanged | ✗ FAILED | Strict path works and is tested (Consensus.cpp:3694-3703; consensus_pending_lifecycle 37/37 incl. forged-reject, grandfather twin, deadline; multi-node TEST_Fs exist at multi_node_finality_fault_test.cpp:2746/2817/2881). BUT the grandfather path re-opens the boundary: probe is existence-only with no content binding (:3650-3658, WR-01) and its registry-load failure is Stalled forever with no defer bound (:3679-3687, CR-03). Both independently confirmed in code. The forged-registry attack remains viable for any slot with an existing durable record; the unbounded-stall DoS the D-03 tracker closed is re-opened on the grandfathered path. |
| 2 | TRUST-02: admission growth-bounds and cross-certificate equivocation audit documented with findings and dispositions | ✓ VERIFIED | .planning/notes/2026-10-validator-admission-equivocation-audit.md exists (68 lines, F1-F4 findings, 5 Disposition entries, file:line evidence, named attacker authorities, Phase 25 residuals named). Cap enforcement proven through the member path (validator_admission_equivocation_test 5/5: over-cap truncates at exactly cap, wired 3 overrides default 10, sub-cap inserts all). Equivocation guard live in FilterCertificate's conflict branch (Consensus.cpp:3517-3547) with dual-signature proof, durable marker under sibling prefix, and disjoint-signer compat twin. |
| 3 | MINT-02: evidence validation fails closed on empty/local chain ids and empty source references; native vs bridge mint follow separated per-type consensus rules | ✓ VERIFIED | PCIV.cpp:462-479 (empty source_reference on mint types -> fail closed), :483-494 (local/scoped-local chain id on mint types -> fail closed), RequiresPublicChainEvidence covers exactly "mint"/"mint-v2" (:70), non-mint local bypass preserved (:495-500). Consensus-side per-type separation present (IsPublicChainMintChainId Consensus.cpp:57-70 -> DecideQuorum bridge_mint split with D-06 slot model, :2645-2662). mint_evidence_binding_test 13/13 green incl. all four identity adversarial cases + compat twin. Caveat (warning): CR-02 keeps a per-endpoint fail-open in the same function — it does not let an empty-source/local-chain mint through, so this truth holds, but see gap 2. |
| 4 | MINT-03: newly submitted self-signed legacy-v1 mint rejected at every ingress; pre-cutover outputs spendable; fresh node syncs READY; restart recovers; admission gating only | ✓ VERIFIED | All three ingresses in code: consensus ingress (TransactionConsensusHandler.cpp:824), submission preflight with named errc before any CRDT Put (TransactionManager.cpp:2025-2031), sync admission certificate-conditional Stall/Reject with D-03 bound (TransactionManager.cpp:3808-3846). One-home predicate IsValueCarryingLegacyMint (TM.cpp:4626-4634, zero-amount exempt). legacy_mint_admission_test 6/6 + legacy_mint_sync_admission_test 7/7 green (stall resolves on cert arrival, deadline rejects with tombstone, certified history admits first-pass, migration zero-stall). Parser map untouched (deserialization open). Review's verified-areas section independently calls the sync gate "soundly bound". Multi-node compat suites (multi_account, migration, multi_node_finality_fault) reported green with commits present; not re-run by this verifier (real-socket, serialized, minutes-long). |
| 5 | MINT-04: mint whose token/amount/destination mismatches the burn receipt rejected even when every RPC endpoint honestly reports the receipt | ✗ FAILED | Binding exists and its tests pass (13/13) — but only for the single-output, bridge-configured shape. CR-01: all three accessors read front() only (MintTransactionV2.cpp:145-170); deserialization and PCIV:188 accept N outputs; a multi-output mint passes every check on output[0] while minting N. CR-02: endpoints without bridge_contract_address skip the entire defense (PCIV.cpp:580) and confirm on receipt existence (:640). Both bypasses code-confirmed; neither covered by tests; no later phase covers the output-set binding. |

**Score:** 3/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| -------- | -------- | ------ | ------- |
| `src/blockchain/ValidatorRegistry.hpp/.cpp` | Verified+unverified loaders, chain-walk helper, memo cap 64, genesis check | ✓ VERIFIED | LoadVerifiedRegistryByCid (:294), LoadUnverifiedRegistryByCid (:311), VerifiedRegistryMemo (:375), VerifyRegistryChainClassified (:435), kMaxVerifiedRegistryMemoEntries (:69), VerifyGenesisAuthoritySignature (:724). Deprecated alias deleted — 0 bare LoadRegistryByCid( refs in src/+test/. Wired: all Consensus.cpp loads go through the verified loader (cache fill :2712, tally :2612, slot-winner :1591, HandleProposal :2965) except the sanctioned grandfather/verification-internal raw uses. |
| `src/blockchain/Consensus.hpp/.cpp` | Grandfather probe, classified verdicts, verified-only cache cap 64, defer window, equivocation guard | ✓ VERIFIED (with gaps 3) | All present and wired (probe :3650, strict verdicts :3694-3728, cache cap 64 + deque eviction :1322/:2719-2731, SetAuthorizationDeferWindow :688 with negative guard, equivocation :1336/:1384/:3538-3546). Grandfather-path defects are behavioral gaps, not missing artifacts. |
| `src/account/PublicChainInputValidator.hpp/.cpp` | Per-type fail-closed branches, receipt decode+bind, decoded_ carriers | ✓ VERIFIED (with gaps 1-2) | RequiresPublicChainEvidence (:70), both fail-closed branches (:462-494), DecodeAndBindBurnFacts (:103-173) via eth::abi::decode_log + BridgeRelayer::ParseBurnEventValues, decoded_token_id/decoded_amount/decoded_recipient carried (:633-635). No GetSlotID re-parse in PCIV. Multi-output and bridge-less-endpoint coverage missing (gaps). |
| `src/account/MintTransactionV2.hpp/.cpp` | GetTokenID/GetPrimaryDestinationAddress accessors | ✓ VERIFIED (front()-only — root of gap 1) | :145-170. |
| `src/base/bounded_defer.hpp/.cpp` + test | Generic bounded defer, FIFO cap 4096, injectable clock | ✓ VERIFIED | 193-line test, 8/8 green; mechanism consumer-agnostic; review's verified-areas section independently confirms correctness. |
| `src/account/GeniusNode.cpp` | Config keys, cap/window wiring, genesis fingerprint pin | ✓ VERIFIED | authorization_defer_window_sec/max_new_validators_per_update read in LoadNetworkConfig; SetMaxNewValidatorsPerUpdate (:860), SetAuthorizationDeferWindow (:868), SetLegacyMintDeferWindow (:1202); genesis_manifest_fingerprint parse (:528) + enforcement before controller construction (:953-968, FATAL_TRUST_MISMATCH; unpinned warn :975). |
| `.planning/notes/2026-10-validator-admission-equivocation-audit.md` | Findings + dispositions (min 60 lines) | ✓ VERIFIED | 68 lines, F1-F4, 5 dispositions, conclusion names Phase 25 residuals. |
| Test targets (7 new/extended) | Adversarial+compat pairs | ✓ VERIFIED | All exist, registered, binaries current vs sources, all run green by this verifier (see spot-checks). |

### Key Link Verification

| From | To | Via | Status | Details |
| ---- | --- | --- | ------ | ------- |
| ValidateCertificate strict path | LoadVerifiedRegistryByCid | classified verdict mapping | ✓ WIRED | :3694-3728; permission_denied->Reject, try_again->Stalled, defer-expired->Reject |
| ValidateCertificate grandfather path | LoadUnverifiedRegistryByCid | durable-record probe | ⚠️ WIRED (unbounded, unbound-by-content) | :3679-3687 — gaps 3 |
| LoadRegistryByCidCached | LoadVerifiedRegistryByCid | verified-only fill, cap 64 | ✓ WIRED | :2712-2731; failures uncached; single-oldest eviction; no clear() |
| FilterTransaction v1 branch | GetCertificateBySlot + CertificateMatchesTransaction | producer-certificate probe | ✓ WIRED | TM.cpp:3808-3846; Clear-on-admit; mismatch defers fail-closed |
| CheckTransactionTypeRules / SendTransactionItem | IsValueCarryingLegacyMint | one-home predicate | ✓ WIRED | TCH:824, TM:2025-2031, TM:3808 — three sites, one home, zero copies |
| DecodeAndBindBurnFacts | abi_decoder + mint accessors | fact comparison | ⚠️ WIRED (front()-only) | PCIV:103-173 — gap 1 |
| Conflict branch | ProveCrossCertificateEquivocation + RecordEquivocationMarker | durable ledger conventions | ✓ WIRED | Consensus.cpp:3538-3546, sibling prefix per audit F4 |
| GeniusNode | Set*DeferWindow / SetMaxNewValidatorsPerUpdate | config wiring | ✓ WIRED | :860/:868/:1202 |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| Verified loader rejects forged/cyclic/non-quorum chains; memo cap eviction | `./validator_registry_verified_load_test` | 10/10 PASSED (1 ms) | ✓ PASS |
| MINT-02/MINT-04 identities + fact mismatches rejected; compat twins | `./mint_evidence_binding_test` | 13/13 PASSED (3 ms) | ✓ PASS |
| Bounded defer: first-seen anchoring, oldest-first eviction | `./bounded_defer_test` | 8/8 PASSED (2 ms) | ✓ PASS |
| v1 mint rejected at consensus ingress + preflight (errc by value, no CRDT Put) | `./legacy_mint_admission_test` | 6/6 PASSED (248 ms) | ✓ PASS |
| Sync admission: stall resolves, deadline rejects w/ tombstone, certified admits | `./legacy_mint_sync_admission_test` | 7/7 PASSED (1.4 s) | ✓ PASS |
| Cap enforcement (default + wired override + sub-cap) + equivocation reject + disjoint-signer compat | `./validator_admission_equivocation_test` | 5/5 PASSED (210 ms) | ✓ PASS |
| TRUST-01 unit pairs: forged reject + no durable record, grandfather twin, deadline reject, stall-not-memoized | `./consensus_pending_lifecycle_test` | 37/37 PASSED (4.0 s) | ✓ PASS |
| Multi-node real-socket suites (TRUST-01 trio, multi_account, migration) | not re-run | real sockets, RUN_SERIAL, minutes-long | ? SKIP — summaries + commits evidence green; binaries and test code verified present |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| ----------- | ----------- | ----------- | ------ | -------- |
| TRUST-01 | 21-01, 21-03, 21-04 | Registry-by-CID authorization verified; forged self-certification rejected; sync/history unbroken | ✗ BLOCKED | Strict path + tests verified; grandfather-path bypasses (CR-03 unbounded stall, WR-01 existence-only probe) leave the attack viable on finalized slots. REQUIREMENTS.md "Complete" mark not supported for the public-network attack surface. |
| TRUST-02 | 21-03, 21-07 | Admission growth bounds + equivocation audited, findings documented | ✓ SATISFIED | Audit doc + cap enforcement + equivocation guard, all adversarially/compat tested. WR-05 (local-only marker) is a documented-scope warning, not a requirement failure. |
| MINT-02 | 21-02 | Fail-closed on empty/local chain ids + empty source refs; per-type consensus rules | ✓ SATISFIED | Code + 13/13 tests; per-type separation present at evidence and consensus layers. CR-02 caveat noted under gap 2 (does not reopen the named identities). |
| MINT-03 | 21-03, 21-05, 21-06 | Legacy-v1 reception disabled via admission gating only | ✓ SATISFIED | Three ingresses fail-closed; compat twins (zero-value, certified history, migration zero-stall, spendability chain); deserialization untouched. Warnings WR-02/WR-03 do not defeat the security truth. |
| MINT-04 | 21-02 | Burn receipt facts bound to mint outputs; honest RPCs cannot authorize mismatch | ✗ BLOCKED | CR-01 multi-output bypass + CR-02 bridge-less-endpoint bypass, both code-confirmed and untested. |

Orphaned requirements: none — all five phase IDs appear in plan frontmatters and REQUIREMENTS.md.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| src/transaction/TransactionManager.cpp | 4636-4639 | SetLegacyMintDeferWindow lacks the negative-window guard its consensus twin has (Consensus.cpp:690-694) | ⚠️ Warning (WR-02) | Negative config value -> immediate reject-on-first-sight for legitimate late-cert v1 history (data loss via misconfiguration) |
| src/transaction/TransactionManager.cpp | 2025-2032 | Preflight aborts whole submission batch on first v1 mint | ⚠️ Warning (WR-03) | One poisoned entry blocks valid batch members |
| src/blockchain/ValidatorRegistry.cpp | 806-818, 916-921 | Corrupt local registry delta classified retryable-forever (no distinct error class) | ⚠️ Warning (WR-04) | Permanent local corruption indistinguishable from ordering; unbounded pending/stall at batch sites |
| src/blockchain/Consensus.cpp | 1384-1448 | Equivocation marker local-only, no protocol consequence | ⚠️ Warning (WR-05) | Signer pays nothing on non-observing nodes; acceptable for 21-07 "cheap guards" scope, header oversells durability |
| src/transaction/TransactionManager.cpp | 3818-3828 | Expired defer entries never Clear()ed | ℹ️ Info (IN-01) | Capacity hygiene only |
| test/src/transaction_sync/legacy_mint_sync_admission_test.cpp | 368 | sleep_for(1100ms) in deadline test | ℹ️ Info | Plan itself specified "1 s window + real elapse"; house wait_condition macros used elsewhere |
| Changed src files (7 scanned) | — | No TBD/FIXME/XXX/TODO/PLACEHOLDER markers; no stub implementations; no console-only handlers | ✓ Clean | — |

### Human Verification Required

None required for the gap decision — all failing evidence is code-confirmed and reproducible by grep/read. Optional (not blocking): a serialized re-run of the multi-node real-socket suites (`multi_node_finality_fault_test`, `multi_account_test`, `migration_sync_test`) to re-confirm the compat claims this verifier skipped.

### Gaps Summary

The phase did real, well-tested work: the verified registry chain-walk, the MINT-03 three-ingress closure, the MINT-02 identity fail-close, the audit + cap + equivocation guard, and all compat twins are present, wired, and green (76 unit-test cases re-run by this verifier across 7 targets, all passing). But this is a security-remediation phase judged on whether the described attacks are actually rejected, and two of the five requirement boundaries have code-confirmed bypasses surviving from the phase's own code review:

1. **MINT-04 does not close fail-closed** (2 blockers): a multi-output mint-v2 inflates past the attested burn because every fact comparison reads the first output only (CR-01), and an endpoint without a configured bridge contract confirms a mint on receipt existence alone (CR-02). The 13 passing binding tests only cover the single-output, bridge-configured shape.
2. **TRUST-01's anchor does not hold on a public network** (2 blockers, same root cause — the grandfather path): the D-01 probe binds by slot-record existence with no content check, so a newly crafted certificate at any already-finalized slot skips chain verification and can tally against a forged registry (WR-01); and the grandfathered registry-load failure returns Stalled with no defer bound, re-opening the unbounded whole-delta stall DoS the D-03 tracker was built to close (CR-03).

MINT-02 and MINT-03 are genuinely closed (their named attacks are rejected and compat survives); TRUST-02's deliverable is satisfied as scoped. Fixes are small and localized: an output-count/set constraint at the binding, a bridge-less-endpoint skip for evidence-required types, a defer bound on the grandfathered load failure, and content binding (hash equality) in the grandfather probe — each with an adversarial test naming the attacker authority.

---

_Verified: 2026-10-21_
_Verifier: Claude (gsd-verifier)_
