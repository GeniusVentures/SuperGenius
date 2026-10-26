# Phase 19: Pruned-Node Survival & Recovery Hardening - Research

**Researched:** 2026-10-23
**Domain:** Post-prune survival: the checkpoint-rebuild guard (GATE-05's remaining half), the TM-SYNCING send-wedge fix (D-02), the tri-state PRUNED answer surface (D-03), the restart matrix / peer-refusal / pruned-mesh E2E harnesses (SURV-01/02/03), and the per-consumer recovery audit (SC-5).
**Confidence:** HIGH (every load-bearing claim re-verified against this repo's source this session with file:line citations; the three fix sites — InitializeUTXOs's mismatch branch, TickOnce's SYNCING error path, the tx-lookup surface — were read in full, and the one previously-unknown mechanism this research uncovered, the clear-wipes-consumed-records fact, was traced through SetUTXOs → StoreUTXOSnapshot to the RocksDB prefix-replace)

## Summary

**D-01's double-spend window is real, precisely located, and its closure requires preserving consumed records through the clear — which today does NOT happen.** The checkpoint-mismatch branch (`InitializeUTXOs`, TransactionManager.cpp:3261-3280) clears via `SetUTXOs({}, address)`, and `SetUTXOs` (UTXOManager.cpp:487-533) drops every entry for the address — including CONSUMED records with their `spent_by_txid` evidence — then `StoreUTXOsLocked` → `StoreUTXOSnapshot` (:1086-1172) REPLACES the `/utxo/<address>/` prefix on disk with the empty snapshot. The rebuild (`QueryTransactions`, TransactionManager.cpp:2503-2533) replays surviving history; a retained producing tx re-creates its outputs READY via `PutUTXO` (:156-226) — and with the consumed records gone, `PutUTXO`'s existing-entry duplicate check (:166-172) has nothing to find: **a spent output whose spender was pruned comes back READY. That is the resurrection vector.** The closure: keep CONSUMED entries (with `spent_by_txid`) alive across the clear (a `ClearUnspentKeepConsumed`-shaped UTXOManager API — one home, the lifecycle owner); then `PutUTXO`'s duplicate check refuses re-creation by construction, and an explicit `IsOutPointGenuinelyConsumed` crosscheck on every gap-filled READY UTXO is the belt-and-suspenders D-01 names. One derived hazard the planner MUST wire: with preserved CONSUMED entries, a replayed spender trips `ParseTransferTransaction`'s double-spend reject (:2639-2659) because the outputs-present idempotent-replay heuristic can never be satisfied after a clear — the rebuild must recognize **own replay** via `GetOutPointSpender(outpoint) == tx.GetHash()` (the durable evidence names this very tx as the spender) or every preserved-consume's spender errors on replay and its change outputs are lost.

**D-02's wedge is a state-machine conflation, not a nonce leak — and the safe boundary is `Commit`, not "Put".** Traced end-to-end: `FillDAGStruct` reserves the nonce at enqueue (:1581, `ReserveNextNonce` → `pending_nonces_`); a pre-broadcast failure (preflight witness/size, SendTransactionItem:2049-2085) fails before `crdt_transaction->Commit` (:2156) — no CRDT Put is durable before Commit (Puts are staged into an `AtomicTransaction` owned by this call); `TickOnce`'s error site (:566-593) marks the tx FAILED — and the FAILED transition **already releases the reserved nonce** (`ChangeTransactionState` FAILED case, :5196) — then enters SYNCING (:581). `SyncNonce` (:3487-3550) exits ONLY when `proposed == confirmed+1`; on the incident's fresh account no durable confirmed nonce exists anywhere and nothing was ever published, so the fallback chain (`GetConfirmedNonce` error → `GetLocalConfirmedNonce` error → bare `return`, :3497-3507) spins in SYNCING forever — the ~2s "Checking if my nonce is updated" loop, all sends rejected (sends run only in READY, :558-596; GeniusNode gates with "Transaction manager is not ready", GeniusNode.cpp:141). The wiring problem: `invalid_argument` and `message_size` are returned by BOTH preflight (:2060/:2071/:2082) and post-commit (:2175/:2187/:2205) failure classes, so the TickOnce site **cannot classify by error code today** — D-02 half 1 needs a distinguishable pre-broadcast signal from `SendTransactionItem` (extend `TransactionManager::Error`, hpp:85, or an equivalent typed shape). Half 2 (bounded SYNCING exit) is collision-safe by construction if the fallback never touches `pending_nonces_`: in-flight reservations stay held, `GetNextNonceLocked` (GeniusAccount.cpp:915-921) skips them, and the next send queues BEHIND anything still in flight.

**Everything else is composition on landed, audited surfaces.** The five-consumer audit (SC-5) finds: broadcast intake terminal (18-01, do not re-prove per D-C3); `RetryDueFailedRoots` bounded (8 attempts) but NOT pruned-aware — it re-enqueues a mid-flight-pruned root for 8 doomed fetch attempts (a small isResolved guard mirroring D-02 makes it terminal, and it also burns fetch-attempt counter ticks that SURV-01's zero-fetch assertion would catch); `RetryStalledDeltas` local-only and bounded (no fetch path at all); certificate work-journal recovery pruned-safe by the never-prune-certs invariant plus the cert-embedded-subject fallback (`OnConsensusCertificate`, TransactionConsensusHandler.cpp:61-109, approves from the certificate alone when both local lookups miss); `missing_tx_hashes_` bounded (12-attempt cap, 18-landed) but not marker-aware — a locally-pruned creating tx still burns 12 account-messenger requests before dropping (a `LookupPrunedEntry` consult at the :3330-3334 insertion makes it zero-attempt, GATE-05's "pruned ⇒ not relevant" applied literally); `AccountMessenger`'s tx-serving leg degrades gracefully on a `GetTransactionCID` miss (empty signed BlockResponse, debug log, no crash — AccountMessenger.cpp:618-760). SURV-02's requester-side failover plumbing already exists (`getNode`'s route-peer walk with per-peer CID-failure accounting and next-peer failover, graphsync_dagsyncer.cpp:206-425; routes register from broadcasts via `AddMultiCIDInfo`, pubsub_broadcaster_ext.cpp:626-667) — the phase supplies the TESTS against real pruned nodes, not new mechanism.

**Primary recommendation:** Three surgical production changes — (1) D-01: a consumed-preserving clear in UTXOManager + the own-replay recognition in the rebuild path + the marker consult at the missing-set insertion; (2) D-02: a distinguishable pre-broadcast error class that keeps the TM in READY, plus a bounded SYNCING exit anchored to the last durable confirmed nonce (ForTest seam per the `SetMissingTxMaxAttemptsForTest` precedent); (3) D-03: a TM-owned tri-state lookup (`PruneEligibility::PrunedLookup` reused verbatim as the verdict vocabulary) surfaced through a GeniusNode passthrough and the example CLI — each proven by targeted tests, the SC-1 restart matrix on the `TxPrunerTest`/`ReopenStackOnSamePath` stack with a real `StartCore` boot, the SC-3 refusal/failover pair on the address-advertised EraseNodeCollection topology, and the D-04 three-node mesh.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked carry-forwards (do not re-litigate)
- **D-C1:** GATE-05's BOOT half landed in Phase 18 (bounded recovery, attempt-capped drop, `SetMissingTxMaxAttemptsForTest` seam; regression `MissingCreatingTxDoesNotWedgeBoot`) — this phase owns the CHECKPOINT-REBUILD half only
- **D-C2:** Harnesses exist: the 18-01 two-node real-pubsub Redelivery fixture, the `tx_pruner_test` CRDTFixture family, `TransactionManagerPendingLifecycleTestAccess`, `GlobalDBEraseTest::EraseNodeCollection`
- **D-C3:** The isResolved broadcast guard (18-01) already makes re-broadcast + restart-head-exchange terminal for pruned roots — SC-5's audit checks the OTHER consumers against the same invariant, not re-proving broadcast
- **D-C4:** SC-2's strengthened wording (missing OR unresolvable creating tx; no unbounded re-request loops) is the requirement of record — live-incident-backed
- **D-C5:** Substrate invariants: compose lock, marker batch-join, sweep-as-cycle, seal retention, dev-mint policy, tick gate

### Locked Decisions (this phase)
- **D-01:** Rebuild + consumed-crosscheck. On checkpoint mismatch with pruned-below evidence: (a) the clear-and-rebuild replay SKIPS missing/pruned transaction refs (treating them as not-relevant per GATE-05 semantics), and (b) every gap-filled READY UTXO is cross-checked against the durable consumed-outpoint records (`IsOutPointGenuinelyConsumed` semantics) — an outpoint whose consuming record exists durably is NOT re-created. The durable UTXO set is authoritative; rebuild is gap-filling only; the double-spend window closes by construction. Rejected: refuse-and-resync-from-full (needs a peer protocol that doesn't exist and a reachable full node).
- **D-02:** Rollback + bounded exit. (1) Pre-broadcast failure (witness-build/size-check — the error classes that fire BEFORE any CRDT Put or proposal side effect) rolls the expected-next nonce baseline back to the last durable value: nothing was published, so the baseline advance was a promise never kept — the next send reuses the nonce cleanly. (2) Bounded SYNCING exit: if no confirmation advances the nonce within a bounded window (knob, sane default), fall back to the last DURABLE confirmed nonce, log loudly, return to READY — the GATE-05 durable-state-authoritative principle applied to the send side. Fix wiring point: the `ChangeState(SYNCING)` error path at TransactionManager.cpp:~581 + `SyncNonce` (:3487).
- **D-03:** Tri-state at the tx-lookup path: found / not-found / pruned (backed by `LookupPrunedEntry` from 17-05). A distinguishable outcome in the owning module's error set; the example `ds` command (and local query paths) surface "history pruned" distinctly. NO RPC/transport mapping this phase (deferred).
- **D-04:** Extend the 18-01 two-node Redelivery fixture to THREE real-pubsub GlobalDB nodes: two light peers that prune + one full node that serves. Reuses the proven wiring (per-test temp dirs, real topics, deterministic teardown). A fresh light bootstraps history the lights no longer have; the full node serves it; E2E green = SC-4.

### Claude's Discretion
- The recovery-audit deliverable shape per consumer (targeted test per consumer is the bar; exact harness per consumer follows existing patterns)
- The bounded-SYNCING window knob name/default and where the fallback logs
- Baseline-rollback placement (inline in the error path vs a small helper)

### Folded Todos
- **failed-send-wedges-tm-in-syncing** (P2, `resolves_phase: 19`): fixed by D-02. Close on landing.

### Deferred Ideas (OUT OF SCOPE)
- Refuse-and-resync-from-full rebuild — rejected (D-01)
- RPC/API-layer PRUNED mapping — deferred (D-03)
- PRUNED answers for peers' *query* protocols beyond graphsync refusal — 18-01 already covers the graphsync half; richer peer-query semantics later
- Reviewed Todos (not folded): `bridge-race-not-all-11-mint-within-window` (P1), `bridge-startup-wiring-mock-rpc` (P1) — bridge scope, untagged
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| GATE-05 (residual: checkpoint-rebuild half) | Boot-time UTXO assembly/checkpoint rebuild treats a missing OR unresolvable creating transaction as not relevant — skips it, completes boot, no unbounded re-request loops, never resurrects spent outputs | The mismatch branch mapped end-to-end (:3261-3289); the clear-wipes-consumed-records fact traced to `StoreUTXOSnapshot`'s prefix replace; the resurrection vector (retained producer + pruned spender) and its by-construction closure (preserved CONSUMED entries + `PutUTXO` duplicate check); the own-replay false-reject hazard in `ParseTransferTransaction` identified with its fix (`GetOutPointSpender == own hash`); marker consult at the missing-set insertion kills the 12 doomed requests |
| SURV-01 | Restart matrix on a pruned node — READY with zero pruned-CID fetch attempts, new-tip sync, spend/receive, correct outgoing nonce chain | Fetch-attempt sources enumerated for restart (heads exchange → D-02 guard; in-memory queues empty on restart; missing-tx requests → marker consult); harness = `TxPrunerTest` + `ReopenStackOnSamePath` + real `StartCore` boot with the READY-wait options analyzed; nonce-chain assertion rides the landed GATE-04 ladder (`GetOutgoingPreviousHash` :1690-1756, cert-slot rung never pruned) |
| SURV-02 | Local queries answer "history pruned" distinctly; peer requests get clean refusal with prompt requester failover — both tested against a pruned node | `PrunedLookup` tri-state enum + `LookupPrunedEntry` signature verified (PruneEligibility.hpp:115-125, :421-422); lookup call sites enumerated (`FetchExactTransactionFromCRDT` :3695, `ResolveTransactionPathAndCID` :4639, `GetTransactionByHash` :3660); requester failover machinery verified (`getNode` route walk :228-425, `AddMultiCIDInfo` route registration); refusal test shape = pruned server + requesting node + fetch-attempt/status observables |
| SURV-03 | Fresh light bootstraps through a pruned mesh (full node serves) — E2E | D-04's 3-node extension of `EraseNodeCollection` (addNode/makeNode/connectNodes/restartNodePendingStart all verified extensible to N nodes); the route-discovery insight: routes register from BROADCASTS, so the full node must (re)broadcast the history heads on the shared topic for the fresh node to hold a serving route — the load-bearing topology detail |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **C++17 only** — no C++20; Allman braces; brace all control statements; `enum class`; explicit casts; `constexpr` over macros; `.hpp/.cpp`; GNUS naming; Doxygen file headers + documented public functions.
- **`outcome::result<T>`** for fallible operations — the tri-state lookup, the crosscheck, the bounded-exit decision return results; never swallow failures; loud WARN/INFO logs where GATE-05 semantics intentionally continue.
- **One owning module per domain rule** — the consumed-record lifecycle stays in `UTXOManager`; the rebuild orchestration in `TransactionManager` (it owns boot assembly); the PRUNED-evidence vocabulary stays `PruneEligibility::PrunedLookup` (its one home, 17-landed); the SYNCING state machine stays `TransactionManager`; GeniusNode/NodeExample only wire/map text.
- **Never duplicate logic** — no second pruned-lookup enum; no second "is this outpoint spent" rule (extend `UTXOManager`'s accessors, don't shadow them); the tri-state surfaces ONE vocabulary everywhere.
- **No domain logic in the transport layer** — D-03 explicitly defers RPC mapping; NodeExample maps verdict→text only; AccountMessenger untouched this phase.
- **Refactor first, then change** — the consumed-preserving clear is a behavior-preserving refactor OF THE CLEAR (a new UTXOManager API; `SetUTXOs` untouched for other callers) landing before the rebuild-guard change that consumes it.
- **Verification is a gate** — clang-format/clang-tidy on changed files, build affected targets, run relevant tests; never claim a check passed that was not run.
- **Build-system pre-PR checklist** applies if CMakeLists changes (expected: new test files in existing `addtest` targets only; no new production libraries).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Consumed-record preservation across clear | `UTXOManager` (new `ClearUnspentKeepConsumed`-shaped API) | — | The lifecycle owner; `SetUTXOs`'s wipe-everything semantics stay for existing callers; only the owner can vend the preserve discipline over `utxos_mutex_` + `StoreUTXOSnapshot` |
| Gap-fill crosscheck (D-01b) | `TransactionManager::InitializeUTXOs` rebuild branch | `UTXOManager::IsOutPointGenuinelyConsumed` / `GetOutPointSpender` | TM owns boot assembly; the spent-evidence reads go through UTXOManager's existing accessors (internally `shared_lock`'d) |
| Own-replay recognition in rebuild | `TransactionManager::ParseTransferTransaction` | `UTXOManager::GetOutPointSpender` | The double-spend reject's own rule; the fix discriminates spender-of-record == replaying tx |
| Pruned-marker consult at missing-set insertion | `TransactionManager::InitializeUTXOs` (:3330-3334) | `PruneEligibility::LookupPrunedEntry` (static, takes `GlobalDB&`) | The insert site's rule; the marker vocabulary's one home |
| Pre-broadcast failure classification | `TransactionManager::SendTransactionItem` (preflight block) → `TickOnce` error site | — | Only the callee knows pre- vs post-Commit; the site owns the state transition decision |
| Bounded SYNCING exit | `TransactionManager::SyncNonce` + SYNCING-entry timestamp | `GeniusAccount::GetLocalConfirmedNonce` (durable anchor) | The state machine's owner; the anchor is the account's durable nonce store |
| Tri-state tx lookup | `TransactionManager` (new query API over `FetchExactTransactionFromCRDT` + `LookupPrunedEntry`) | `PruneEligibility::PrunedLookup` (verdict type) | The tx-lookup owner; evidence vocabulary reused verbatim |
| Local surfacing (ds/CLI) | `GeniusNode` passthrough + NodeExample command | — | Presentation only; `GetTransactionStatus` (GeniusNode.hpp:967) is the precedent shape |
| Failed-root-retry pruned guard | `CrdtDatastore::RetryDueFailedRoots` | `GraphsyncDAGSyncer::isResolved` | The retry loop's owner; the same seal signal the landed D-02 guard uses |
| Restart matrix / E2E harnesses | `test/src/account/tx_pruner_test.cpp` family + `test/src/crdt/globaldb_erase_test.cpp` | — | D-C2's named harnesses; no new harness families |

## Standard Stack

**No new external dependencies, no vendored changes, no new production libraries.** Everything rides landed internal APIs (all verified this session; line numbers current as of today's develop):

### Core
| API | Location | Purpose | Why Standard |
|-----|----------|---------|--------------|
| `UTXOManager::SetUTXOs` / `StoreUTXOSnapshot` | UTXOManager.cpp:487-533 / :1086-1172 | D-01's substrate — and the hazard source: the clear + prefix-replace wipes CONSUMED records | The lifecycle owner; the preserve-clear API lands beside them under the same locks |
| `UTXOManager::PutUTXO` duplicate check | UTXOManager.cpp:156-226 (check :166-172) | The by-construction resurrection blocker once CONSUMED entries survive | Existing entry (any state) ⇒ `false`, entry untouched — no new mechanism needed |
| `UTXOManager::IsOutPointGenuinelyConsumed` / `GetOutPointSpender` / `IsOutPointConsumed` | UTXOManager.cpp:866-884 / :854-858 | D-01b's crosscheck + the own-replay discriminator | Internally `shared_lock(utxos_mutex_)`; "genuine" = CONSUMED ∧ amount≠0 (synthetic tombstones are amount-0 with `spent_by_txid`) |
| `UTXOManager::LoadUTXOs` / `LoadLatestCheckpoint` / `ComputeUTXOMerkleRoot` | UTXOManager.cpp:928-1066 / :1272+ / :891-921 | The boot inputs D-01 wraps | Root = merkle over READY only; checkpoints under `/utxo-checkpoint` — never pruned |
| `TransactionManager::InitializeUTXOs` / `InitTransactions` / `QueryTransactions` / `FetchAndProcessTransaction` | TransactionManager.cpp:3235-3338 / :3340-3423 / :2503-2533 / :2535-2590 | D-01's wiring sites | The 18-landed bounded recovery (:3363-3403) is the pattern the marker consult extends |
| `PruneEligibility::PrunedLookup` + `LookupPrunedEntry(db, tx_hash)` | PruneEligibility.hpp:115-125, :421-422 | D-03's verdict vocabulary + backing lookup (returns `pair<PrunedLookup, optional<PrunedTransactionRecord>>`) | 17-designed, 18-production-written (the erase batch stages the marker); comment literally names SURV-02 |
| `TransactionManager::Error` | TransactionManager.hpp:85-88 | D-02 half 1's error home (extend with a pre-broadcast condition + category) | The owning module's error set, per D-03/D-02 wording |
| `GeniusAccount` nonce store (`ReserveNextNonce`/`ReleaseNonce`/`GetProposedNonce`/`GetLocalConfirmedNonce`/`GetConfirmedNonce`) | GeniusAccount.cpp:915-1090, state hpp:403-406 | D-02's baseline mechanics | `pending_nonces_` under `nonce_mutex_`; `GetNextNonceLocked` skips held nonces — the collision-safety anchor |
| `TransactionManager::TickOnce` READY/SYNCING cases + `SyncNonce` + `RollbackTransactions` | TransactionManager.cpp:541-597 / :3487-3550 / :2222-2230 | D-02's two halves' wiring points | Verified at the CONTEXT-cited lines (drift: none — :581/:3487 exact) |
| `CrdtDatastore::RetryDueFailedRoots` / `RetryStalledDeltas` / `EnqueueRootCID` + fetch-attempt counter | crdt_datastore.cpp:1816-1851 / :1872-1931 / EnqueueRootCID (counter increments on the return-true branch) + `Get/ResetRootFetchAttemptsForTest` | SC-5 audit subjects + the zero-fetch observable | MAX_FAILED_ROOT_RETRIES=8 (crdt_datastore.hpp:623); counter is the 18-01 seam |
| `ConsensusManager::RecoverPendingCertificateWork` / `DispatchStalledCertificateEntryLocked` | Consensus.cpp:4557-4631 | SC-5 work-journal recovery audit subject | Cert keys only (`CERT_KEY_PATTERN`); certificates never pruned |
| `TransactionConsensusHandler::OnConsensusCertificate` | TransactionConsensusHandler.cpp:61-109 | SC-5 cert-consumer audit: cert-embedded fallback approves when both local lookups miss | The pruned-subject safety net — test target, no change expected |
| `AccountMessenger::HandleBlockLikeRequest` | AccountMessenger.cpp:618-760 (tx leg :671-686) | SC-5 AccountMessenger-after-map-deletion audit subject | `GetTransactionCID` miss ⇒ empty signed BlockResponse + debug log — graceful; no change this phase |
| `GraphsyncDAGSyncer::getNode` route walk + `AddMultiCIDInfo` | graphsync_dagsyncer.cpp:206-425 / pubsub_broadcaster_ext.cpp:626-667 | SURV-02 requester failover + SURV-03 route discovery | FAILED + non-connection status ⇒ `RecordCIDFailure` + next peer; routes come from broadcasts carrying PeerInfo |
| `TransactionManager::GetOutgoingPreviousHash` ladder | TransactionManager.cpp:1690-1756 | SURV-01's nonce-chain assertion vehicle | GATE-04's cert-slot rung is pruned-proof by design (comment names GATE-04) |

### Supporting
| API | Location | Purpose | When to Use |
|-----|----------|---------|-------------|
| `EraseTransactionEntryPruned` | TransactionManager.cpp:4404-4409 (shared helper above) | Sanctioned pruned-state driver for every 19 test | Setup step; marker joins the erase batch |
| `SetMissingTxMaxAttemptsForTest` / `missing_tx_max_attempts_for_test_` | TransactionManager.cpp:3223-3233, cap default 12 (hpp:775), cooldown 5s (hpp:771) | The ForTest-seam PRECEDENT for D-02's bounded-exit knob | Same static-atomic shape |
| `TxPruner::RunCycle` + `TxPrunerTest` fixture + `ReopenStackOnSamePath` + `ForceManagerReady` | test/src/account/tx_pruner_test.cpp:122-503 | SC-1 restart matrix base | Full GlobalDB+Blockchain+TM(Light) stack, reopen on same path, lifecycle ChangeState seam |
| `TransactionManagerPendingLifecycleTestAccess` | tx_pruner_test.cpp:75-82 (per-binary definition; owner binary `transaction_manager_pending_lifecycle_test`) | Driving READY/SYNCING deterministically for D-02 tests | D-02's RED/GREEN vehicle |
| `EraseNodeCollection` (addNode/makeNode/connectNodes/restartNodePendingStart) | globaldb_erase_test.cpp:81-238 | D-04's 3-node extension base | Address-advertised, one-direction dial, deterministic teardown |
| `TransactionManagerPendingLifecycleTestAccess` + `transaction_manager_pending_lifecycle_test` binary | test_bin verified present | D-02's test home | |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Preserved-CONSUMED clear (new UTXOManager API) | Snapshot-restore around existing `SetUTXOs` in TM | **Use the owner API** — a TM-side save/restore reaches into another module's state through its public vector API (loses `spent_by_txid` — `GeniusUTXO` does not carry it); only UTXOManager can preserve entries atomically under its lock |
| TM-level pre-broadcast error enum extension | Error-code comparison table at TickOnce site | **Use the typed error** — `invalid_argument`/`message_size` are AMBIGUOUS between preflight (:2060/:2071/:2082) and post-commit (:2175/:2187/:2205) classes; a code table would silently misclassify as the send path grows |
| Bounded SYNCING exit keyed on a wall timer member | ChangeState-callback timestamp | **Use a member set at the D-02 wiring site** — the state-change callback is a notification surface others own; the entry time is SYNCING-recovery's private fact (planner may choose the callback if cleaner — discretion) |
| New tri-state enum in TransactionManager | Reuse `PruneEligibility::PrunedLookup` | **Reuse** — hard invariant 2; the 17-designed enum IS the contract (`VALUE_PRESENT_INVARIANT_VIOLATION` included); TM wraps it in a result struct with the fetched tx |
| Extending `ds` dump output | New `tx <hash>` CLI command (GeniusNode passthrough) | **Prefer a hash-arg query** — `ds`→`CrdtSet::PrintDataStore` is a raw element dump (markers are raw keys, invisible there); a targeted lookup prints an unambiguous verdict; final shape is planner's |

**Installation:** none — zero registry installs. (Package audit: not applicable.)

## Package Legitimacy Audit

No external packages are installed, recommended, or required. All work lands in existing internal libraries (`transaction`, `account`, `crdt_*`) and existing test targets. Slopcheck gate: not applicable (zero registry installs) — carried convention from Phases 16/17/18 (all audited clean).

## Architecture Patterns

### System Architecture Diagram

```
 RESTART (SC-1 / GATE-05 / D-01)                     SEND PATH (D-02)
 ─────────────────────────────────                    ──────────────────────────────────
 boot: StartCore                                      EnqueueTransaction
   └─ InitializeUTXOs (:3235)                           └─ FillDAGStruct → ReserveNextNonce
        ├─ LoadUTXOs (/utxo/ — pruned nodes: intact)        (pending_nonces_ holds it)
        ├─ checkpoint mismatch? (:3263)                 SendTransactionItem (:2012)
        │    └─ CLEAR+REBUILD — D-01:                      ├─ preflight size/witness (:2049-85)
        │        ├─ NEW clear keeps CONSUMED                  │   fail BEFORE Commit ⇒
        │        │  records (spent_by_txid survives)          │   typed PRE_BROADCAST error
        │        ├─ QueryTransactions replays                  ▼
        │        │  surviving tx/ values                       └─ TickOnce error site (:566)
        │        │    ├─ producer replay → PutUTXO               ├─ pre-broadcast ⇒ rollback,
        │        │    │   existing CONSUMED entry ⇒ NOT             STAY READY (NEW — half 1)
        │        │    │   re-created (window closed)             ├─ other ⇒ ChangeState(SYNCING)
        │        │    └─ spender replay → own-replay             │            + FAILED +
        │        │        via GetOutPointSpender==hash           │            ReleaseNonce(:5196)
        │        │        (no false double-spend reject)         └─ post-Commit failure ⇒ SYNCING
        │        └─ gap-fill crosscheck:                              ▼
        │            IsOutPointGenuinelyConsumed per          SyncNonce (:3487) — half 2:
        │            rebuilt READY UTXO (belt)                proposed == confirmed+1 ⇒ READY
        └─ per-READY-UTXO creating-tx miss (:3330)           else… bounded window (knob):
             ├─ LookupPrunedEntry == PRUNED ⇒                 elapsed > bound ⇒ anchor to last
             │   skip (not relevant — NEW)                      DURABLE confirmed nonce, WARN,
             │                                                 return READY (pending_nonces_
             └─ else missing_tx_hashes_ ⇒                      untouched — held nonces stay
                 bounded recovery (18-landed, cap 12)           held; no collision) — NEW

 LOOKUP (D-03) + PEERS (SURV-02/03)
 ─────────────────────────────────────────────────────────────
 local query (ds/tx <hash>) → TM tri-state lookup
   tracked map hit ⇒ FOUND ─ FetchExactTransactionFromCRDT hit ⇒ FOUND
   all nets miss + LookupPrunedEntry PRUNED ⇒ "history pruned"
   all nets miss + NOT_FOUND ⇒ not found (distinct)

 pruned server: graphsync request for pruned CID ⇒ CID_NOT_FOUND refusal
 requester: getNode route walk (routes from broadcasts: AddMultiCIDInfo)
   peer1 (pruned light) FAILED/CID-specific ⇒ RecordCIDFailure ⇒ next peer
   peer2 (full node) serves ⇒ SURV-02 failover / SURV-03 bootstrap green
```

Entry points: boot → D-01 guard; enqueue/send → D-02; CLI lookup → D-03; broadcasts → routes → failover. A reader traces one restart (left), one failed send (middle), one pruned query/fetch (right).

### Recommended Project Structure

```
src/account/
├── UTXOManager.hpp / .cpp          # MODIFIED — consumed-preserving clear API (D-01's substrate)
└── GeniusNode.hpp / .cpp           # MODIFIED — tri-state lookup passthrough (thin, GetTransactionStatus shape)
src/transaction/
├── TransactionManager.hpp / .cpp   # MODIFIED — D-01 rebuild guard (marker consult + crosscheck),
│                                    #   own-replay recognition, D-02 (typed pre-broadcast error,
│                                    #   READY-preserving site, bounded SYNCING exit + ForTest seam),
│                                    #   D-03 tri-state lookup API
├── PruneEligibility.hpp            # UNTOUCHED — PrunedLookup/LookupPrunedEntry reused verbatim
└── TxPruner.hpp / .cpp             # UNTOUCHED
src/crdt/
└── impl/crdt_datastore.cpp         # MODIFIED — RetryDueFailedRoots isResolved guard (one check,
                                     #   mirrors the landed D-02 insertion)
example/node_test/NodeExample.cpp   # MODIFIED — the local surfacing (verdict → text)
test/src/account/
├── tx_pruner_test.cpp (or a new 19 file in the same target)  # SC-1 restart matrix, D-01 suites
├── transaction_manager_pending_lifecycle_test.cpp            # D-02 suites (the seam's owner binary)
test/src/crdt/
└── globaldb_erase_test.cpp          # EXTENDED — 3-node mesh (D-04), refusal/failover (SURV-02/03)
```

No new libraries; new test files join existing `addtest` targets.

### Pattern 1: Owner-vended consumed-preservation (D-01)
**What:** a `UTXOManager` API (shape: `ClearUnspentKeepConsumed(address)` or a `SetUTXOs` variant flag — planner picks the exact signature) that under `utxos_mutex_` drops READY/RESERVED entries and reservations but keeps CONSUMED entries (with `spent_by_txid`), then persists via the existing `StoreUTXOsLocked` (the snapshot naturally re-writes the address prefix with only the survivors).
**Why owner-vended:** `GeniusUTXO` (the public vector type) does not carry `spent_by_txid`; only UTXOManager can round-trip consumed evidence; a TM-side save/restore would silently lose the spender binding — the exact fact the crosscheck needs.

### Pattern 2: Evidence-first skips (GATE-05 semantics, three sites)
**What:** consult durable evidence BEFORE enqueuing recovery work: (a) `LookupPrunedEntry == PRUNED` at the missing-set insertion (:3330-3334) ⇒ skip (log INFO "creating tx pruned locally; UTXO set authoritative"); (b) `isResolved` in `RetryDueFailedRoots` before `DeleteCIDBlock`/`EnqueueRootCID` ⇒ skip (terminal); (c) the gap-fill crosscheck post-rebuild.
**Precedent:** the landed D-02 broadcast guard is the same shape (seal evidence consulted at the intake); `PrunedLookup`'s invariant-violation case reports loudly, never guesses (PruneEligibility.hpp:117-124).

### Pattern 3: Typed error classification at the send boundary (D-02 half 1)
**What:** the preflight block's failures return a distinguishable condition (extend `TransactionManager::Error` + its category, or an equivalent typed shape); the `TickOnce` site branches: pre-broadcast ⇒ rollback + pop + STAY READY + WARN (nothing durable changed; the FAILED transition already released the nonce at :5196); everything else ⇒ the existing SYNCING path.
**Why typed:** the same `std::errc` codes fire on both sides of `Commit` (:2156) — code comparison cannot classify; the callee is the only party that knows.

### Pattern 4: Bounded wait anchored to durable state (D-02 half 2)
**What:** record the SYNCING entry time at the wiring site; in `SyncNonce`, when the window (knob; `SetSyncingExitTimeoutForTest`-shaped seam, static-atomic precedent :3223-3233) elapses without the exit condition: WARN loudly, use `GetLocalConfirmedNonce()` as the anchor, `ChangeState(READY)`. NEVER touch `pending_nonces_` — held reservations make the next send queue behind in-flight nonces (`GetNextNonceLocked` skips them), which is the collision-safety argument.

### Pattern 5: One tri-state vocabulary (D-03)
**What:** a TM-level query (e.g. `LookupTransactionVerdict LookupTransaction(hash) const` returning `{FOUND, tx}` / `PRUNED, record}` / `NOT_FOUND`) composed from `GetTransactionByHashNoLock` → `FetchExactTransactionFromCRDT` → `LookupPrunedEntry`; `PrunedLookup` reused verbatim; GeniusNode passthrough + CLI text mapping are the only new surface.
**Why:** "found" already has two sources (tracked map, CRDT); the PRUNED distinction exists exactly once; RPC mapping deferred per D-03.

### Anti-Patterns to Avoid
- **TM-side save/restore of UTXO entries around the clear** — loses `spent_by_txid` (not in the public type); reaches through the owner's boundary.
- **A second pruned-lookup enum or a second "value present?" probe** — `PrunedLookup` + `LookupPrunedEntry` are the one contract; rebuilding the marker key by hand duplicates the key-layout fact (the key home is PruneEligibility).
- **Classifying pre/post-broadcast by comparing `std::errc` values at the TickOnce site** — ambiguous by construction (same codes both sides of Commit).
- **Releasing pending nonces in the bounded-exit fallback** — that is the nonce-collision bug the threat model asks about; reservations stay.
- **"Fixing" `RetryStalledDeltas` with a network fetch** — it is deliberately local-only (`GetNodeWithoutRequest`, :1900); adding fetch reintroduces the fetch-storm hazard.
- **Testing the restart matrix only through `ForceManagerReady`** — the seam bypasses `InitializeUTXOs`; SC-1 must run the REAL boot at least once (see Validation).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Pruned-evidence lookup | A second marker probe / enum | `PruneEligibility::LookupPrunedEntry` | The 17-designed, 18-production contract incl. the invariant-violation case |
| Spent-evidence reads | New spent-state queries | `IsOutPointGenuinelyConsumed` / `GetOutPointSpender` | UTXOManager's existing atomic accessors |
| Resurrection blocking | A post-rebuild "un-spend" pass | Preserved CONSUMED entries + `PutUTXO`'s duplicate check | The check exists (:166-172); preservation makes it load-bearing by construction |
| Fetch-attempt observability | New counters | `Get/ResetRootFetchAttemptsForTest` | The 18-01 seam, already proven in anger |
| READY/SYNCING driving in tests | Sleeps/waits on network | `TransactionManagerPendingLifecycleTestAccess::ChangeState` | D-C2's named seam; deterministic |
| Pruned-state setup | Manual RocksDB surgery | `EraseTransactionEntryPruned` | The sanctioned driver (marker joins the batch) |
| Multi-node topology | A new network harness | `EraseNodeCollection` + `makeNode`/`connectNodes` | Proven address-advertised wiring (18-01's three auto-fixes are baked in) |

**Key insight:** Phase 19 adds three small production mechanisms (preserve-clear, typed pre-broadcast error + bounded exit, tri-state lookup) and one one-line guard (`RetryDueFailedRoots`); everything else is tests proving landed invariants hold under pruning.

## Common Pitfalls

### Pitfall 1: The own-replay false reject (D-01's subtlest edge)
**What goes wrong:** preserving CONSUMED entries makes `ParseTransferTransaction`'s double-spend check (:2639-2659) reject the REPLAY of the legitimate spender during rebuild — `IsOutPointConsumed` is true (preserved) but the outputs-present heuristic can never pass (they were just cleared), so the rebuild errors the spender and loses its change outputs.
**How to avoid:** in the rebuild path, treat `GetOutPointSpender(input) == tx.GetHash()` as proof of own replay (proceed to re-apply consume + produce); keep the reject for any other spender-of-record.
**Warning signs:** "Rejected double-spending transfer" ERROR logs during a clean rebuild; missing change UTXOs after checkpoint rebuild in tests.

### Pitfall 2: Cross-checking against evidence the clear just destroyed
**What goes wrong:** implementing D-01b's crosscheck without the preserve-clear — `IsOutPointGenuinelyConsumed` reads the in-memory map the clear just wiped; the crosscheck vacuously passes and the window stays open.
**How to avoid:** the preserve-clear lands FIRST (its own commit, behavior-preserving for existing callers), the rebuild guard second; the D-01 test must assert `spent_by_txid` survives the clear on disk (`/utxo/` raw read).
**Warning signs:** a D-01 test that passes with the crosscheck reading zero consumed entries.

### Pitfall 3: Synthetic tombstones misread as genuine spends
**What goes wrong:** `ConsumeUTXOs` synthesizes amount-0 CONSUMED tombstones with `spent_by_txid` for outpoints it never found (:296-306, the not-found branch); a crosscheck that treats any CONSUMED as a genuine spend, or one that treats `spent_by_txid` presence as genuineness, misclassifies.
**How to avoid:** `IsOutPointGenuinelyConsumed` (CONSUMED ∧ amount≠0) is the evidence-quality discriminator; use it for the "genuinely" wording and log the distinction; for pure blocking purposes any CONSUMED entry suffices (a tombstone still blocks re-creation).
**Warning signs:** crosscheck code re-deriving genuineness instead of calling the accessor.

### Pitfall 4: The bounded-exit fallback that creates a nonce collision
**What goes wrong:** "fall back to the last durable nonce" implemented as resetting/releasing pending nonces — a still-in-flight published tx's nonce gets handed to the next send; two txs compete for one nonce.
**How to avoid:** the fallback changes ONLY the sync anchor and the state; `pending_nonces_` untouched; the next `ReserveNextNonce` skips held nonces by construction. Test: fallback fires with a pending reservation ⇒ the next reserved nonce is strictly greater.
**Warning signs:** any `ReleaseNonce` call appearing in the half-2 diff.

### Pitfall 5: Restart-matrix READY proven the wrong way
**What goes wrong:** driving the matrix entirely through the lifecycle seam — `ForceManagerReady` bypasses `InitializeUTXOs`, so the test proves nothing about pruned creating-tx handling at boot.
**How to avoid:** at least one matrix leg runs the real `StartCore` boot and waits bounded for READY. NOTE: on a peerless fixture `CheckNonce`'s network fetch fails for Light nodes (CheckNonce :3425-3485 returns false on fetch error unless replicating) — options: (a) two-node topology so a nonce responder exists, (b) pre-seed a local confirmed nonce so the "trusting local nonce" path (:3462-3466) or local comparison passes, (c) small wait-for-INITIALIZING-progress assertions + seam for the READY leg. The marker-consult assertion (zero RequestTransaction attempts for pruned refs) does not depend on READY.
**Warning signs:** matrix tests with no `StartCore` occurrence; READY asserted only via the seam.

### Pitfall 6: SURV-03 route discovery assumed automatic
**What goes wrong:** the fresh node knows only the pruned light (its head broadcast delivered the pruned CID) — the fetch fails over to… no other route; the bootstrap fails or rides the 8-attempt retry ladder.
**How to avoid:** the full node must ALSO broadcast/re-broadcast the history heads on the shared topic (`BroadcastHeadsForTopics` precedent) so `AddMultiCIDInfo` registers the serving route on the fresh node; wire the topology so BOTH broadcasts arrive before the fetch window is measured.
**Warning signs:** E2E green only with retries/maximal timeouts; fetch-attempt counts far above 2.

### Pitfall 7: RetryDueFailedRoots guard placed after the counter
**What goes wrong:** the isResolved check after `EnqueueRootCID` — the counter already ticked; SURV-01's strict zero/delta assertions wobble.
**How to avoid:** check before `DeleteCIDBlock`/`EnqueueRootCID`; the skip logs and leaves the retry entry to age out (or clears it — planner's discretion, but the counter must not move).
**Warning signs:** fetch-attempt deltas of exactly the retry count in restart tests.

### Pitfall 8: Tri-state lookup racing the erase batch
**What goes wrong:** a lookup that reads value and marker in separate operations can observe the post-erase pre-… — actually impossible at the Phase-16 atomicity level (value XOR marker in ONE batch), but a lookup that reads the marker FIRST and the value SECOND can misorder on a concurrent erase.
**How to avoid:** read the VALUE first (the CRDT/raw get), then the marker — the batch-atomic invariant makes value-then-marker ordering safe (value present ⇒ FOUND regardless of marker state ⇒ report the invariant violation loudly per the 17 contract, never guess).
**Warning signs:** lookup code paths fetching the marker before probing the value.

## Code Examples

### D-01 preserve-clear + crosscheck (shape — signatures are planner's)
```cpp
// Sources: UTXOManager.cpp:487-533 (SetUTXOs), :1086-1172 (StoreUTXOSnapshot),
//          TransactionManager.cpp:3261-3289 (the mismatch branch)
// UTXOManager — the owner vends the discipline; SetUTXOs stays untouched:
outcome::result<void> UTXOManager::ClearUnspentKeepConsumed( const std::string &address )
{
    std::unique_lock lock( utxos_mutex_ );
    // drop READY/RESERVED + reservations; KEEP CONSUMED entries (spent_by_txid intact)
    ...
    return StoreUTXOsLocked( address );   // snapshot re-writes the prefix with survivors
}

// InitializeUTXOs mismatch branch — rebuild gap-fills; PutUTXO's duplicate check
// (:166-172) refuses re-creation of preserved CONSUMED outpoints BY CONSTRUCTION;
// the explicit crosscheck is the belt:
for ( /* each rebuilt READY outpoint */ )
{
    if ( GetOutPointSpender( txid, idx ).has_value() )   // consuming record exists durably
    {
        m_logger->warn( "Rebuild: outpoint ({},{}) has durable consume evidence - not re-created", ... );
        // do not create / revert entry to its preserved CONSUMED state; count + report
    }
}
```

### Own-replay recognition (the Pitfall-1 fix)
```cpp
// Source: TransactionManager.cpp:2639-2659 (the reject), :874-884 (GetOutPointSpender)
if ( std::any_of( inputs.begin(), inputs.end(), [&]( const InputUTXOInfo &input ) {
         return utxo_mgr.IsOutPointConsumed( input.txid_hash_, input.output_idx );
     } ) )
{
    // NEW: preserved consume evidence naming THIS tx is an idempotent rebuild replay
    const bool own_replay = std::all_of( inputs.begin(), inputs.end(), [&]( const InputUTXOInfo &input ) {
        auto spender = utxo_mgr.GetOutPointSpender( input.txid_hash_, input.output_idx );
        return spender.has_value() &&
               spender.value() == base::Hash256::fromReadableString( transfer_tx->GetHash() ).value();
    } );
    if ( !own_replay && !replayed )   // existing outputs-present heuristic stays for the normal path
    {
        return std::errc::invalid_argument;   // genuine double spend
    }
}
```

### D-02 half 1 — typed classification at the site
```cpp
// Sources: TransactionManager.cpp:2049-2085 (preflight), :2156 (Commit boundary),
//          :566-593 (TickOnce error site)
// SendTransactionItem preflight:
if ( !consensus_m_->BuildUTXOWitness( *transaction ).has_value() )
{
    return outcome::failure( Error::PRE_BROADCAST_VALIDATION );  // NEW typed condition
}
// TickOnce site:
if ( send_result.error() == Error::PRE_BROADCAST_VALIDATION )    // nothing durable changed
{
    m_logger->warn( "Pre-broadcast validation failed ({}) - rolling back, staying READY", ... );
    auto rollback_result = RollbackTransactions( tx_queue_m.front() );  // FAILED ⇒ ReleaseNonce (:5196)
    if ( !rollback_result.has_error() ) { tx_queue_m.pop_front(); }
    break;   // NO ChangeState(SYNCING)
}
```

### D-02 half 2 — bounded exit (shape)
```cpp
// Sources: TransactionManager.cpp:3487-3550 (SyncNonce), :3223-3233 (the seam precedent)
void TransactionManager::SyncNonce()
{
    if ( syncing_since_.has_value() &&
         std::chrono::steady_clock::now() - syncing_since_.value() > syncing_exit_timeout_for_test_ )
    {
        auto durable = account_m->GetLocalConfirmedNonce();   // last DURABLE anchor
        m_logger->warn( "SYNCING exit: no nonce confirmation within {}s - falling back to durable "
                        "confirmed nonce {}; returning to READY",
                        ...,
                        durable.has_value() ? durable.value() : 0 );
        syncing_since_.reset();
        ChangeState( State::READY );    // pending_nonces_ UNTOUCHED — held nonces stay held
        return;
    }
    ...  // existing logic
}
```

### D-03 — the tri-state lookup (shape)
```cpp
// Sources: TransactionManager.cpp:3660-3731 (both found-sources), PruneEligibility.hpp:115-125
struct TransactionLookupResult
{
    PruneEligibility::PrunedLookup             verdict;
    std::shared_ptr<GeniusTransaction>         tx;       // set iff FOUND
    std::optional<SGTransaction::PrunedTransactionRecord> record;  // set iff PRUNED
};
// FOUND: tracked map (GetTransactionByHashNoLock) OR FetchExactTransactionFromCRDT hit.
// All networks missed ⇒ LookupPrunedEntry(db, hash):
//   PRUNED ⇒ "history pruned"; NOT_FOUND ⇒ plain not-found;
//   VALUE_PRESENT_INVARIANT_VIOLATION ⇒ report loudly (17 contract), never guess.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Unbounded boot re-request loop on missing creating tx | Bounded recovery, attempt-capped drop | Phase 18 (pulled forward) | This phase adds the marker consult (zero attempts for locally-pruned) and the rebuild half |
| Boot half of GATE-05 only | + checkpoint-rebuild half (D-01) | This phase | GATE-05 closes |
| Nonce-recovery assumes every send eventually resolves | Pre-broadcast rollback + bounded SYNCING exit (D-02) | This phase | The live incident's wedge class dies; SC-5's audit gains the send-side consumer |
| not-found conflated with pruned at every lookup | Tri-state PRUNED answer at the TM surface (D-03) | This phase | SURV-02 local half; RPC mapping deferred |
| 2-node redelivery proof | 3-node pruned-mesh E2E (D-04) | This phase | SURV-03 |

**Deprecated/outdated:** none new this phase; note `RequestRelevantHeads` DOES exist (TransactionManager.cpp:3552 — the TM-side head request; 18-RESEARCH's "no such symbol" scoped to src/crdt, where the mechanism is `RebroadcastHeads`).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Puts staged into an `AtomicTransaction` returned by `BeginTransaction` have no durable/broadcast effect until `Commit` (so pre-Commit failures are side-effect-free even mid-batch) | D-02 | If staging has any durable effect, the pre-broadcast boundary moves; verify `AtomicTransaction::Put`/destructor during implementation |
| A2 | The FAILED transition's `ReleaseNonce` (:5196) plus the reservation discipline makes the nonce itself reusable after a pre-broadcast failure (the wedge is purely the state machine) | D-02 | If some path also advances `local_confirmed_`/`confirmed_nonces_` at send time, half 1 needs a genuine baseline rollback; grep `SetPeerConfirmedNonce` callers (found: confirmation paths only) |
| A3 | A light node's `/utxo/` records reload intact after pruning (pruning never touches `/utxo/` or `/utxo-checkpoint`) — restart WITHOUT mismatch takes the normal branch | D-01 / SC-1 | If any prune-era code writes `/utxo/`, the mismatch branch could fire on pruned nodes in the wild; the SC-1 matrix would surface it |
| A4 | `GetLatestNonce` from peers on a fixture answers (or cleanly no-answer) fast enough for a bounded REAL-boot READY wait | SC-1 harness | If the no-peer timeout is long, use the (b)/(c) READY options in Pitfall 5; measure first |
| A5 | The graphsync server refuses a pruned CID with a FAILED-request status the requester classifies as CID-specific (not connection) — verified plumbing-side (`RecordCIDFailure` branch), the on-the-wire status value not exercised this session | SURV-02 | If the refusal surfaces as request-not-found instead, the failover walk still works (the :293-322 branch blacklists/fails over) but the test's status assertion adjusts |
| A6 | Line numbers drift a few lines as Phase-19 edits land (files read at the cited lines this session) | throughout | Citation drift only; contracts re-verifiable by symbol |

**All other claims were verified against source this session** (file:line cited inline).

## Open Questions (RESOLVED)

1. **Does the D-01 preserve-clear also need the "pruned-below evidence" gate D-01's text mentions?**
   - What we know: preserving CONSUMED records is strictly safer unconditionally (consumed evidence is never wrong to keep); the evidence gate would only shape the LOG.
   - Recommendation: land the preserve-clear unconditionally (KISS); if the owner wants evidence-gated logging, the prune watermark (`TxPruner` status) is already queryable — cosmetic follow-up.
2. **Half-1 classification surface: extend `TransactionManager::Error` or a richer return type?**
   - What we know: `Error` exists (hpp:85, one value); errc codes are ambiguous across the Commit boundary.
   - Recommendation: extend `Error` + its error-code category (the outcome-pattern-native shape); planner finalizes per the category's current wiring.
3. **SC-1's real-boot READY strategy (Pitfall 5 options a/b/c).**
   - What we know: peerless CheckNonce fails-closed for Light on network error; succeeds on "network answered without nonce" or via local-confirmed paths.
   - Recommendation: measure first; prefer (b) pre-seeded local confirmed nonce (deterministic, no second node needed), fall back to (a) two-node.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Configured build tree | builds/tests | ✓ | `build/OSX/Release` — `tx_pruner_test`, `prune_eligibility_test`, `globaldb_integration_test`, `crdt_test`, `transaction_manager_pending_lifecycle_test`, `transaction_manager_certificate_fallback_test`, `utxo_manager_test`, `multi_node_finality_fault_test` all verified present | reconfigure |
| cmake / ctest | build/test | ✓ | 3.31.4 / 3.31.4 | — |
| clang-format / clang-tidy | repo verification gate | ✓ | `/opt/homebrew/opt/llvm@22/bin` (repo memory: keg-only paths + compile-db filter) | — |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** none.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest via repo `addtest` macro (auto ctest registration, auto `disable_clang_tidy`) |
| Config file | none — per-target registration (`test/src/account/CMakeLists.txt`, `test/src/crdt/CMakeLists.txt`) |
| Quick run command | `cmake --build build/OSX/Release --target tx_pruner_test && build/OSX/Release/test_bin/tx_pruner_test --gtest_filter='*Restart*'` |
| Full suite command | `cmake --build build/OSX/Release && ctest --test-dir build/OSX/Release --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| GATE-05/D-01 (preserve-clear) | Mismatch clear keeps CONSUMED records (incl. `spent_by_txid`) in memory and on disk; READY/RESERVED/reservations cleared; existing `SetUTXOs` callers byte-identical | unit (UTXOManager fixture) | `test_bin/utxo_manager_test --gtest_filter='*KeepConsumed*'` | ❌ Wave 0 |
| GATE-05/D-01 (no resurrection) | Rebuild after clear: retained producer + preserved consume ⇒ outpoint NOT READY (stays CONSUMED); spender replay re-applies without false double-spend reject; change outputs re-created | integration (TxPrunerTest stack + mismatch induction: tamper the READY set or checkpoint) | `test_bin/tx_pruner_test --gtest_filter='*Rebuild*'` | ❌ Wave 0 |
| GATE-05/D-01 (crosscheck) | Every gap-filled READY UTXO cross-checked; genuine-vs-tombstone distinction logged (amount≠0 accessor) | integration | same suite | ❌ Wave 0 |
| GATE-05 (marker consult) | Creating tx with PRUNED marker ⇒ zero RequestTransaction attempts, missing set stays empty, boot proceeds; unmarked miss ⇒ bounded requests as today (negative control) | integration + seam (attempt observable: existing request logs or a small counter seam) | `test_bin/tx_pruner_test --gtest_filter='*PrunedCreatingTx*'` | ❌ Wave 0 |
| SURV-01 (restart matrix) | prune → reopen (`ReopenStackOnSamePath`) → REAL StartCore → bounded READY wait → `GetRootFetchAttemptsForTest` delta 0 across the restart window → new-tip delivery processed → spend/receive → next outgoing tx nonce == expected and `previous_hash` == GATE-04 resolution of the pruned frontier | integration (the full matrix, double-run) | `test_bin/tx_pruner_test --gtest_filter='*RestartMatrix*'` | ❌ Wave 0 |
| D-02 half 1 (rollback) | Pre-broadcast failure (witness-build, size) ⇒ tx FAILED, nonce released, TM STAYS READY, next send reuses the nonce and succeeds; post-Commit failure still enters SYNCING (negative control) | unit + integration (pending-lifecycle seam; drive SendTransactionItem with a failing witness/oversized tx) | `test_bin/transaction_manager_pending_lifecycle_test --gtest_filter='*PreBroadcast*'` | ❌ Wave 0 |
| D-02 half 2 (bounded exit) | SYNCING with no confirming advance ⇒ WARN + READY within the (ForTest-shrunk) window; pending reservation survives ⇒ next reserved nonce strictly greater (collision proof); normal SYNCING→READY path unchanged (negative control) | integration (seam: `SetSyncingExitTimeoutForTest`) | `test_bin/transaction_manager_pending_lifecycle_test --gtest_filter='*SyncingExit*'` | ❌ Wave 0 |
| D-03 (tri-state local) | Lookup on pruned tx ⇒ PRUNED (record returned); on unknown ⇒ NOT_FOUND; on present ⇒ FOUND (both found-sources); invariant-violation case reports loudly; CLI/GreatNode passthrough maps verdicts | unit + integration | `test_bin/tx_pruner_test --gtest_filter='*TriState*'` (or transaction target) | ❌ Wave 0 |
| SURV-02 (peer refusal + failover) | Pruned server: graphsync request for pruned CID ⇒ refusal; requester: route walk fails over to the serving peer, fetch succeeds, pruned peer recorded as CID-failure (not blacklisted-as-connection); both against real pubsub nodes | integration (EraseNodeCollection, address-advertised) | `ctest --test-dir build/OSX/Release -R globaldb_integration` (new cases) | ❌ Wave 0 |
| SURV-03 / D-04 (pruned-mesh E2E) | 3 nodes (2 lights that prune + 1 full); fresh light bootstraps pruned-history heads; full node's route serves; E2E green; fetch-attempt accounting sane (Pitfall 6) | integration (real pubsub, double-run) | same target, `--gtest_filter='*PrunedMesh*'` | ❌ Wave 0 |
| SC-5 (RetryDueFailedRoots) | Root job failed → CID pruned → retry pass SKIPS it (fetch-attempt delta 0, entry terminal); unpruned failed root still retries (negative control) | integration (seam-driven failure + counter) | `ctest -R globaldb_integration` or crdt target | ❌ Wave 0 |
| SC-5 (RetryStalledDeltas) | Stalled delta whose block pruned ⇒ bounded give-up, zero fetch attempts (local-only path proven) | integration | same | ❌ Wave 0 |
| SC-5 (work-journal/cert recovery) | Cert redelivery for a subject whose tx is pruned ⇒ Approve via cert-embedded fallback (OnConsensusCertificate :61-109), no re-fetch; unfinished-entry recovery leaves no unbounded loop for pruned subjects | integration (cert-fallback family + pruned driver) | `test_bin/transaction_manager_certificate_fallback_test --gtest_filter='*PrunedSubject*'` | ❌ Wave 0 |
| SC-5 (missing_tx_hashes_) | Covered by the GATE-05 marker-consult case + the 18-landed cap regression (exists — `MissingCreatingTxDoesNotWedgeBoot`) | integration | existing + new | ✅ (extend) |
| SC-5 (AccountMessenger) | Pruned tx request served with empty signed BlockResponse (no crash, no error propagation); requester's bounded retry then drop | integration (messenger-level or EraseNodeCollection w/ account stack where practical; else the methods-wired unit shape) | planner places (see Open Q below) | ❌ Wave 0 |
| Containment | EraseSurfaceGate + PutRaw sanctioned-set greps stay green after the new TM/UTXOManager additions | gate tests | `test_bin/crdt_test --gtest_filter='*EraseSurfaceGate*'` | ✅ (verify) |

### Sampling Rate
- **Per task commit:** targeted `--gtest_filter` on the new/extended suites + clang-format/clang-tidy on changed files
- **Per wave merge:** `ctest --test-dir build/OSX/Release --output-on-failure` (full suite)
- **Phase gate:** full suite green + all new suites green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `UTXOManager` consumed-preserving clear API (+ utxo_manager_test cases)
- [ ] `TransactionManager::Error` pre-broadcast condition + category wiring (D-02 half 1)
- [ ] SYNCING-entry timestamp + bounded-exit knob + `SetSyncingExitTimeoutForTest` seam (D-02 half 2)
- [ ] TM tri-state lookup API + GeniusNode passthrough + NodeExample surfacing (D-03)
- [ ] Marker consult at :3330-3334 + a request-attempt observable if none suitable exists
- [ ] `RetryDueFailedRoots` isResolved guard (one check)
- [ ] Own-replay recognition in the rebuild path (with Pitfall-1 test)
- [ ] SC-1 matrix file/suite (real-boot leg included); D-04 3-node topology helper (EraseNodeCollection is N-ready; add the full-node role wiring)
- [ ] Mismatch-induction helper for D-01 tests (tamper the READY set or write a divergent checkpoint — planner picks; the checkpoint writer `CreateCheckpoint` is available)
- [ ] Framework install: none needed (gtest integrated)

## Security Domain

ASVS level 1 (config: `security_enforcement: true`, `security_asvs_level: 1`, `security_block_on: high`).

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|------------------|
| V2 Authentication | no | no new auth surface |
| V3 Session Management | no | — |
| V4 Access Control | yes | D-01 is the phase's integrity control: the rebuild may never re-authorize a spent output (spent-evidence preserved durably; re-creation blocked by construction + crosschecked) |
| V5 Input Validation | yes | D-03 validates lookup inputs against the marker contract (`VALUE_PRESENT_INVARIANT_VIOLATION` reported, never guessed); D-02 classifies failures before trusting the sync anchor; rebuild replays deserialize through the existing fail-closed `DeSerializeTransaction` |
| V6 Cryptography | no | existing hash/signature machinery untouched |
| V7 Error handling | yes | `outcome::result` throughout; GATE-05 skips are LOUD (INFO/WARN naming the pruned ref); the bounded exit WARNs; never silently swallow |
| V8 Data protection | yes (invariant) | `/utxo/` consumed evidence + `/utxo-checkpoint` + `/prune/` markers + seals remain outside every modification path this phase adds; the preserve-clear is the first new `/utxo/` writer shape — gated by its own tests |

### Known Threat Patterns for this stack
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Resurrect-spent-outputs via checkpoint rebuild (attacker induces a mismatch — e.g. corrupt one `/utxo/` record — then double-spends the re-created output) | Tampering/Elevation | D-01: consumed records survive the clear (memory+disk); `PutUTXO` duplicate check blocks re-creation; explicit crosscheck per gap-filled UTXO; own-replay discriminator keeps the false-reject from becoming a bypass (a rejected REPLAY must not delete the consume evidence) |
| Synthetic-tombstone confusion (tombstone with `spent_by_txid` but amount-0 misread as genuine spend evidence, or vice versa) | Tampering | `IsOutPointGenuinelyConsumed` is the one discriminator; tests cover both classes |
| Nonce collision via bounded-exit fallback (stale durable anchor while a tx is still in flight) | Tampering | Fallback never touches `pending_nonces_`; held nonces skipped by `GetNextNonceLocked`; collision-proof test (next reserved nonce strictly greater) |
| Fetch-storm / DoS via pruned-CID retry loops | DoS | `RetryDueFailedRoots` isResolved guard; `RetryStalledDeltas` stays local-only; existing 8-attempt caps; SURV-01 zero-fetch assertions |
| Unbounded re-request loops on unresolvable refs (SC-2 strengthened wording) | DoS | 18-landed 12-attempt cap (regression exists) + this phase's marker consult (zero attempts for locally-pruned) |
| Lookup conflation hiding pruning state (an attacker making "pruned" look like "never existed") | Repudiation/Information disclosure | Distinct tri-state with durable marker backing; invariant violation reported loudly |
| Wedged TM as availability attack (pre-broadcast failure ⇒ permanent SYNCING ⇒ node unusable) | DoS | D-02 both halves; live-incident regression test |

## Sources

### Primary (HIGH confidence — verified in this repo this session)
- `src/transaction/TransactionManager.cpp` — `TickOnce` send/error site :541-597 (SYNCING at :581, Rollback at :585); `SendTransactionItem` :2012-2220 (baseline :2029-2041, preflight :2044-2085, Commit :2156, post-commit failures :2158-2217); `RollbackTransactions` :2222-2230; `QueryTransactions` :2503-2533; `FetchAndProcessTransaction` :2535-2590; `PutProducedUTXOs` :2592-2605; `ParseTransferTransaction` double-spend check :2637-2660; `FillDAGStruct`/`ReserveNextNonce` :1577-1600; `InitializeUTXOs` :3235-3338 (mismatch :3261-3280, missing insert :3330-3334); `InitTransactions` :3340-3423 (cap 12 / cooldown 5s hpp:771-775); `CheckNonce` :3425-3485; `SyncNonce` :3487-3550; `RequestRelevantHeads` :3552-3580; `CheckTransactionValidity` :3585-3638; `GetTransactionByHash(NoLock)` :3660-3678; `FetchExactTransactionFromCRDT` :3695-3731; erase driver + marker staging :4355-4409; `GetTransactionCID`/`ResolveTransactionPathAndCID` :4633-4669; `ChangeState` :4617-4631; CONFIRMED/UNCONFIRMED/FAILED transitions :4990-5204 (ReleaseNonce :5127/:5196, mint cleanup :5158-5188); `GetOutgoingPreviousHash` ladder :1690-1756; missing-set seams :3223-3233
- `src/transaction/TransactionManager.hpp` — `Error` :85-88, `State` :93-99, `HasMissingTxHashes` :343-346, `missing_tx_hashes_` :763, init constants :771-775
- `src/account/UTXOManager.cpp` — `PutUTXO` :156-226; `DeleteUTXO` :228-263; `ConsumeUTXOs` incl. tombstone + owner-store :265-405; `RestoreConsumedUTXOs` :407-440; `SetUTXOs` :487-533; `IsOutPointConsumed`/`IsOutPointGenuinelyConsumed`/`GetOutPointSpender` :854-884; `ComputeUTXOMerkleRoot` :891-921; `LoadUTXOs` :928-1066; `StoreUTXOSnapshot` :1086-1172; `StoreUTXOs(Locked)` :1174-1184; `CreateCheckpoint` :1193-1270; `LoadLatestCheckpoint` :1272+
- `src/account/UTXOManager.hpp` — states :58, entry fields :84-85, `DB_PREFIX="/utxo"` :525, `CHECKPOINT_PREFIX` :527
- `src/account/GeniusAccount.cpp` — nonce store :822-941 (`SetPeerConfirmedNonce`, `RollBackPeerConfirmedNonce`, `GetNextNonceLocked` :915-921, `ReserveNextNonce` :929, `ReleaseNonce` :937); `FetchNetworkNonce` :977-998; `GetConfirmedNonce` :1000-1090; `RequestTransaction` :1159-1168; CID method wiring :649-657, :1225-1235
- `src/account/AccountMessenger.cpp` — tx-request handler :600-618; `HandleBlockLikeRequest` :618-760 (tx leg :671-686, empty-response shape :688-700)
- `src/crdt/impl/crdt_datastore.cpp` — DAG worker loop :95-137; `SeedNextExternalRoot` :184-207; failure handling :209-244; `HandleRootCIDBlock` :798-828; `CreateRootJob` :830-842; `GetLinksToFetch` :844-922; journal wiring :442-444; `MarkJobFailed(Locked)`/retry scheduling :1771-1814; `RetryDueFailedRoots` :1816-1851; `ScheduleStalledDeltaRetryLocked`/`RetryStalledDeltas` :1853-1931; `GetJobStatus` :1933-1948; `EnqueueRootCID` + counter + accessors (post-18-01 shape); `PrintDataStore` :2106-2109
- `src/crdt/crdt_datastore.hpp` — `MAX_FAILED_ROOT_RETRIES = 8` :623
- `src/crdt/impl/graphsync_dagsyncer.cpp` — `AddRoute` :168-186; `getNode` route walk :206-425 (deadline :230-240, FAILED branch :366-388, IN_PROGRESS bounds :389-425); `HasBlock` :524-530; CID_NOT_FOUND returns :485/:875
- `src/crdt/globaldb/pubsub_broadcaster_ext.cpp` — `AddMultiCIDInfo` route registration :626-667
- `src/crdt/impl/crdt_work_journal.cpp` — whole file (MarkSeen/MarkProcessing/MarkStalled/MarkDone, ListUnfinished, RecoverStaleProcessing, `/crdt/work/` v1 format)
- `src/blockchain/Consensus.cpp` — `RecoverPendingCertificateWork` :4557-4579; `DispatchStalledCertificateEntryLocked` :4581-4631; `ProcessCommittedCertificate` :4650-4728 (backoff :4697-4724)
- `src/transaction/TransactionConsensusHandler.cpp` — `OnConsensusCertificate` cert-embedded fallback :61-109
- `src/transaction/PruneEligibility.hpp` — `PrunedLookup` :115-125; `LookupPrunedEntry` :421-422; marker entry builder :424+
- `src/account/GeniusNode.cpp/.hpp` — `GetTransactionStatus` :967 + impl; "not ready" gate :141/:3385; `PrintDataStore` :3983-3987
- `src/crdt/globaldb/globaldb.cpp` — `PrintDataStore` :661-665
- `example/node_test/NodeExample.cpp` — `cmd_ds` :395-402, command table :514-528, dev-mint comment block :421-446
- Tests: `test/src/crdt/globaldb_erase_test.cpp` (EraseNodeCollection :81-238, Redelivery suite :527-731); `test/src/account/tx_pruner_test.cpp` (lifecycle seam :75-82, fixture :122-156, `ReopenStackOnSamePath` :451-503, crash matrix :1623-1907, suites list)
- Build tree verified: `build/OSX/Release/test_bin/` contains tx_pruner_test, prune_eligibility_test, globaldb_integration_test, crdt_test, transaction_manager_pending_lifecycle_test, transaction_manager_certificate_fallback_test, utxo_manager_test, multi_node_finality_fault_test; cmake/ctest 3.31.4; llvm@22 present

### Secondary (MEDIUM-HIGH confidence)
- `.planning/phases/18-*/18-{CONTEXT,RESEARCH}.md` + `18-01-SUMMARY.md` — substrate contracts, the D-02 guard's landed shape, the fixture's hard-won wiring lessons (address advertisement, one-direction dial, message-cache delivery windows)
- `.planning/phases/17-*/17-RESEARCH.md` — marker/lookup contract design (D-04), spender-pruned-first edge
- `.planning/todos/pending/failed-send-wedges-tm-in-syncing.md` + `completed/initializeutxos-boot-wedge-on-unresolvable-creating-tx.md` — the two live incidents; the completed todo's resolution note scopes this phase's GATE-05 residual exactly
- `.planning/milestones/v1.2-REQUIREMENTS-wip.md` — GATE-05 strengthened wording, SURV-01..03
- `.planning/ROADMAP.md` — Phase 19 entry, five success criteria

### Tertiary (LOW confidence)
- none used

## Metadata

**Confidence breakdown:**
- D-01 mechanics (clear wipes consumed records; resurrection vector; own-replay hazard): HIGH — traced through SetUTXOs → StoreUTXOSnapshot to the RocksDB prefix replace; the replay chain (QueryTransactions → FetchAndProcessTransaction → ParseTransaction → ConsumeUTXOs/PutUTXO) read in full
- D-02 mechanics (error taxonomy, boundary at Commit, SyncNonce exit analysis, collision safety): HIGH — every cited line read; the invalid_argument ambiguity across the Commit boundary is verified, not assumed
- Five-consumer audit: HIGH — each consumer read at its site; two genuinely non-terminal-enough behaviors found (RetryDueFailedRoots not pruned-aware; missing-set not marker-aware), both with one-check fixes
- Harness shapes: HIGH with A4/A5 caveats (real-boot READY timing; on-the-wire refusal status value)
- Security analysis: HIGH — the resurrect-spend and nonce-collision vectors are source-grounded

**Research date:** 2026-10-23
**Valid until:** 2026-11-22 (stable internal codebase; re-verify if `src/transaction/TransactionManager.cpp`, `src/account/UTXOManager.cpp`, or `src/crdt/impl/crdt_datastore.cpp` changes land first)

### Resolution Record (2026-10-23, adopted by the plans)
1. Preserve-clear gate: **unconditional preserve** adopted (19-01-T1 names the default); pruned-below evidence shapes logging only.
2. Half-1 classification surface: **extend TransactionManager::Error + category** adopted (19-02-T1).
3. SC-1 real-boot READY strategy: **(b) pre-seeded local confirmed nonce primary, (a) two-node fallback** adopted (19-06, measure-first gate).
