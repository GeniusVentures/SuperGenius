---
phase: 16-erasure-primitives-bookkeeping-hardening
audited: 2026-10-20
auditor: gsd-security-auditor
asvs_level: 1
block_on: high
branch: gsd/phase-17-eligibility-policy-gating-dry-run
threats_found: 17
threats_closed: 17
threats_open: 0
threats_accepted: 2
status: secured
---

# Phase 16 Security Audit — Erasure Primitives & Bookkeeping Hardening

Audit of the implemented phase-16 surface (commits `21138327d`..`210e72daf` plus
review-fix series `55e80f64c`, `176de6520`, `3335739fa`, `d043b86f4`, `46cd906ad`;
all verified present as ancestors of the audited branch). Every mitigation was
verified against the source on disk — documentation and summary claims were not
accepted as evidence.

**Surface audited:** local two-level erasure primitives with zero production
callers — `CrdtSet::StageEraseElement`, `GraphsyncDAGSyncer::StageBlockRemoval`,
`GlobalDB::EraseEntry`, `TransactionManager::EraseTransactionEntry` /
`RemoveProcessedTransactionEntry` — plus their test/gate suites.

## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| caller → CrdtSet::StageEraseElement | in-process caller supplies batch + key; key-layout derivation stays inside CrdtSet |
| caller → GraphsyncDAGSyncer::StageBlockRemoval | in-process caller supplies CID + batch; block-key derivation stays inside the owner |
| caller → GlobalDB::EraseEntry | the ONLY entry to physical erasure; guards run inside the API |
| caller → TransactionManager::EraseTransactionEntry | domain-key construction stays in the map owner |
| test suites → production erase APIs | no test-only backdoors beyond the documented interruption seam |
| ctest gate → source tree | source-scan reads src/ at test runtime; tampered/missing tree fails the gate |

## Threat Register Verification

| Threat ID | Category | Component | Disposition | Status | Evidence |
|-----------|----------|-----------|-------------|--------|----------|
| T-16-01 | Tampering | StageEraseElement key handling | mitigate | CLOSED | `src/crdt/impl/crdt_set.cpp:669-737` — null-batch guard (:672-675), dataStore_ null guard (:677-680), empty-key rejection with `std::errc::invalid_argument` (:685-688, WR-01 fix), keys derived only via private builders `ElemsPrefix`/`TombsPrefix`/`ValueKey`/`PriorityKey` (:701-731), all four families staged (elems + tombs scanned, value + priority unconditional); zero `commit(` in body |
| T-16-02 | Tampering | deleteHookFunc_ side effects | mitigate | CLOSED | Zero `deleteHookFunc_` occurrences in the `StageEraseElement` body (`crdt_set.cpp:669-737`); hook-flag proof in `test/src/crdt/crdt_set_test.cpp` `StagedEraseRemovesAllFamilies` — `SetDeleteHook` sets `deleteHookFired` (:392), `EXPECT_FALSE( deleteHookFired )` after staged erase + caller commit |
| T-16-03 | Denial | stale-DB false-green tests | mitigate | CLOSED | `test::removeAllWithRetry` in every fixture (`crdt_set_test.cpp:311`; `crdt_erase_test.cpp:77,216,433,559`; `globaldb_erase_test.cpp:98`); present-before asserts on every level (`globaldb_erase_test.cpp:297-303`); double-run rule recorded in all four SUMMARY verification sections |
| T-16-04 | Tampering | wrong block-key staging | mitigate | CLOSED | `src/crdt/impl/graphsync_dagsyncer.cpp:467-478` — key built exclusively from `cid.toBytes()` (:476-477) under `dagMutex_` (:475); no caller-supplied raw keys; `StagedRemovalDeletesBlockKeyOnly` asserts exact block-key deletion + sibling CID integrity |
| T-16-05 | Denial | seal-marker erasure → re-fetch storm | mitigate | CLOSED | `grep -rn '"sealed"' src/` == 0 matches (verified this audit); seal key never constructed on any production path (`StageBlockRemoval` body :467-478 has no seal token); tests assert seal present + `isResolved` true after erase (`crdt_erase_test.cpp` rawSealKeyPresent); CI gate `SealKeyHasNoProductionHome` (`crdt_erase_test.cpp:722-731`) |
| T-16-06 | Tampering | tracked-map cleanup reverting spends | mitigate | CLOSED | `src/transaction/TransactionManager.cpp:4260-4275` — plain erase only: `unique_lock tx_mutex_m` (:4262), `find` (:4263), `tx_processed_m.erase` (:4266); zero `RevertTransaction`/`RollBackPeerConfirmedNonce`/`DeleteTransaction`/`DeleteKey` in body; gate case enforces the same ban with extra tokens for TM functions (`crdt_erase_test.cpp:782-787`) |
| T-16-07 | Tampering | non-atomic removeNode chain | mitigate | CLOSED | `StageBlockRemoval` body contains no `removeNode` call; `grep -rn "removeNode" src/` == exactly the 3 owner lines (`graphsync_dagsyncer.hpp:101`, `impl/graphsync_dagsyncer.cpp:461,464`); CI gate `RemoveNodeStaysDormant` (`crdt_erase_test.cpp:736-765`) |
| T-16-08 | Tampering | erasing an in-use CID | mitigate | CLOSED | `src/crdt/globaldb/globaldb.cpp:797-847` — four guards fail-closed `INVALID_PARAMETERS` BEFORE any staging: all-topic `GetCRDTHeadList` scan (:814-825), `IsRootCIDPendingOrActive` (:827-833), `IsCIDInCache` (:835-839), `isResolved`-must-be-true (:842-847); no single-topic `IsHead` used. Check-then-act concurrency caveat documented in the header contract (WR-02 fix, `globaldb.hpp:213-226`) with the critical section explicitly deferred to Phase 18 (see Notes) |
| T-16-09 | Tampering | guard bypass via pre-verified flags | mitigate | CLOSED | API signature takes only `(key, cid)` (`globaldb.hpp:248`) — no trust parameters; guards unconditional inside the body; end-to-end rejection tests assert the exact error value: `HeadCidIsRejected` (`globaldb_erase_test.cpp:367-385`), `UnresolvedCidIsRejected` (:395-420) — both `EXPECT_TRUE( erased.error() == Error::INVALID_PARAMETERS )`; zero production callers of any new symbol (grep inventory clean, this audit) |
| T-16-10 | Tampering | partial erase on crash between writes | mitigate | CLOSED | One batch (`globaldb.cpp:851`), exactly one `commit()` (:867, sync per `rocksdb_batch.cpp` `wo_.sync=true`); interruption seam aborts WITHOUT committing at steps 1/2/3 (:853-866); `InterruptedEraseIsAllOrNothing` TEST_P parameterized over steps 1, 2, 3 (`globaldb_erase_test.cpp:327-365`) asserts everything still present |
| T-16-11 | Tampering | tombstone publication through the new surface | mitigate | CLOSED | `EraseEntry` body (`globaldb.cpp:797-884`) and `EraseTransactionEntry` body (`TransactionManager.cpp:4277-4309`) contain zero `DeleteKey`/`DeleteTransaction`/`removeNode`/`DeleteCIDBlock`/`deleteHook` tokens; CI gate `EraseSurfaceContainsNoTombstones` (`crdt_erase_test.cpp:768-807`) enforces the ban on all five tracked bodies |
| T-16-12 | Information repudiation | silent seal loss post-commit | mitigate | CLOSED | Post-commit `isResolved` re-read (`globaldb.cpp:875-881`) — failure logs "seal marker vanished after erase - seal-addressing bug (erase already committed)" and returns the DISTINCT `Error::POST_COMMIT_ERASE_ANOMALY` (WR-03 fix; enum `globaldb.hpp:93`, error-string switch `globaldb.cpp:51`), separating "erased, then anomaly" from "nothing erased" |
| T-16-13 | Denial | gate bypass via renamed functions | mitigate | CLOSED | Gate FAILS, never silently passes, on drift: `ASSERT_TRUE( filesystem::exists )` per tracked file (:795), `ASSERT_FALSE( body.empty() )` when the definition line is not found (:797), `ADD_FAILURE` when `SUPERGNUS_ERASE_GATE_SRC` is undefined (:587) — `crdt_erase_test.cpp`; negative controls demonstrated in 16-04-SUMMARY |
| T-16-14 | Tampering | vacuous green tests | mitigate | CLOSED | D-C6 discipline verified in test source: fresh `removeAllWithRetry` per fixture; present-before AND absent-after on every level (`globaldb_erase_test.cpp:297-309`); sibling byte-identity negative control + head-intact check (:312-319); substrate reopen pass in `InterruptedBatchIsAllOrNothing` |
| T-16-15 | Denial | interruption seam left armed in production | mitigate | CLOSED | Seam is static, `nullptr` by default (`globaldb.cpp:790`), doc-commented "No production caller may invoke this method" (`globaldb.hpp:256`); zero production references (grep clean outside globaldb owner files, this audit); tests clear the hook after each case (`globaldb_erase_test.cpp:142,353`) and the node-collection destructor clears it again |
| T-16-16 | Tampering | test seal-key drift from vendored contract | accept | CLOSED (accepted) | Accepted risk (16-04-PLAN threat_model). Bound as planned: both test-only constructions carry the vendored-contract comment naming the one production home (`crdt_erase_test.cpp:58-66` citing `datastore_rocksdb.cpp:48-56`; `globaldb_erase_test.cpp:224-229`); drift is caught by the same tests' `isResolved` cross-check. Logged below under Accepted Risks |
| T-16-SC | Tampering | npm/pip/cargo supply chain | accept | CLOSED (accepted) | Accepted risk declared in all four plans. Verified: zero registry installs — `git diff --name-only 21138327d^ HEAD -- package.json package-lock.json Cargo.toml requirements.txt pyproject.toml Pipfile go.mod` == 0 files; tech-stack `added: []` in all four summaries. Logged below under Accepted Risks |

**Totals:** 17 register entries — 15 mitigate (all CLOSED with code evidence), 2 accept (both CLOSED with documented entries + boundary verification). **Open: 0.**

## Threat Flags (from SUMMARYs)

- 16-01-SUMMARY, 16-02-SUMMARY, 16-04-SUMMARY: no Threat Flags section (no new attack surface reported).
- 16-03-SUMMARY `## Threat Flags`: "None — no security-relevant surface beyond the plan's threat model."

**Unregistered flags: none.**

## Accepted Risks Log

| ID | Threat | Risk Accepted | Boundary / Rationale | Date |
|----|--------|---------------|----------------------|------|
| AR-16-1 | T-16-16 | Test code constructs the seal marker key (`cid.toString() + "sealed"`) to assert seal retention; a vendored format change could drift the test construction | Vendored-contract comments name the one production home (`thirdparty/ipfs-lite-cpp/.../datastore_rocksdb.cpp:48-56`); the same tests' `isResolved` cross-check fails if the format changes; production src/ remains seal-free (CI-gated) | 2026-10-15 (plan) / 2026-10-20 (audit) |
| AR-16-2 | T-16-SC (x4 plans) | No supply-chain scanning performed for this phase | Zero registry installs: no package manifests touched across the phase commit range; only existing vendored targets linked | 2026-10-15 (plan) / 2026-10-20 (audit) |

## Independent Review Cross-Reference

The independent code review (16-REVIEW.md) and its fix series (16-REVIEW-FIX.md)
were re-verified during this audit — all four in-scope findings are present in
code on the audited branch:

- **CR-01** (cross-network CID/path mismatch → asymmetric partial erase): fixed —
  `ResolveTransactionPathAndCID` private helper (`TransactionManager.cpp:4538`,
  hpp:882 private) pairs path+CID from the SAME network; `EraseTransactionEntry`
  uses the pair (`:4293-4295`), and `RemoveProcessedTransactionEntry` keys on the
  same path (`:4303`).
- **WR-01** (prefix scan over-erases descendants; empty key scans the whole
  namespace): fixed — empty-key rejection + `HierarchicalKey(id).IsTopLevel()`
  filter (`crdt_set.cpp:685-717`); regression tests
  `DescendantKeyFamiliesSurviveParentErase` and `EmptyKeyIsRejected`
  (`crdt_set_test.cpp:452,501`).
- **WR-02** (guards check-then-act, header stated them as absolute): fixed as
  documentation — the `EraseEntry` Doxygen now states best-effort-under-concurrency
  semantics and names the caller (Phase 18 TxPruner) as the critical-section owner
  (`globaldb.hpp:213-226`; in-code comment `globaldb.cpp:807-811`).
- **WR-03** (post-commit failure indistinguishable from guard rejection): fixed —
  distinct `Error::POST_COMMIT_ERASE_ANOMALY` with documented "data IS erased"
  semantics (`globaldb.hpp:93,240-246`; `globaldb.cpp:875-881`).

Open Info findings IN-01..IN-04 (dead error branch, untyped empty error codes,
test assert diagnostics, case-insensitive compares) are code-quality items
explicitly out of fix scope, none affecting a register mitigation.

## Notes

- The guard race window (WR-02) is a **documented residual by design**, not an
  open threat: the declared T-16-08 mitigation (four fail-closed guards inside
  the API, before staging) is fully present; concurrency closure was scoped to
  Phase 18's TxPruner at plan time (RESEARCH Pitfall 7) and the shipped header
  contract states the limitation. This is acceptable while the surface has zero
  production callers (verified: no call sites of `EraseEntry`,
  `EraseTransactionEntry`, `RemoveProcessedTransactionEntry`,
  `StageEraseElement`, or `StageBlockRemoval` outside the owner files on this
  branch).
- The interruption seam (`SetEraseInterruptHookForTest`) is the only test-only
  production seam; it is inert (nullptr) in production and doc-commented.
- clang-tidy could not be executed in this environment (documented in
  16-01..16-04 summaries); compensated by the executor with warning-clean
  rebuilds. Not a register mitigation; noted for the audit trail.

## Audit Trail

| Step | Result |
|------|--------|
| Required reading loaded (4 PLANs, 4 SUMMARYs, VALIDATION, REVIEW, REVIEW-FIX) | done |
| Threat register extracted (T-16-01..T-16-16 + T-16-SC) | 17 entries |
| Mitigations verified by source inspection + grep (src/ and test/) | 15/15 present |
| Accept dispositions verified (risk boundary + documentation) | 2/2 |
| Repo gates re-run live (`"sealed"` == 0; `removeNode` == 3 owner lines; zero production callers) | pass |
| Phase/review-fix commits present as ancestors of audited branch | 17/17 |
| Implementation files modified by this audit | none (read-only) |

---
*Audit method: every mitigation verified by grep/read against the implemented code on the audited branch; summaries and Doxygen were treated as claims, not evidence.*
