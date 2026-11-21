---
phase: 21-consensus-trust-mint-authorization
plan: "02"
subsystem: auth
tags: [mint-authorization, evidence, fail-closed, abi-decode, receipt-binding, bridge]

# Dependency graph
requires:
  - phase: 21-consensus-trust-mint-authorization/21-01
    provides: nothing directly (parallel wave); shares the per-plan adversarial+compat discipline
provides:
  - RequiresPublicChainEvidence per-type taxonomy (mint + mint-v2 = public-evidence-required) at the evidence layer
  - Fail-closed MINT-02 identities — empty source reference and empty/local ("supergenius", "supergenius/<scope>") chain ids reject mint types with named-rule logs
  - DecodeAndBindBurnFacts — exact receipt/mint payload binding (token, amount, recipient) inside the matched-log branch of GatherVerificationEvidence
  - MintTransactionV2::GetPrimaryDestinationAddress — the destination accessor beside GetSlotID's own field
  - RpcVerificationEvidence decoded_token_id/decoded_amount/decoded_recipient carriers for Phase 24 content corroboration
  - MockRpcTransport::BuildBridgeOutFactReceiptJson — fact-carrying mock receipts for any test that must confirm a mint-v2
affects: [21-03, 21-05, 21-06, 24-bridge-ingestion-safety, TRUST-02-audit]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Canonical topic0 selects the event shape for decode; a matched log that names no canonical bridge event fails closed"
    - "Fact mismatch discards ALL gathered evidence (mirrors the reverted-receipt precedent) so no slot can certify a mismatched mint"
    - "Receipt-fact normalization reuses the relayer's one home (BridgeRelayer::ParseBurnEventValues) — the validator never re-implements v1/v2 destination decoding"

key-files:
  created:
    - test/src/account/mint_evidence_binding_test.cpp
  modified:
    - src/account/PublicChainInputValidator.hpp
    - src/account/PublicChainInputValidator.cpp
    - src/account/MintTransactionV2.hpp
    - src/account/MintTransactionV2.cpp
    - test/src/account/CMakeLists.txt
    - test/src/mock/mock_rpc_transport.hpp
    - test/src/mock/mock_rpc_transport.cpp
    - test/src/bridge_race/bridge_race_fault_rpc_test.cpp

key-decisions:
  - "MINT-04 binding applies to MintTransactionV2 (dynamic_cast): v1 mints have no destination accessor, the plan's compat suites pin v1 receipt shapes (empty event data), and 21-03/21-05/21-06 disable v1 mint admission — MINT-02 identity fail-close covers BOTH mint types"
  - "Decoded facts flow through BridgeRelayer::ParseBurnEventValues (BurnEventParams), not BridgeEventClaim: the claim struct's recipient is a 20-byte EVM Address while the bridge destination is the 64-byte SGNS pubkey carried in event data — the relayer's parser is the existing one-home normalization"
  - "Scoped local chain ids are matched by explicit 'supergenius/' prefix (never by stripping); non-mint local-path behavior is byte-identical to before"

patterns-established:
  - "Adversarial+compat unit pairs for evidence-layer authorization via SetTransportFactory mocks with independently-set mint fields and parameterized ABI-encoded receipts (v1 dynamic-bytes and v2 bytes32 shapes)"

requirements-completed: [MINT-02, MINT-04]

# Metrics
duration: 40min
completed: 2026-10-20
---

# Phase 21 Plan 02: Mint Evidence Fail-Closed + Receipt Binding Summary

**Per-type fail-closed mint identities and exact burn-receipt fact binding in PublicChainInputValidator — an honest-RPC receipt for a different burn can no longer authorize a mismatched mint, and empty/local-identity mints reject with named rules instead of certifying with empty slots**

## Performance

- **Duration:** 40 min (2026-10-20T19:41:59Z → 2026-10-20T20:22:02Z)
- **Tasks:** 3 (TDD: RED → GREEN for Tasks 1 and 2; completion commit for Task 3)
- **Files modified:** 10 (1 created)

## Accomplishments

- **MINT-02 (finding #3, T-21-0201/0203):** `RequiresPublicChainEvidence` owns the per-type taxonomy (exactly "mint" and "mint-v2"). A public-evidence-required mint with an empty source reference, or with an empty/local chain id (`supergenius` and every `supergenius/<scope>` ScopedChainId form, matched by explicit prefix), now returns `evidence.valid = false` with an error log naming the rule. Non-mint routed types (escrow/genesis/registration) keep the local bypass byte-identically — pinned by a compat twin (Pitfall 7).
- **MINT-04 (finding #5, T-21-0202/0204):** inside the matched-log branch, `DecodeAndBindBurnFacts` selects the event shape by canonical topic0 (v1 `BridgeSourceBurned` / v2 `BridgeOutInitiated`), decodes with `eth::abi::decode_log` and the registered ABI descriptors, normalizes via `BridgeRelayer::ParseBurnEventValues` (the one home for v1 raw-bytes / v2 X-only decompression), and compares token (`GetTokenID`), amount (`GetAmount`), and recipient (`GetPrimaryDestinationAddress`) against the mint's own fields. Any failure — non-canonical matched topic0, undecodable log, malformed values, fact mismatch — discards all gathered evidence so no slot can certify the claim.
- `RpcVerificationEvidence` carries `decoded_token_id` / `decoded_amount` / `decoded_recipient` (one fact, one field) so Phase 24's content-digest corroboration consumes this decode instead of re-decoding (Pitfall 8).
- `MintTransactionV2::GetPrimaryDestinationAddress` added beside `GetSlotID`'s own field; nothing re-parses `GetSlotID` output (grep in PCIV: 0 matches).
- 13-case adversarial+compat unit matrix (VER-01 comments naming attacker authority, expected rejection, and pre-fix behavior per case), plus the fact-carrying mock upgrade that keeps the deterministic full-pipeline race test meaningful under binding.

## Task Commits

1. **Task 1: MINT-02 fail-closed per-type branches (tdd)** — `a46baf76a` (test: RED — empty source ref / empty chain id / local chain id mints must reject, today accepted) + `7f4348612` (feat: GREEN — predicate + both branches)
2. **Task 2: MINT-04 receipt decode + fact binding (tdd)** — `1d0225cef` (test: RED — amount/token/recipient/parity mismatches + undecodable log must reject; data scaffolding: evidence carriers + destination accessor) + `6845e7533` (feat: GREEN — decode+bind at one comparison home)
3. **Task 3: Adversarial + compat unit pairs (mock transport)** — `10c8a0693` (test: bogus-topic0 negative control; MockRpcTransport fact-receipt builder; bridge_race_fault_rpc canonical-topic0 + canned fact-matching receipts)

**Plan metadata:** (this commit)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] bridge_race_fault_rpc mock receipts can no longer confirm a mint**
- **Found during:** Task 2 design review (compat sweep before implementation)
- **Issue:** The deterministic full-pipeline fault test serves `MockRpcTransport` default success receipts (empty event data, arbitrary `0x1234…` topic0) to ALL THREE quorum slots — including the DIRECT slot — for a mint-v2 created from a real Anvil burn. Under MINT-04 binding those receipts are undecodable/non-canonical, so the POSITIVE case (mint must reach quorum) would fail.
- **Fix:** `MockRpcTransport::BuildBridgeOutFactReceiptJson` (fact-carrying v2 receipt builder in the mock's own home); the fault test now accepts the canonical v2 topic0 and serves canned receipts whose facts match the seeded burn (token 0, kMintAmount, compressed destination from `BridgeDestinationFromSgnsAddress`). Verified end-to-end: the test passes twice (pre- and post-format), proving binding works through the real node pipeline.
- **Files modified:** test/src/mock/mock_rpc_transport.hpp/.cpp, test/src/bridge_race/bridge_race_fault_rpc_test.cpp (outside the plan's files_modified — test-shape fix necessitated by the security change)
- **Committed in:** 10c8a0693

**2. [Rule 3 - Blocking] Compilable RED required data scaffolding**
- **Found during:** Task 2 RED authoring
- **Issue:** The RED tests assert the new `decoded_*` evidence fields and the destination accessor; without declarations the RED cannot compile (and thus cannot fail).
- **Fix:** The RED commit carries the pure data surface (evidence carriers, `GetPrimaryDestinationAddress`) with zero behavior; all behavioral assertions still fail at RED (6 failing cases) and pass at GREEN.
- **Files modified:** src/account/PublicChainInputValidator.hpp, src/account/MintTransactionV2.hpp/.cpp
- **Committed in:** 1d0225cef

**3. [Design note] Decoded facts flow through BridgeRelayer::ParseBurnEventValues, not BridgeEventClaim**
- **Found during:** Task 2 implementation
- **Issue:** The plan's action 2 sketched decoding "into BridgeEventClaim", but that struct's `recipient` is a 20-byte EVM `Address` — the bridge burn's destination is the 64-byte SGNS pubkey carried in the event data. Mapping one onto the other would be incorrect, and no log→BridgeEventClaim decoder exists to reuse.
- **Fix:** Decode via `eth::abi::decode_log` (the plan's key_link pattern `abi_decoder|BridgeEventClaim` matches on `abi_decoder`), then normalize through the relayer's existing parser (one home for the v1/v2 destination rules — DRY invariant). PCIV still includes `<eth/abi_decoder.hpp>` per the acceptance criteria.
- **Files modified:** src/account/PublicChainInputValidator.cpp
- **Committed in:** 6845e7533

**4. [Plan-text note] Scoped-local-chain "(today true)" was inaccurate**
- **Found during:** Task 1 RED run
- **Issue:** The plan claims `supergenius/333` is accepted today; in fact it already falls through to the no-endpoints error path and returns invalid — the rejection was incidental, never policy.
- **Fix:** Shipped the explicit per-type rejection (named-rule log) and pinned it with a test whose comment records the pre-fix behavior precisely.
- **Committed in:** a46baf76a / 7f4348612

---

**Total deviations:** 3 auto-fixed + 1 plan-text note
**Impact on plan:** All required for the plan's own compat and security contracts — no scope creep.

## Issues Encountered

- `bridge_race_single_burn_test` fails in this tree (solo and concurrent runs). Pre-existing open P1 todo (`.planning/todos/pending/bridge-race-not-all-11-mint-within-window.md`, 2026-07-21: "still does not pass"); in this run the watcher never discovers the seeded burn at all (`GatherVerificationEvidence` executes 0 times — the 21-02 code paths never ran), so it is not caused by this plan. Logged in `deferred-items.md`. The deterministic sibling `bridge_race_fault_rpc_test`, which drives the full mint-v2 witness path with mocks, passes.
- clang-tidy cannot consume this tree's `compile_commands.json` directly (dual-arch compile commands); used the filtered single-arch DB at /tmp/sgns-tidy-db (21-01 methodology). No new Required-group findings in changed regions — the findings in the changed files are the pre-existing Recommended-group noise (naming, scoped_lock, cognitive complexity, use-after-move in untouched `StoreEvidence`) plus generated-proto-header noise.
- `mock_rpc_transport.cpp` was not previously clang-format-clean; the Task 3 format pass normalized the whole file (530-line diff, behavior-neutral; the fault test re-run green post-format).

## Verification

- `ctest -R "public_chain_input_validator|mint_evidence_binding"` — 2/2 targets, 13/13 + 13/13 cases passed.
- `bridge_e2e_chainlist_test` (untouched by this plan) — 12/12 passed.
- `bridge_race_fault_rpc_test` (Foundry, full 11-node pipeline, mock quorum) — passed twice (pre- and post-format).
- `multi_node_finality_fault_compatibility_smoke_test` — passed.
- clang-format applied to all changed C++ files; clang-tidy clean of new Required findings (see Issues Encountered).
- Acceptance greps: PCIV.cpp includes `<eth/abi_decoder.hpp>`; comparisons read `GetTokenID`/`GetAmount`/`GetPrimaryDestinationAddress`; `GetSlotID` appears 0 times in PCIV; `RpcVerificationEvidence` carries the three `decoded_` fields.

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - all paths are wired to the real decoder/binder; no placeholder data.

## Threat Flags

None - all five `mitigate` dispositions implemented and unit-proven (T-21-0201 per-type fail-close; T-21-0202 exact payload binding incl. honest-RPC mismatch cases; T-21-0203 compat twins pin escrow/genesis/registration local paths; T-21-0204 undecodable matched log fails closed; T-21-0205 logs name rule/fact with truncated hashes only). No new security surface beyond the plan's model.

## Next Phase Readiness

- 21-03/21-05/21-06 (v1 mint admission disable) close the residual v1 window this plan deliberately left on today's receipt checks (v1 has no destination accessor; its suites pin the empty-data receipt shape).
- Phase 24 consumes `RpcVerificationEvidence::decoded_*` for content-digest corroboration and rewrites the finality half of `GatherVerificationEvidence`; the decode+bind block is a self-contained named helper ready to be lifted.
- `MockRpcTransport::BuildBridgeOutFactReceiptJson` is the receipt builder for any future test needing a confirming mock quorum.

## Self-Check: PASSED

All 10 key files exist on disk; all 5 task commits (a46baf76a, 7f4348612, 1d0225cef, 6845e7533, 10c8a0693) found in git history.

---
*Phase: 21-consensus-trust-mint-authorization*
*Completed: 2026-10-20*
