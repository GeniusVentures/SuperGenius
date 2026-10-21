---
phase: 21-consensus-trust-mint-authorization
plan: "08"
subsystem: auth
tags: [mint-authorization, gap-closure, fail-closed, evidence-binding, bridge, multi-output, quorum]

# Dependency graph
requires:
  - phase: 21-consensus-trust-mint-authorization/21-02
    provides: DecodeAndBindBurnFacts binding home, RequiresPublicChainEvidence taxonomy, the 13-case matrix these closures extend
provides:
  - Exactly-one-output constraint at burn-fact binding (CR-01 closure): a mint-v2 whose output set is not exactly the attested single output cannot gather any confirming evidence
  - Bridge-less endpoint no-confirm guard (CR-02 closure): zero confirmation weight from endpoints lacking bridge contract or accepted topic0 set, scoped to evidence-required mint types
  - MINT-04 legitimacy: the requirement's Complete mark in REQUIREMENTS.md is now backed by tests covering the reviewed attack set
affects: [24-bridge-ingestion-safety, 25-verification-spine]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Output-set authorization lives at the evidence boundary, never in the deserializer (D-02 admission-gating discipline): DeSerializeByteVector stays N-output-open; DecodeAndBindBurnFacts is where burn facts authorize the output set"
    - "Scoped no-confirm guard mirrors the topic-mismatch continue precedent: an endpoint that cannot check bridge evidence earns no weight and no slot while quorum evaluation continues over the remaining endpoints"

key-files:
  created: []
  modified:
    - src/account/PublicChainInputValidator.cpp
    - test/src/account/mint_evidence_binding_test.cpp

key-decisions:
  - "CR-01 closes at DecodeAndBindBurnFacts with an exactly-one-output guard before the topic0 selection: the front()-backed accessors are only the full output set when there is exactly one output; nullopt discards ALL evidence via the existing caller branch (reverted-receipt precedent)"
  - "CR-02 guard covers BOTH empty bridge_contract_address and empty accepted_topic0_hashes, scoped by RequiresPublicChainEvidence so non-mint public-chain types keep bridge-optional confirmation byte-identically (pinned by a dedicated compat twin)"
  - "The multi-output test fixture uses the attacker's actual route (SGTransaction::MintTxV2 proto -> SerializeToString -> DeSerializeByteVector) because the MintTransactionV2 constructor is private and New is single-output by design - no multi-output factory was added to production code"
  - "MakeValidator refactored into MakeValidatorWithBridgeBinding (one home for the endpoint-construction shape; MakeValidator delegates) so bridge-less/topic-less endpoint fixtures reuse the exact same validator wiring"

patterns-established:
  - "Pre/post clang-tidy comparison by materializing the pre-plan file version in place against the filtered single-arch DB - proves zero NEW findings rather than eyeballing"

requirements-completed: [MINT-04]

# Metrics
duration: 17min
completed: 2026-10-21
---

# Phase 21 Plan 08: MINT-04 Gap Closure (CR-01 multi-output + CR-02 bridge-less endpoint) Summary

**Both code-confirmed MINT-04 bypasses from 21-VERIFICATION.md gaps 1/2/4 are closed fail-closed in PublicChainInputValidator — a multi-output mint-v2 whose output[0] matches the burn cannot gather any evidence, and quorum weight can no longer be assembled from endpoints that never check bridge logs — restoring MINT-04's Complete mark legitimately**

## Performance

- **Duration:** 17 min (2026-10-21T18:24:49Z → 2026-10-21T18:41:42Z)
- **Tasks:** 2 (both TDD: RED → GREEN; plus one tidy-hygiene commit)
- **Files modified:** 2 (0 created)

## Accomplishments

- **CR-01 (T-21-0801, gap 1):** `DecodeAndBindBurnFacts` now rejects any mint-v2 whose `GetUTXOParameters().second.size() != 1` at the top of the function — before the topic0 selection and the three fact comparisons — with the named-rule log `receipt/mint fact binding failed: multi-output mint tx={}` (PreviewValue-truncated hash only). The nullopt flows into the existing caller branch that discards ALL gathered evidence, so no slot can certify a multi-output mint. Deserialization stays N-output-open (T-21-0804 accept: D-02 admission-gating discipline — the parser is not the authorization point).
- **CR-02 (T-21-0802, gap 2):** in `GatherVerificationEvidence`'s endpoint loop, immediately before the bridge block, a guard scoped by `RequiresPublicChainEvidence( tx )` continues past any endpoint with an empty `bridge_contract_address` OR empty `accepted_topic0_hashes` — warn-level `endpoint lacks bridge binding for chain_id/url — not confirming` — earning no weight and no slot while quorum evaluation continues over the remaining endpoints (the :600-617 topic-mismatch continue precedent, T-21-0805 accept). Non-mint public-chain types keep today's bridge-optional confirmation byte-identically.
- **Adversarial tests (VER-01 comments naming attacker authority, expected rejection, and recorded pre-fix behavior):** `MultiOutputMintFirstOutputMatchesStillRejected` (two-output mint via the proto route — output[0] exactly the attested burn, output[1] extra value; pre-fix valid == true 75/75), `BridgelessEndpointQuorumFails` (three 25-weight bridge-less endpoints honestly serving an unrelated status-0x1 receipt; pre-fix valid == true 75/75 with every defense skipped), `EmptyTopic0SetDoesNotConfirm` (bridge contract set, topic0 set empty — pins both empty binding fields).
- **Compat twins:** `MatchingV2FactsValidate` (single-output v2) and all 12 other pre-existing cases stay green; new `BridgelessEndpointStillConfirmsNonMintPublicType` pins the CR-02 scoping (non-mint public-chain type confirmed by bridge-less endpoints, unchanged before and after).

## Task Commits

1. **Task 1: Require exactly one output at burn-fact binding (CR-01, tdd)** — `e357a4c4c` (test: RED — proto-route two-output mint with fact-matching output[0] accepted today, valid == true) + `2132e0b9c` (feat: GREEN — output-count guard before the fact comparisons)
2. **Task 2: Bridge-less endpoints do not confirm evidence-required mints (CR-02, tdd)** — `2ad50c9f4` (test: RED — bridge-less quorum credits 75/75 on an unrelated receipt today; + topic0-empty pin + scoping compat twin + MakeValidatorWithBridgeBinding refactor) + `6b67302de` (feat: GREEN — scoped no-confirm guard before the bridge block)
3. **Hygiene:** `8aa4759ab` (refactor — const-ref topic0_hashes parameter, const non_mint_tx; no behavior change, matrix stays 17/17)

**Plan metadata:** (this commit)

## Deviations from Plan

### Auto-fixed Issues

**1. [Verification gate] clang-tidy hygiene on the new test helpers**
- **Found during:** Task 2 repo gates (pre/post tidy comparison)
- **Issue:** The new `MakeValidatorWithBridgeBinding` parameter was flagged `performance-unnecessary-value-param` and `non_mint_tx` `misc-const-correctness`.
- **Fix:** const-reference parameter; const declaration. Committed separately (`8aa4759ab`) so the RED/GREEN pairs stay pure.
- **Files modified:** test/src/account/mint_evidence_binding_test.cpp

**2. [Plan-text note] CR-02 key_link single-line regex cannot match under the 120-column format**
- **Found during:** Task 2 acceptance greps
- **Issue:** The plan frontmatter key_link pattern `RequiresPublicChainEvidence\( tx \).*bridge_contract_address\.empty` assumes the guard condition on one line; the full condition is 132 characters at loop indent, so clang-format wraps it after `&&`.
- **Fix:** none needed — the task's binding acceptance grep (`lacks bridge binding`) matches; guard structure, scoping, and placement are exactly as the plan's action specified (verified: guard at :598-599 before the bridge block at :610, both empty fields covered).

---

**Total deviations:** 1 auto-fixed + 1 plan-text note
**Impact on plan:** None on scope or semantics.

## Issues Encountered

- **Pre-existing tidy finding incremented:** `GatherVerificationEvidence` cognitive complexity was already over threshold pre-plan (52 > 40, flagged in 21-02 as pre-existing noise); the plan-mandated inline guard raises it to 56 (+4). No NEW finding appears — PCIV.cpp has the identical 21 findings before and after this plan. Not fixed: extracting the guard into a helper would contradict the plan's inline placement and the topic-mismatch precedent it mirrors.
- **Test-file tidy deltas kept by convention:** +8 `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` from boost::json `object::operator[]` in the new plain-receipt builder — the sibling `BuildBridgeBurnReceiptJson` carries 13 identical pre-existing findings (the file's established builder idiom); +2 lowercase `u` literal suffixes matching the repo-wide test convention (`grep ', 2u )' test/src` — many hits, zero uppercase uses).
- The clang-tidy comparison required materializing the pre-plan file (commit 9de167e99) in place against the filtered single-arch DB — a copied-outside-the-tree file silently produces wrong results (the compile command still references the repo path).

## Verification

- `ctest -R "mint_evidence_binding|public_chain_input_validator" --test-dir build/OSX/Release` — 2/2 targets passed; full matrix 17/17 cases (13 pre-existing + 4 new).
- `bridge_race_fault_rpc_test` (Foundry full 11-node pipeline, insurance beyond the plan) — Passed (82.5 s): all its endpoints carry both bridge fields and its mints are single-output, so the stricter guards do not disturb it.
- `clang-format --dry-run --Werror` clean on both changed files.
- clang-tidy (filtered single-arch DB, LLVM 22.1.8, pre/post comparison): PCIV.cpp — 21 findings before, the same 21 after (sole delta: pre-existing cognitive-complexity value 52→56); test file — both new non-noise-class findings fixed in `8aa4759ab`, remaining deltas are the documented convention/idiom noise classes above. Zero new Required-group (bugprone/CERT/core-guidelines-the-code-follows) findings in changed regions.
- Acceptance greps: `multi-output` matches the named-rule log at PCIV.cpp:115 with the guard at :113 BEFORE the fact comparisons at :167/:173/:179; `lacks bridge binding` matches at :602 with the scoped guard at :598-599 before the bridge block at :610.
- VERIFICATION.md gap bullets satisfied: gap-1 missing-bullets (multi-output adversarial test + rejection at binding), gap-2 missing-bullets (empty bridge/topic0 no-confirm + bridge-less quorum test), gap-4 bullet (both bypass shapes covered by tests naming attacker authority).

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - both guards are fully wired enforcement; no placeholder data.

## Threat Flags

None - T-21-0801/0802 implemented and unit-proven adversarially; T-21-0803 log content follows the house rule (PreviewValue-truncated hash, url, chain id only — no key material); T-21-0804/0805 accepted dispositions hold (deserializer stays open; continue-only loop semantics unchanged). No security surface beyond the plan's threat model.

## MINT-04 Legitimacy Restatement (success criterion)

requirements-completed: [MINT-04] — the Complete mark in REQUIREMENTS.md, recorded by 21-02 but left unsupported per 21-VERIFICATION.md, is now legitimate: 21-02's green matrix covers the single-output, bridge-configured attack shapes (token/amount/recipient/parity mismatch, undecodable log, bogus topic0, honest-RPC identities), and this plan's closures prove the two reviewed bypass shapes are actually rejected (multi-output inflation CR-01; bridge-less-endpoint quorum CR-02) with compat twins showing legitimate confirmation is unchanged.

## Self-Check: PASSED

Both key files exist on disk; all 5 task commits (e357a4c4c, 2132e0b9c, 2ad50c9f4, 6b67302de, 8aa4759ab) found in git history on gsd/phase-21-consensus-trust-mint-authorization.

---
*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-21*
