---
phase: 17-eligibility-policy-gating-dry-run
audited: 2026-10-20
auditor: gsd-security-auditor
asvs_level: 1
block_on: high
branch: gsd/phase-17-eligibility-policy-gating-dry-run
threats_found: 26
threats_closed: 26
threats_open: 0
threats_accepted: 3
status: secured
---

# Phase 17 Security Audit — Eligibility, Policy & Gating (Dry-Run)

Audit of the implemented phase-17 surface (commits `2086039f7^..HEAD`, including
the review-fix series `eb1a23adf`..`10f6371a2`, the verifier gap fix
`fd7e96a83`, and the generation-tag fix `df2762719`). Every declared mitigation
was verified against the source on disk — plan text, SUMMARY claims, and
review-fix reports were not accepted as evidence. Where a mitigation's proof is
a test, the test body was read, not just its name.

**Surface audited:** the `spent_by_txid` spend-recency carrier
(`UTXOManager::ConsumeUTXOs` / `GetOutPointSpender` + four
`TransactionManager` call sites), the CRDT read exposures (`GetPriority` /
`GetKeyPriority`, `GetLocalDeltaKeys`), the generalized cert-slot
`previous_hash` rung, the `PruneEligibility` evaluator (window arithmetic,
allowlist, `PruningConfig`, block gate, pruned-marker contract), the
`PrunedTransactionRecord` proto, and the `GeniusNode` config + dry-run timer
wiring.

## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| tx-processing → UTXOManager | spender identity arrives from the in-scope, signature-verified transaction object |
| evaluator → GlobalDB facade → CrdtSet/CrdtDatastore | read-only queries over locally committed CRDT state; `cid_string` originates from the tx→CID map |
| previous_hash resolution → certificate store | quorum-signed `/cert/` artifacts read through `GetCertificateBySlot` |
| DB values → evaluator | stored transaction bytes deserialize at evaluation time; malformed input must never crash and never become eligible |
| config file → PruningConfig | operator-controlled JSON parses through `FromJSON` (type-checked per key, fail-closed) |
| key space → allowlist | candidate/block keys must prove membership in `/bc-<net>/tx/` over monitored networks |
| block bytes → gate | delta blocks parse at evaluation time; a corrupt block fails closed (not erasable) |
| marker bytes → lookup | marker records decode from the node-local raw surface; malformed records must not crash or masquerade as PRUNED |
| timer cycle → evaluator | reads committed state only; must never write or erase (dry-run invariant) |

## Threat Register Verification

| Threat ID | Category | Component | Disposition | Status | Evidence |
|-----------|----------|-----------|-------------|--------|----------|
| T-17-0101 | Tampering | ConsumeUTXOs consuming_txid parameter | accept | CLOSED (accepted) | Parameter originates from the in-scope transaction object at every call site (all four verified: `TransactionManager.cpp:2640/:2673/:2699/:2910` — post-WR-01 guarded parses). No external input crosses the boundary. Logged under Accepted Risks |
| T-17-0102 | Repudiation | spent_by_txid record | mitigate | CLOSED | `src/account/UTXOManager.cpp:291` (genuine-branch populate), `:311` (synthesized-branch initializer — uniform), `:392` (revert reset to nullopt), `:936-943` (LoadUTXOs restore), `:1052-1056` (StoreUTXOSnapshot persist). Round-trip proven by `test/src/account/utxo_manager_test.cpp:785` (`SpenderSurvivesBucketRewriteAndReload`) plus `:707/:733/:759` (populate, synth-not-genuine, revert-reset) — 34/34 green |
| T-17-0103 | Tampering | GetOutPointSpender accessor | mitigate | CLOSED | `src/account/UTXOManager.cpp:787-797` — `std::shared_lock( utxos_mutex_ )` at `:790` mirroring `IsOutPointGenuinelyConsumed` (:781); returns `std::optional<base::Hash256>` BY VALUE (copy, never a reference into the map) |
| T-17-0201 | Tampering | GetLocalDeltaKeys cid_string input | mitigate | CLOSED | `src/crdt/impl/crdt_datastore.cpp:2322` — `BOOST_OUTCOME_TRY( auto cid, CID::fromString( cid_string ) )` validates BEFORE any block access (`:2324` GetNodeWithoutRequest, strictly local). Fail-closed pinned by `test/src/crdt/crdt_priority_delta_keys_test.cpp:283` (`DeltaKeysFailClosedOnUnknownCid`) |
| T-17-0202 | Information disclosure | priority/delta-keys exposures | accept | CLOSED (accepted) | Both exposures are const read-only passthroughs over locally committed data (`crdt_datastore.cpp:2260-2262`, `:2320-2340`; `globaldb.cpp:902-910`, `:1015-1023`); `GetNodeWithoutRequest` never fetches from the network. Logged under Accepted Risks |
| T-17-0203 | Tampering | GetPriority 0-for-missing-key ambiguity | mitigate | CLOSED | Doxygen caveat at both levels: `crdt_datastore.hpp:267-276` ("0 never implies existence") and `globaldb.hpp:330-338`; semantics pinned by `PriorityPassthroughToleratesMissingKey` (test :228). The consumer-side guard is the evaluator's existence-before-priority ordering (T-17-0403, verified below) |
| T-17-0301 | Tampering | generalized cert rung returning a wrong hash | mitigate | CLOSED | `src/transaction/TransactionManager.cpp:1688-1729` — all four binding checks verbatim: DecodeNonceSubject (`:1712-1714`), TRANSACTION_NOT_SET (`:1715`), nonce==nonce−1 (`:1716`), account_id==own (`:1717`); every failed check returns "" (`:1700-1726`); cert via `GetCertificateBySlot` (`:1706`). Negative proven by `NoCertNoValueFailsClosed` (test :1591) |
| T-17-0302 | Tampering | rung-3/rung-4 divergence after generalization | mitigate | CLOSED | `CertRungMatchesCRDTScan` (`test/src/account/transaction_manager_certificate_fallback_test.cpp:1613`) asserts the captured cert-rung answer equals the full CRDT-scan answer for the same certified non-registration predecessor; 28/28 green |
| T-17-0303 | Elevation | relaxation widening beyond intended scope | mitigate | CLOSED | Commit `9179cc9c2` diff on `TransactionManager.cpp` shows exactly ONE changed code line (the `GetType() != "registration"` disjunct removal; remainder is the comment block). Ladder order intact at `:1660-1686` (tracked → persisted → cert → CRDT scan) |
| T-17-0304 | Spoofing | forged certificate at a slot | accept | CLOSED (accepted) | `GetCertificateBySlot` rechecks the slot key and runs ValidateCertificate internally — the pre-existing consensus trust model; no new surface introduced (tests build certificates through the real signing-manager path). Logged under Accepted Risks |
| T-17-0401 | Tampering | allowlist bypass via prefix confusion | mitigate | CLOSED | `src/transaction/PruneEligibility.cpp:119-133` — THE one home: `rfind(prefix, 0) == 0` over `GetMonitoredNetworkIDs()` only, prefix `GetBlockChainBase(net) + "tx/"` WITH trailing slash (:126). Test `:634-653` enumerates all five crafted keys (cert slot, reg/ path, `/bc-999/`, slashless `txfoo`, proof key) rejecting each, accepting only the real form |
| T-17-0402 | Elevation | NodeType-default-Light mistaken for consent | mitigate | CLOSED | `PruneEligibility.cpp:108-114` — `New()` fails `operation_not_permitted` for non-Light; `PruningConfig.enabled = false` default (hpp:213) is the consent gate; FromJSON fail-close at `:90-97` (enabled without window → disabled + fixed `kWindowRequiredMessage`), WR-03 zero-reject at `:84-88`. Tests `:556` (Full/Archive refuse), `:692`, `:711` (zero window), `:721` |
| T-17-0403 | Tampering | missing-key priority-0 vacuously satisfying the window | mitigate | CLOSED | `PruneEligibility.cpp:430` — `ResolveTransactionPathAndCID(spender)` establishes existence BEFORE `:458` `GetKeyPriority(resolved path)`; unresolvable ⇒ `SPENDER_UNRESOLVED` (:454) and priority-read failure ⇒ `SPENDER_UNRESOLVED` (:461). Ordering documented in hpp:61-66 and :419-420. Ghost-spender case in the ExactEligibleSet fixture; mutation check 3 documented in 17-04-SUMMARY |
| T-17-0404 | Repudiation | unexplained eligibility decisions | mitigate | CLOSED | Every failure path records a `PruneReason` via `RecordReason` (OUT_OF_PREFIX :383, DESERIALIZE_FAILED :392, NO_OUTPUTS :406, UNSPENT_OUTPUT :416, SPENDER_NOT_RECORDED :423, SPENDER_UNRESOLVED :454/:461, SPEND_WITHIN_WINDOW :489); `eligible = true` only when `HasFailureReason` is false (:475-493). WR-05 closes the last silent skip: eligible-without-CID emits an explicit `CID_UNRESOLVED` BlockGateResult (:289-300) |
| T-17-0405 | DoS | malformed stored tx value crashing evaluation | mitigate | CLOSED | `PruneEligibility.cpp:387-393` — `DeSerializeTransaction` failure ⇒ DESERIALIZE_FAILED ineligible, function returns the candidate (scan continues; per-network query error also continues at :254-258). Proven by `FailClosedGates_GarbageValueReportsDeserializeFailed` (test :584, commit `19df08956` — the summary's self-found test gap, closed) |
| T-17-0406 | Tampering (invariant) | dry-run writing anything | mitigate | CLOSED | Grep gate re-run this audit: `EraseEntry\|EraseTransactionEntry\|PutRaw\|StageErase\|StageBlock\|DeleteKey\|DeleteTransaction` on `PruneEligibility.cpp` == exactly 1 match — the `PutRaw` at :226, inside `WritePrunedEntryForTest`'s body (:215-227). The evaluator exposes no mutating API (header :243-248: "ZERO writes: no Put, no Erase, no staging, no hooks") |
| T-17-0501 | Tampering | block gate missing tombstone keys | mitigate | CLOSED | Gate consumes `GetLocalDeltaKeys` (`PruneEligibility.cpp:338`), which iterates BOTH `delta.elements()` and `delta.tombstones()` (`crdt_datastore.cpp:2331-2338`); per-key loop `:349-364`. Proven by `BlockGate_TombstoneKeyHoldsBlockNotErasable` (test :893); the 17-05 mutation check documented the test binds (an elements-only gate fails it) |
| T-17-0502 | Tampering | forged/malformed marker masquerading as PRUNED | mitigate | CLOSED | `PruneEligibility.cpp:159-163` — `ParseFromArray` failure logs ERROR and returns `std::errc::bad_message`, never a silent PRUNED. WR-04 hardening: `:171-177` — decoded `tx_path` must be non-empty AND end with the requested `tx_hash`, else `bad_message` (kills both the wrong-key PRUNED grant and the whole-store `QueryKeyValues("")` scan). Tests `:1022` (mismatched path), `:1041` (empty payload) |
| T-17-0503 | Tampering (invariant) | marker+value coexistence hiding a state bug | mitigate | CLOSED | `PruneEligibility.cpp:202-209` — `VALUE_PRESENT_INVARIANT_VIOLATION` tri-state returned with an ERROR log ("never guess which side is right"); both reads share one resolution path (marker via `GetRaw` :147, value presence via `QueryKeyValues(record.tx_path())` :184 with exact-key match :190 — path already WR-04-validated). Test `:1001` asserts the enum (and non-PRUNED) |
| T-17-0504 | Tampering | non-tx key smuggled into an otherwise-eligible block | mitigate | CLOSED | Per-key allowlist conformance inside the gate: `PruneEligibility.cpp:351-354` — `!IsAllowlistedTxKey(key)` ⇒ `OUT_OF_PREFIX`, `erasable = reasons.empty()` (:366). Proven on a real committed block by `BlockGate_BlockWithOutOfPrefixKeyIsNotErasable` (test :851) |
| T-17-0505 | Elevation | marker write escaping the test-only boundary | mitigate | CLOSED | `PutRaw` count in `PruneEligibility.cpp` == 1 at :226, textually inside `WritePrunedEntryForTest` (:215-227); header :397 "@note No production caller may invoke this method"; grep `WritePrunedEntryForTest` across `src/` == definition only (re-run this audit). The production write is Phase 18's batch participant |
| T-17-0601 | Tampering/Elevation | config silently enabling pruning (default-on foot-gun) | mitigate | CLOSED | `src/account/GeniusNode.cpp:478-489` — the whole pruning read sits inside `HasMember("pruning") && IsObject()`; absent member executes zero new statements (SC-5); delegation to the one parse home `PruningConfig::FromJSON` (:480, no per-key duplication); WARN on `disabled_reason` (:481-484); resolved values INFO-logged once (:485-488). Fail-close chain verified under T-17-0402 |
| T-17-0602 | Elevation | NodeType-default-Light consenting to pruning | mitigate | CLOSED | Defense in depth: role gate at the wiring (`GeniusNode.cpp:4872-4878` — non-Light WARN + return, timer never created) AND at evaluator construction (`PruneEligibility.cpp:108-114` — `operation_not_permitted`); consent remains the config object (default disabled) |
| T-17-0603 | Tampering (invariant) | dry-run cycle writing anything | mitigate | CLOSED | `RunPruneDryRunCycle` (`GeniusNode.cpp:4913-5020`) calls Evaluate + INFO only; grep of `GeniusNode.cpp` for `EraseEntry\|EraseTransactionEntry\|PutRaw\|StageErase\|StageBlock` == 0 matches (re-run this audit). WR-02 robustness verified present AND safe: transient-error branch WARNs (:4969-4970) and the summary block is guarded by `if ( !evaluation.has_error() )` (:4975 — the verifier-gap fix, commit `fd7e96a83`), so the errored outcome is never dereferenced |
| T-17-0604 | DoS | evaluation on the CRDT callback path (pre-commit reads) | mitigate | CLOSED | Timer-driven only: the phase diff adds exactly 2 `async_wait` sites (`GeniusNode.cpp:4898`, `:5008` — first cycle + reschedule, both gc_timer_ weak-self pattern) and zero CRDT-callback tokens (`OnElement\|ElementCallback\|RegisterCallback\|set_callback\|Subscribe` in the diff == 0, re-run this audit) |
| T-17-0605 | DoS | stale TransactionManager reference after generation switch | mitigate | CLOSED | Per-tick construction from a consistent `SnapshotAccountServices()` snapshot (`GeniusNode.cpp:4931-4943`, no cached evaluator — CR-01 fix `eb1a23adf`); snapshot shared_ptrs held for the whole cycle so the account/UTXOManager outlive the evaluation; `tx_globaldb_` unlocked read justified by the documented init-once/reset-after-io-drain invariant (:4926-4930). Generation-tag fix (`df2762719`): `:4982` marks a superseded generation's summary "[generation superseded — retired snapshot]" |

**Totals:** 26 register entries — 23 mitigate (all CLOSED with code evidence), 3
accept (all CLOSED with documented entries and boundary verification).
**Open: 0.**

## Threat Flags (from SUMMARYs)

All six SUMMARYs carry plan-authored `## Threat Model Disposition` sections
(the register re-stated with evidence) rather than executor-raised
`## Threat Flags`; no new attack surface was reported by any executor.

One self-found gap was recorded in 17-04-SUMMARY (DESERIALIZE_FAILED was
implemented but unproven) and closed in-phase by commit `19df08956`
(`FailClosedGates_GarbageValueReportsDeserializeFailed`) — maps to T-17-0405.

**Unregistered flags: 1 (informational, non-blocking).**

- **GENIUS_CHAIN_ID witness bypass** — `PublicChainInputValidator.cpp:326-334`
  accepts any signed mint declaring chain_id "supergenius" (or empty),
  unconditionally in released code. Discovered during this phase's live
  staging-net UAT (17-HUMAN-UAT.md session byproduct) and recorded as a P1
  security todo (`.planning/todos/pending/genius-chain-witness-bypass-hardening.md`).
  NOT phase-17 attack surface: zero phase-17 commits touched the file (last
  change predates `2086039f7`), and the register is plan-complete. Severity is
  already refined in the todo (the witness-build producer gate blocks
  fabricated mints at send time; exposure is quorum-controlled nets and
  defense-by-topology rather than policy). Tracked for Phases 18/19
  (commit `f3c6d8862`). Logged here so the next phase's register inherits it.

## Accepted Risks Log

| ID | Threat | Risk Accepted | Boundary / Rationale | Date |
|----|--------|---------------|----------------------|------|
| AR-17-1 | T-17-0101 | The `consuming_txid` parameter could in principle carry a tampered spender identity | Parameter originates from the in-scope transaction object at every call site (verified: all four sites parse the spender from the tx being processed, post-signature-verification upstream); no external input crosses the tx-processing→UTXOManager boundary; WR-01 additionally made every parse fail-closed | 2026-10-16 (plan) / 2026-10-20 (audit) |
| AR-17-2 | T-17-0202 | The two new read exposures could disclose locally stored state | Read-only const passthroughs over data the caller (in-process evaluator) can already reach through the same store; `GetNodeWithoutRequest` is strictly local — no new network surface | 2026-10-16 (plan) / 2026-10-20 (audit) |
| AR-17-3 | T-17-0304 | A forged certificate at a slot could feed a wrong previous_hash | `/cert/` entries are quorum-signed consensus artifacts; `GetCertificateBySlot` rechecks the slot key and runs `ValidateCertificate` (signature checks) internally — the pre-existing consensus trust model; the relaxation adds readers of the same artifacts, not new trust | 2026-10-16 (plan) / 2026-10-20 (audit) |

## Independent Review Cross-Reference

The independent code review (17-REVIEW.md), its fix series (17-REVIEW-FIX.md),
and the verifier's gap resolution (17-VERIFICATION.md) were re-verified during
this audit — every finding that carries a register mitigation is present in
code on the audited branch:

- **CR-01** (dry-run cycle reads replaceable members unlocked — the T-17-0605
  mitigation): fixed in `eb1a23adf` — `SnapshotAccountServices()` snapshot,
  held shared_ptrs, documented `tx_globaldb_` invariant
  (`GeniusNode.cpp:4913-4943`).
- **WR-01** (unchecked `.value()` on `fromReadableString` at the four new
  call sites — hardening of the T-17-0101 accepted boundary): fixed in
  `c93ed120f` — guarded parses returning `outcome::failure` at
  `TransactionManager.cpp:2640-2648`, `:2673-2682`, `:2699/:2738`,
  `:2910-2917`; the three pre-existing unchecked sites (:1293/:2748/:2952)
  are explicitly out of phase scope.
- **WR-02** (transient evaluation error killing the timer — T-17-0603
  robustness): fixed in `120fffba2` AND the verifier's gap on that fix closed
  in `fd7e96a83` — only `std::errc::invalid_argument` stops the cycle
  (:4956-4963); the transient path WARNs (:4969) and the summary block is
  guarded by `if ( !evaluation.has_error() )` (:4975), so `evaluation.value()`
  is unreachable on the error path.
- **WR-03** (`finality_window: 0` defeating the fail-closed contract —
  T-17-0402/0601): fixed in `fe68cba4b` — zero rejected at read
  (`PruneEligibility.cpp:84-88`); regression tests `:711`/`:721`.
- **WR-04** (marker `tx_path` trusted with no consistency check — the
  T-17-0502 mitigation): fixed in `79f95961b` — terminal-segment validation
  at `:171-177`; regression tests `:1022`/`:1041`.
- **WR-05** (eligible candidate with unresolved CID silently skipping the
  block gate — the T-17-0404 repudiability mitigation): fixed in `10f6371a2`
  — explicit `CID_UNRESOLVED` BlockGateResult at `:289-300` (source-audit
  verified; the fail state is unconstructible through the fixture, per
  REVIEW-FIX).

Documented residuals (designed deferrals, NOT open threats — the declared
mitigations they neighbor are fully present):
- **WR-02's check-then-act window** on the dry-run summary's generation tag
  (`GeniusNode.cpp:4982` reads the generation a second time): Info-tier in
  17-REVIEW.md; the critical section is Phase 18's (TxPruner owns erasure
  closure). The dry-run consequence is at most a mislabeled summary line on a
  retired snapshot — zero writes either way (T-17-0603 grep-proof).
- **IN-03** (whole-store evaluation synchronous on an io-pool thread): Info-tier;
  Phase 18 scaling (worker/strand when the store grows). Matches the
  `RunResultGC` precedent.
- **IN-01** (misleading `finality_window=0` log when unset), **IN-02**
  (duplicate per-cycle INFO summaries), **IN-04** (sibling GlobalDB facades
  return different error codes for the same null-datastore failure): Info-tier
  code-quality items, explicitly out of fix scope, none affecting a register
  mitigation.

## Notes

- The dry-run zero-write invariant is enforced three ways: the evaluator has
  no mutating API and passes the token grep (T-17-0406), the only raw write in
  the new surface is the doc-commented test-only marker writer (T-17-0505),
  and the GeniusNode cycle is grep-clean of every erase/stage token
  (T-17-0603). Production erase-caller inventories remain unchanged from the
  Phase-16 audit.
- `spent_by_txid` writer inventory is exactly the plan's four homes:
  ConsumeUTXOs (populate :291/:311), RestoreConsumedUTXOs (reset :392), the
  UTXOEntry struct field (hpp:85), GetOutPointSpender (read :796) — plus the
  persistence serialization pair (:936-943/:1052-1056).
- The `GetLocalDeltaKeyValues` elements-only precedent is untouched
  (additive-only diff on `impl/crdt_datastore.cpp`, confirmed by the phase
  verification and re-checked by this audit's read of :2300-2318 vs :2320-2340).
- Suite evidence (run by the verifier on the post-fix tree, binaries newer
  than the last fix commit): prune_eligibility_test 22/22, utxo_manager_test
  34/34, transaction_manager_certificate_fallback_test 28/28, crdt
  passthrough 4/4, full ctest exit 0. This audit re-verified source text and
  grep gates; it did not re-run the suites.
- No supply-chain surface: no registry manifests touched in the phase range
  (carried convention from Phase 16; no new external dependencies appear in
  the audited files).

## Audit Trail

| Step | Result |
|------|--------|
| Required reading loaded (6 PLANs, 6 SUMMARYs, VALIDATION, REVIEW, REVIEW-FIX, VERIFICATION) | done |
| Threat register extracted (T-17-0101..0605 across six threat_model blocks) | 26 entries |
| Mitigations verified by source inspection + grep (src/ and test/) | 23/23 present |
| Accept dispositions verified (risk boundary + documentation) | 3/3 |
| Review-fix series re-verified in code (CR-01, WR-01..WR-05) + verifier gap fix + generation-tag fix | all present |
| Containment gates re-run live (PutRaw count; WritePrunedEntryForTest callers; GeniusNode erase tokens; async_wait inventory; CRDT-callback tokens) | pass |
| SUMMARY Threat Flags / UAT byproducts mapped | 1 informational unregistered flag (pre-existing witness bypass, tracked todo) |
| SECURITY.md written; implementation files untouched | done |
