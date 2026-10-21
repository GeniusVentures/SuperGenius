# Phase 18: TxPruner Service & First Real Prune - Research

**Researched:** 2026-10-21
**Domain:** The first production erase service — TxPruner composing the Phase-16 erase substrate + Phase-17 evaluator into an idempotent, crash-consistent, mutex-serialized erase cycle; the seal-based re-delivery guard (D-02); the net-scoped dev-mint policy through full consensus (D-03); GeniusNode wiring behind default-off config; per-cycle observability + queryable status; the folded MintFunds burn-UTXO leak.
**Confidence:** HIGH (every load-bearing claim re-verified against this repo's source this session with file:line citations; the D-03 ctest-mint trace — the phase's biggest unknown — was followed end-to-end through the send preflight, the validator registry routing, the witness layer, and the ctest fixtures)

## Summary

**The D-03 ctest-mint trace is solved, and it is cleaner than the todo feared.** Burn-less ctest mints never "pass" the `BuildUTXOWitness` producer gate — the gate is never *reached*, because witness-building is conditional on the selected input validator's `RequiresConsensusUTXOData()`, which is `false` for every public-routed chain (`PublicChainInputValidator.hpp:190-193`). The routing (`TransactionManager::SelectInputValidator`, `TransactionManager.cpp:1942-1982`) sends any chain id that is not "supergenius"/"supergenius_chain"/registered to the public validator. The real ctest allowance is `test/testutil/TestMintInputValidator.hpp` — a test-only validator registered into the **process-global `IInputValidator` registry** for chain ids `"test"` and `"0"` by a file-scope static initializer at include time (`:48`); it returns `kValid` for every witness, `true` for every UTXO-param check, and requires no consensus UTXO data. `multi_node_finality_fault_test` then runs REAL consensus: the mint (chain id `"test"`, fabricated burn hash `'c'*64`) gets a hand-built commitment, an **empty** `UTXOWitness`, `CreateProposal` → `SubmitProposal` → genuine quorum votes → certificate through CRDT (`multi_node_finality_fault_test.cpp:1227-1249`). The owner's correction is exactly right: only the burn requirement is waived; the consensus is real. The live-node production twin is therefore mechanical: a net-gated dev-mint validator plus a net-scoped evidence bypass, keyed on `version::GetNetworkID()` (the process-global net id: MAIN 369 / TEST 963 / DEV 144 / STAGING 333, `sgns_version.hpp:12-15`), so a validator's OWN net — never the proposer's claim — decides whether burn-less mints certify.

**The live "global bypass" is precisely located.** The reachable unconditional bypass is `PublicChainInputValidator::GatherVerificationEvidence` (`PublicChainInputValidator.cpp:316-334`): a mint-v2 with an **empty** chain id routes to the public validator (empty + mint-v2 ⇒ routing chain "public", `TransactionManager.cpp:1947-1954`), skips the witness at send, passes the trivial type-rules check (`PublicChainInputValidator.cpp:42-64` — non-empty inputs/outputs only, no local UTXO consult), and then hits `chain_id.empty() ⇒ evidence.valid = true` at every validator on every net — certifiable on main with zero burn evidence. The `"supergenius"` leg of the bypass is normally UNREACHABLE (a "supergenius"-chain tx registry-routes to `GeniusInputValidator`, `GeniusInputValidator.cpp:113-120`, registered at `TransactionManager.cpp:72` file-scope init, which demands a real witness whose producer gate is what blocked the live dev mint — "Missing producer transaction", `TransactionConsensusHandler.cpp:1057-1064`). D-03's fix: net-scope BOTH bypass legs (empty source reference AND empty/supergenius chain id) behind the designated-net policy, and route live dev mints through a named dev validator whose `kValid` verdict is conditional on the validator's own net.

**Everything else is composition of landed, audited surfaces.** `GlobalDB::EraseEntry` (`globaldb.cpp:797-884`) is the guarded four-step compose with the `SetEraseInterruptHookForTest` seam; D-01 closes its documented WR-02 window by holding `CrdtSet`'s recursive `mutex_` (private, `crdt_set.hpp:378`) across guard→stage→commit — `PutElems` already holds it across build+commit (`crdt_set.cpp:761-806`), and `StageEraseElement`'s internal `lock_guard` (`:690`) is reentrant under an outer acquisition. D-02's guard site is verified: `HandleCIDBroadcast` (`crdt_datastore.cpp:677-772`) dedups on `HasBlock` (`:706-717`), which after a prune returns false (block gone) while `isResolved` stays true (seal retained — `HasBlock` reads the block store/LRU only, `graphsync_dagsyncer.cpp:524-530`; `isResolved` reads the seal, `:818-822`); both re-broadcast AND restart head exchange (`RebroadcastHeads`, `crdt_datastore.cpp:382-416, :1209+`) deliver CIDs into this ONE function, so one guard covers SC-3's two delivery modes. The phase's subtlest NEW correctness edge: **multi-transaction blocks and the K budget** — erasing one eligible tx deletes the shared delta block, and a sibling left behind by an exhausted K budget can then NEVER pass the block gate (parse fails closed on the missing block) — the cycle must group erases by block (defer a group that exceeds the remaining budget) and/or treat "block already gone" as erase-completing evidence.

**Primary recommendation:** Build `TxPruner` as a standalone service class in `src/transaction/` (constructor mirroring `PruneEligibility::New` over GlobalDB&/TransactionManager&/UTXOManager&/NodeType/PruningConfig, plus a mutex-guarded `PruneStatus` struct that GeniusNode's `GetPruneStatus()` delegates to), driven per-tick from the extended GeniusNode timer: gate (Light ∧ READY ∧ empty missing-set ∧ settle) → `Evaluate()` → for the first K eligible, block-grouped: UTXO spent-ness re-verify → CrdtSet compose-lock → guards → stage (4 steps + marker staging in the same batch) → per-tx commit → status update. D-02 inserts the `isResolved` check after the `HasBlock` miss. D-03 lands the `DevMintPolicy` (constexpr designated-net set {333, 144}, one home) + the net-scoped evidence bypass + a `"dev"`-chain validator gated on the validator's own net, and wires the MintFunds FAILED-transition cleanup (folded todo).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions (carry-forwards D-C1..C4 — do not re-litigate)
- **D-C1:** Substrate as landed: `GlobalDB::EraseEntry` four-step ordered flow (value → CID lookup → block-eligibility+delete → map entry), `PruneEligibility` spend-recency evaluator, block gate, marker contract (`/prune/tx/<hash>`, tri-state lookup, `WritePrunedEntryForTest`) — the marker WRITE joins this phase's erase batch (D-04 of 17-CONTEXT)
- **D-C2:** Tick gate: Light ∧ `State::READY` ∧ empty `missing_tx_hashes_` ∧ settle window; config default-off; evaluator runs on committed state only, never from CRDT callbacks
- **D-C3:** Zero-tombstone / seal-retention / one-batch discipline — gate-enforced (EraseSurfaceGateTest); `dry_run: true` keeps the Phase-17 observe-only behavior, `dry_run: false` arms the real erase
- **D-C4:** Generation discipline: per-tick service snapshot (SnapshotAccountServices), generation tag on summaries, no cached evaluator across account switches

### Locked Decisions (this phase)
- **D-01:** Mutex-serialized compose. The erase path holds the existing `CrdtSet` recursive `mutex_` across guard-check → staging → commit (the same discipline `PutElems` already uses across build+commit), plus a spent-ness re-verification under the UTXO lock immediately before staging (guards against a spend revert between evaluation and erase). The evaluator stays lock-free read-only; per-tick bounded work (D-04) keeps the hold short. This CLOSES the WR-02 check-then-act window by construction. Rejected alternatives: optimistic epoch checks, CRDT-worker-affine scheduling (pins the single DAG worker — the archive-replication-flake hazard).
- **D-02:** Seal-based dedup at the broadcast entry. After a `HasBlock` miss, `isResolved(cid) == true` ⇒ processed-and-pruned ⇒ skip. No new durable state — the never-erased seal markers are the dedup of record. **D-02b:** The prune cycle IS the sweep; each tick re-evaluates everything that exists; add-wins-resurrected entries are still spent + still old → re-erased; no separate sweep service.
- **D-03:** Burn-less mints go through FULL consensus (proposal → quorum → certificate) — the ctest model, replicated for the live node. Permitted ONLY when the node's net is designated (staging 333 + local dev nets); every validator REFUSES to certify unbacked mints on all other nets — test and main nets can never certify them regardless of proposer. NOT a bypass-consensus test-seam. Research MUST trace the exact ctest mint path and replicate its semantics under the net-scoped policy; the researcher decides the designated-net representation (config list vs constant) and the enforcement points.
- **D-04:** Bounded K per tick (config knob, `pruning.max_erases_per_cycle`, sane default ~100). NO cursor: the evaluator enumerates EXISTING transactions and erased ones vanish from enumeration — the backlog self-marks; each tick takes the first K eligible until drained; restart-safe by construction. A persistent `/prune/` cursor is built ONLY if the prefix scan measurably outcosts the erases (measure first).

### Claude's Discretion
- TxPruner class placement/shape (service in `src/transaction/` per the milestone research's owning-module map)
- Sweep-vs-tick logging detail; the queryable status surface shape (RECL-04 — local API vs simple getter; no RPC surface this phase)
- Exact config key names beyond the locked ones (`pruning.*` namespace, `max_erases_per_cycle`)

### Folded Todos
- **mintfunds-leaks-burn-utxo-on-consensus-reject** (P1): wire reservation/UTXO cleanup to the FAILED terminal transition (not just the exception path), mirroring the D-18 rollback contract.
- **genius-chain-witness-bypass-hardening** (P1): RESOLVED BY DESIGN here — D-03's net-scoping replaces the global bypass; update the todo to `resolves_phase: 18` and close it with the policy's landing.

### Deferred Ideas (OUT OF SCOPE)
- Durable pruned-CID record set — rejected (D-02: seals provide the state)
- Optimistic epoch locking / CRDT-worker-affine scheduling — rejected (D-01)
- Persistent backfill cursor — deferred until scans measure hot (D-04)
- RPC-facing prune-status surface — local queryable status only this phase (RECL-04); operator tooling later
- Bytes-reclaimed reporting — completes in Phase 20 with compaction (by design)
- Reviewed Todos (not folded): `initializeutxos-boot-wedge` (P1), `failed-send-wedges-tm-in-syncing` (P2) — Phase 19; `bridge-*` todos — untagged bridge scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PRUNE-04 | Idempotent, crash-consistent prune cycle — ordered erase flow reaches a fixed point across crashes and restarts | Per-tx atomic batch (Phase-16 substrate + marker staging joining it); per-tx commit with K bound (crash argument below); cycle-as-sweep idempotence (D-02b); interruption seam extension for service-level crash points; DB-reopen fixed-point assertions (Phase-16 test precedent) |
| PRUNE-05 | Re-delivered/resurrected entries re-pruned by the periodic sweep; root processing treats pruned/resolved CIDs as terminal — no re-fetch storm | D-02 guard site verified (`HandleCIDBroadcast` :677-772, `HasBlock` :706-717); one guard covers both re-broadcast and `RebroadcastHeads` head-exchange delivery; fetch-attempt observable needed (Wave 0); sweep = re-evaluation (D-02b) |
| RECL-03 | Per-cycle observability log — txs examined/erased, bytes reclaimed, watermark advance | Cycle log derived from the Phase-17 structured result + new erase outcomes; watermark = maxHeight at last completed cycle; bytes-reclaimed field completes in Phase 20 (SC-5 wording) — log the placeholder as not-yet-measured |
| RECL-04 | Prune status queryable — watermark + last-cycle stats | `PruneStatus` struct owned by TxPruner (mutex-guarded snapshot), `GeniusNode::GetPruneStatus()` delegating — the testable layer is the service, not the facade |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **C++17 only** — no C++20; Allman braces; brace all control statements; `enum class`; explicit casts; `constexpr` over macros; `.hpp/.cpp`; GNUS naming; Doxygen file headers + documented public functions.
- **`outcome::result<T>`** for fallible operations — the cycle, the guard, the policy checks, the status reads return results; never swallow failures (log-and-continue is sanctioned only where the evaluator's scan-continues contract already establishes it).
- **One owning module per domain rule** — the erase-cycle rule lives in TxPruner (`src/transaction/`); the marker key stays in the marker owner (PruneEligibility constants); the designated-net set has ONE home beside the net-id constants; the compose-lock discipline lives in the CRDT owner; GeniusNode only wires.
- **Never duplicate logic** — window arithmetic stays in `PruneEligibility::Evaluate` (TxPruner reuses verbatim); the allowlist stays `IsAllowlistedTxKey`; `SpendHeight`/window re-computation must NOT appear in TxPruner; the ctest-semantics replication must not fork `ValidateWitness` logic.
- **No domain logic in the transport layer** — nothing in gRPC/relay; the status getter is a local surface.
- **Refactor first, then change** — the D-01 lock acquisition is a behavior-preserving hardening of `EraseEntry`'s compose window (land separately from the first real-erase wiring if the diff mixes both); the GatherVerificationEvidence net-scoping changes behavior deliberately (its own change).
- **Verification is a gate** — clang-format/clang-tidy on changed files, build affected targets, run relevant tests; never claim a check passed that was not run.
- **Build-system pre-PR checklist** applies if CMakeLists changes (expected: new test files in existing `addtest` targets; possibly one new source file in an existing library target).
- **No `sh -c` in build commands, Python3 via `find_package(Python3 COMPONENTS Interpreter)`** — not expected to apply (no new codegen).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Prune-cycle orchestration (evaluate→erase→status) | NEW `TxPruner` service (`src/transaction/`) | GeniusNode (timer + wiring only) | Milestone research: "TxPruner must not be a GeniusNode lambda (god-class facade)"; the service takes the same dependency tuple as `PruneEligibility::New` and is unit-testable on CRDTFixture-family fixtures |
| Eligibility decision | `PruneEligibility::Evaluate()` (reused verbatim) | — | D-C1/D-C4; the window arithmetic's one home |
| Atomic per-tx erase + marker | `GlobalDB::EraseEntry` + marker staging fn | `TransactionManager::EraseTransactionEntry` (driver) | Landed substrate; the marker write joins the SAME batch (value XOR marker atomically) |
| Compose critical section (D-01) | `CrdtSet` (mutex owner) via a lock-scoped compose exposure on `CrdtDatastore`/`GlobalDB` | — | `mutex_` is private; only the owner can vend the discipline; `StageEraseElement`'s internal lock is reentrant under it |
| Spent-ness re-verify (D-01) | `UTXOManager` (`IsOutPointGenuinelyConsumed`/`GetOutPointSpender`, internally `shared_lock(utxos_mutex_)`) | — | Each call is atomic under the UTXO lock; UTXOManager holds no CRDT refs (17-RESEARCH verified) so no lock-order inversion with the CrdtSet mutex |
| Re-delivery guard (D-02) | `CrdtDatastore::HandleCIDBroadcast` | `GraphsyncDAGSyncer::isResolved` | The single broadcast intake; heads re-broadcast and head exchange both land here |
| Dev-mint net policy (D-03) | NEW `DevMintPolicy` (one home, beside `sgns_version` net constants) | `PublicChainInputValidator` (net-scoped bypass), NEW dev validator, `SelectInputValidator` routing | The policy predicate reads `version::GetNetworkID()` — the validator's OWN process-global net, not any proposer claim |
| Tick gate (Light ∧ READY ∧ empty missing ∧ settle) | TxPruner (checks) + GeniusNode (timer arm) | `TransactionManager::GetState()` (public), NEW missing-set accessor | `missing_tx_hashes_` is private (hpp:748-749) — Wave-0 accessor |
| Config surface | `PruningConfig::FromJSON` (one parse home) + `GeniusNode::LoadSgnsConfig` delegation | — | 17-landed pattern; new keys `max_erases_per_cycle` + settle knob |
| Queryable status (RECL-04) | TxPruner-owned `PruneStatus` (mutex-guarded) | `GeniusNode::GetPruneStatus()` (thin delegation) | GeniusNode is not unit-testable; the service is |
| MintFunds FAILED cleanup (folded todo) | `TransactionManager` (`ChangeTransactionState` FAILED case) + `UTXOManager::RollbackUTXOs` | — | The mint/rollback domain owners; reservation id = burn hash string (D-18 contract) |
| Crash-consistency proof | test suites (seam-extended) | — | SC-2; Phase-16 D-03 explicitly deferred process-kill-level proof to this phase's service criterion |

## The D-03 Trace (the phase's biggest unknown — solved)

> Every claim below was traced in source this session. This section is the answer to 18-CONTEXT D-03's mandated research: (a) the ctest mint path, (b) the enforcement-point map, (c) the designated-net representation and validator refusal.

### (a) How ctest mints certify — the exact path

**The three gates, as they exist today:**

1. **Send-side preflight witness gate** (`TransactionManager::SendTransactionItem`, `TransactionManager.cpp:2036-2057`, repeated post-commit at `:2131-2163`): for any tx with UTXO params, `BuildUTXOTransitionCommitment` is required — but it builds purely from the tx's OWN params (`TransactionConsensusHandler.cpp:919-975`; no local UTXO store reads — `multi_node_finality_fault_test`'s hand-built `CommitmentFor` proves the shape). The **witness** (`BuildUTXOWitness`) is built ONLY when `SelectInputValidator(tx).RequiresConsensusUTXOData()` is true.
2. **`BuildUTXOWitness` producer gate** (`TransactionConsensusHandler.cpp:978-1110`): per input — `GetUnconsumedUTXO(txid, idx)` must exist locally (`:1004`), then `owner_.GetTransactionByHash(input.txid)` must resolve the PRODUCER transaction (`:1057-1064` — the "Missing producer transaction" error), then the producer's outputs must contain the outpoint with matching payload. This gate fires ONLY for genius-routed txs — which is why the live dev mint (declaring the genius chain) was blocked at send, pre-broadcast.
3. **Validator-side gates** (every validator runs these on the proposal via `ConsensusManager::HandleProposal` → subject handler → `TransactionConsensusHandler::HandleNonceConsensusSubject`, `Consensus.cpp:2803-2972`, `TransactionConsensusHandler.cpp:312-513`): `ValidateWitnessForConsensus` (`:821-916` — witness requirement at `:881-889` conditional on the validator's `RequiresConsensusUTXOData()`; then `validator.ValidateWitness(...)` at `:897`) and `CheckTransactionTypeRules` (`:801-819` → `ValidateUTXOParameters`).

**The routing that decides everything** (`TransactionManager::SelectInputValidator`, `TransactionManager.cpp:1942-1982`), in order:
1. `public_chain_input_validator_.GetFirstRpcUrl(chain_id)` has wired RPC endpoints → public validator (`RequiresConsensusUTXOData() == false`, `PublicChainInputValidator.hpp:190-193`).
2. `IInputValidator::Get(chain_id)` — the **process-global registry**: `"supergenius"`, `"supergenius_chain"`, `""` → `GeniusInputValidator` (registered at `TransactionManager.cpp:72` file-scope init, `GeniusInputValidator.cpp:113-120`; `RequiresConsensusUTXOData() == true`); `"migration"` → migration validator.
3. `chain_id == "supergenius"` (or scoped `"supergenius/<id>"`) → genius validator.
4. Fallthrough → **public validator**.

**The ctest fixtures:**
- `multi_node_finality_fault_test` (real multi-peer consensus): `MintFor` builds `MintTransactionV2::New(42, "test", …)` with fabricated burn hash `'c'*64` (`:1097-1112`); the test includes `testutil/TestMintInputValidator.hpp` (`:23`), whose file-scope static (`TestMintInputValidator.hpp:48`) registers a validator for `"test"` and `"0"` in the global registry that returns `kValid`/`true`/no-witness-required for everything. The subject is hand-built with `CommitmentFor(mint)` and an **empty** `UTXOWitness`, then `CreateProposal` → `SubmitProposal` → real votes on all peers → certificate via CRDT (`:1227-1249`). **No witness is ever built on the proposer; validators approve because the registry-routed validator accepts.**
- `CertificateFallbackTest` (`transaction_manager_certificate_fallback_test.cpp:353-371, :378-449`): `MakeCompetingMintV2` uses chain `"source-chain"`; those mints mostly travel with fixture-built certificates (`BuildSignedCertificate`/`PersistCertificateAtSlot`) rather than the voting path.
- Live `MintFunds` (`TransactionManager.cpp:918-1063`): canonicalizes empty chainid to `"public"` (`:932-936`), inserts the burn UTXO locally (`:995-1002`, `UTXO_BRIDGE`), reserves it (`:1016-1030`), builds the commitment from params — passes send for public routing; dies at consensus when the witness layer demands real evidence.

**The live global bypass (what D-03 removes):** a mint-v2 with an **empty** chain id (constructed directly — `MintFunds` canonicalizes, but the tx type itself permits it) routes `"public"` → public validator → no witness at send, trivial type-rules, and at every validator `GatherVerificationEvidence(tx, source)` sees `tx.GetChainId() == ""` → `evidence.valid = true` unconditionally (`PublicChainInputValidator.cpp:326-334`; the sibling empty-`source_reference` bypass at `:316-323`). Certifiable on ANY net, including main, with a fabricated burn. The `"supergenius"` leg of the same bypass is normally unreachable (registry routes those txs to the genius validator first), unless someone wires RPC endpoints under the literal chain id "supergenius" — defense by routing accident, not policy: exactly the todo's point.

### (b) Enforcement-point map — every point that admits or blocks burn-less mints

| # | Point | Location | Today | Under D-03 |
|---|-------|----------|-------|------------|
| E1 | Send preflight witness requirement | `TransactionManager.cpp:2047-2056` | Conditional on routed validator | Unchanged (routing decides; dev mints ride a no-witness validator) |
| E2 | Send post-commit witness | `TransactionManager.cpp:2151-2162` | Same condition | Unchanged |
| E3 | `BuildUTXOWitness` producer gate | `TransactionConsensusHandler.cpp:1004, :1057-1064` | Genius-routed only; blocks fabricated inputs at send | **Unchanged** — native genius mints keep requiring real producers |
| E4 | `BuildUTXOTransitionCommitment` | `TransactionConsensusHandler.cpp:919-975` | Params-only; passes for any non-empty params | Unchanged (binding re-check on validators is the real gate, `HandleNonceConsensusSubject:347-372`) |
| E5 | Type rules, public routing | `PublicChainInputValidator.cpp:42-64` | Trivial accept (non-empty inputs/outputs) | Unchanged (public claims are unverifiable locally by design) |
| E6 | Type rules, genius routing | `GeniusInputValidator.cpp:122-155` → `UTXOManager::VerifyParameters` (`UTXOManager.cpp:681+`, `shared_lock(utxos_mutex_)`) | Demands real local outpoints + signatures | **Unchanged** — native strictness preserved on every net |
| E7 | Witness requirement at validation | `TransactionConsensusHandler.cpp:881-889` | Conditional on validator | Unchanged |
| E8 | **Evidence bypass** | `PublicChainInputValidator.cpp:316-334` (both legs: empty source ref; empty/"supergenius" chain id) | Unconditional, every net, released binary | **NET-SCOPED** — both legs additionally require `DevMintPolicy::BurnlessMintsPermitted()`; on non-designated nets an empty-chain mint with no RPC endpoints is REJECTED (closes the global bypass and the empty-chain_id sibling) |
| E9 | **Dev-mint validator (NEW)** | Production twin of `TestMintInputValidator`, registered for a designated chain id (recommend the literal `"dev"`) | — (ctest-only via `"test"`/`"0"`) | `ValidateWitness` returns `kValid` **iff the VALIDATOR's own net is designated**; `RequiresConsensusUTXOData() == false`. This is the explicit, auditable allowance — and the refusal: on test/main the same registration rejects |
| E10 | `MintFunds` | `TransactionManager.cpp:918-1063` | Burn UTXO insert + reserve; exception-only rollback | Unchanged mint path; + FAILED-transition cleanup (folded todo) |
| E11 | FAILED terminal transition | `ChangeTransactionState` (`TransactionManager.cpp:4784+`); fired by consensus rejection (`TransactionConsensusHandler.cpp:414-453` `reject_and_maybe_fail_local`) | No reservation cleanup | Wire `RollbackUTXOs(inputs, burn_hash, UTXO_BRIDGE)` to the mint FAILED transition (folded todo) |

**The "regardless of proposer" guarantee:** the policy predicate reads `version::GetNetworkID()` — a process-global set once from `sgns_config.json` `net_id` (`GeniusNode.cpp:411-416`, validated against the four known ids in `sgns_version.cpp:75-86`). A validator on net 963/369 computes `BurnlessMintsPermitted() == false` from ITS OWN net and rejects, whatever the proposer declared. Claiming net 333 means actually joining net 333's topics/keyspace (`/bc-333/`), so "spoofing the designated net" = running on the designated net — which is the policy's intent. Cross-net quorums do not form (nets are partitioned).

### (c) Designated-net representation — DECIDED by this research

**Compile-time constant set beside the net-id constants; NO config key.**

```cpp
// One home, beside sgns_version.hpp's net constants (the net-id domain owner):
/// Nets where burn-less dev mints may certify through full consensus (D-03).
/// Anything NOT in this set: validators refuse unbacked mints outright.
inline constexpr std::array<uint16_t, 2> kDevMintPermittedNets{ STAGING_NET_ID, DEV_NET_ID };
```

- `STAGING_NET_ID = 333` (staging) and `DEV_NET_ID = 144` (the DEFAULT net — a node with no `net_id` key runs 144, which is "local dev", `sgns_version.cpp:18`). Both owner-named requirements covered.
- **Why constant over config:** a config list is a per-node opt-in that WEAKENS validator refusal (a misconfigured test-net validator could enable certifying unbacked mints — the exact exposure D-03 exists to remove); a constant makes the refusal the default state of every released binary and the allowance a code-reviewed, greppable fact. Config cannot widen the set; only a code change can.
- The dev-mint CHAIN id (what the live mint declares so it routes to E9): recommend the literal `"dev"` — explicit, never collides with real public-chain ids from chainlist, and keeps the empty-chain-id path closeable outright (E8's empty leg becomes reject-on-non-designated + route-to-dev-validator nowhere, since "dev" is registered).
- The demo/staging mint flow becomes: `MintFunds(amount, burn_ref, "dev", token, dest)` on net 333 → public-ish routing to the dev validator → no witness at send → validators on 333 approve (`BurnlessMintsPermitted()` true) → real quorum → certificate → the mint's produced UTXOs land (`ParseMintTransaction`) → spend them → prunable history exists. On net 963 the identical call certifies nothing.

## Standard Stack

**No new external dependencies, no vendored changes, no new libraries.** Everything rides landed internal APIs (all verified this session):

### Core
| API | Location | Purpose | Why Standard |
|-----|----------|---------|--------------|
| `GlobalDB::EraseEntry(key, cid)` | `globaldb.cpp:797-884`, contract `globaldb.hpp:203-248` | The guarded four-step compose the TxPruner drives | D-C1; guards fail-closed before staging; post-commit seal anomaly distinct error; WR-02 window documented as caller-owned — D-01 closes it |
| `GlobalDB::SetEraseInterruptHookForTest` | `globaldb.hpp:250-258`, `globaldb.cpp:790-795` | Crash-injection seam (steps 1/2/3) | Extends for service-level crash points (SC-2); static, nullptr in production, destructor-cleared (EraseNodeCollection precedent) |
| `TransactionManager::EraseTransactionEntry` / `ResolveTransactionPathAndCID` | `TransactionManager.cpp:4277-4309` | Per-tx driver: network-consistent path+CID pair + revert-free tracked-map cleanup | Landed Phase 16; the TxPruner's per-tx entry point |
| `PruneEligibility::New/Evaluate/EvaluateDeltaBlockErasable` | `PruneEligibility.hpp:264-345` | The eligibility decision + block gate (reused verbatim) | D-C1; window arithmetic's one home |
| `PruneEligibility::BuildPruneMarkerKey` / `LookupPrunedEntry` / `WritePrunedEntryForTest` | `PruneEligibility.hpp:354-405` | Marker key home + tri-state lookup + test writer | The production marker STAGING joins the erase batch; `WritePrunedEntryForTest` remains for fixtures |
| `CrdtSet::mutex_` (recursive, private) + `StageEraseElement` | `crdt_set.hpp:378`, `crdt_set.cpp:669-737` (lock at `:690`) | D-01's compose lock; staging is reentrant under an outer hold | `PutElems` discipline (`:761-806`) is the precedent D-01 names |
| `UTXOManager::IsOutPointGenuinelyConsumed` / `GetOutPointSpender` | `UTXOManager.cpp:777-797` (per 17-audit; `shared_lock(utxos_mutex_)` internally) | Spent-ness re-verify immediately before staging (D-01) | Each call atomic under the UTXO lock; UTXOManager holds no CRDT refs → no lock-order inversion with the CrdtSet mutex |
| `CrdtDatastore::HandleCIDBroadcast` | `crdt_datastore.cpp:677-772` (`HasBlock` at `:706-717`) | The D-02 guard site | Single intake for re-broadcast AND `RebroadcastHeads` head exchange (`:382-416, :1209+`) |
| `GraphsyncDAGSyncer::isResolved` / `HasBlock` | `graphsync_dagsyncer.cpp:818-822` / `:524-530` | D-02's signal: seal consult vs block-store consult | `HasBlock` reads block+LRU only; `isResolved` reads the seal — after a prune: false + true = the skip signature |
| `TransactionManager::GetState()` / `missing_tx_hashes_` | hpp:330-333 / hpp:748-749 | Tick gate inputs | `GetState` public; missing-set PRIVATE — Wave-0 accessor |
| `GeniusNode::SnapshotAccountServices` / `AccountServiceSnapshot` | `GeniusNode.hpp:1022-1030`, `.cpp:4144+`; cycle pattern `GeniusNode.cpp:4913-4931` | Per-tick consistent generation snapshot (D-C4) | 17-landed CR-01 pattern; TxPruner constructed per tick or handed the snapshot |
| `StartPruneDryRunTimerIfConfigured` / `RunPruneDryRunCycle` | `GeniusNode.cpp:4862-4911` / `:4913-5020` | The wiring the real cycle extends; the `dry_run == false` WARN-return branch (`:4879-4883`) is replaced by the real arm | gc_timer_-style weak-self asio pattern; 1-min first tick / 10-min cadence; generation tag; error-class split (permanent stop vs transient retry) |
| `PruningConfig::FromJSON` | `PruneEligibility.hpp:210-239`, impl `:84-97` | One config parse home; gains `max_erases_per_cycle` + settle knob | 17-landed fail-closed chain (enabled-without-window disabled + WARN) |
| `version::GetNetworkID()` + net constants | `sgns_version.hpp:12-15`, `.cpp:18, :56` | D-03's net identity | Process-global, set from `net_id` config, validated against the four known ids |
| `IInputValidator` registry + `SelectInputValidator` | `InputValidators.hpp`, `GeniusInputValidator.cpp:113-120`, `TransactionManager.cpp:1942-1982` | D-03's routing substrate | The exact mechanism the ctest seam uses — the production twin registers the same way |
| `UTXOManager::RollbackUTXOs` | `UTXOManager.hpp:345-347` | Folded-todo cleanup vehicle | D-18 reservation contract; reservation id = burn hash string |

### Supporting
| API | Location | Purpose | When to Use |
|-----|----------|---------|-------------|
| `GlobalDB::GetRaw/PutRaw/QueryRaw` | `globaldb.hpp:174-196` | Marker assertions in tests; `/utxo/` byte-identity assertions (SC-1 "unspent set untouched") | Every service test |
| `GlobalDB::GetCRDTHeadList` | `globaldb.hpp:327` | maxHeight → watermark (RECL-03/04) | Per cycle |
| `GlobalDBEraseTest::EraseNodeCollection` | `test/src/crdt/globaldb_erase_test.cpp:73-161` | Real pubsub + GlobalDB multi-node fixture — the SC-3 harness base | Re-broadcast/head-exchange tests |
| `EraseSurfaceGateTest` | `test/src/crdt/crdt_erase_test.cpp:706-807` | Containment gates (banned tokens per tracked function) | MUST be updated: new tracked targets (marker staging, dev policy) — see Patterns |
| `CRDTFixture` family | `test/testutil/storage/base_crdt_test.hpp` | Service-level unit fixtures (real GlobalDB + pubsub + temp dir) | TxPruner tests |
| `CertificateFallbackTest` helpers | `transaction_manager_certificate_fallback_test.cpp:378-449` + helpers `:421-533` | Consensus-side fixtures (PersistTransaction/BuildSignedCertificate) | D-03 validator tests |
| `TestMintInputValidator` | `test/testutil/TestMintInputValidator.hpp` | The ctest seam being production-twin'd | Reference for E9's shape; stays for existing tests |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Per-tx commit, K-bounded | One batch for the whole cycle's K erases | **Use per-tx** — each tx's value/block/map/marker is one atomic batch (the landed substrate's shape); a K-sized mega-batch multiplies write-amp/latency and violates the bounded-batch pitfall; crash mid-cycle simply leaves the fixed point partially advanced, which re-run converges on |
| Block-grouped erase budgeting | Independent per-tx K consumption | **Use group-or-defer** — avoids stranding same-block siblings behind a deleted block (see Pitfall 3) |
| Constant designated-net set | Config list (`pruning`-adjacent or standalone key) | **Use constant** — config is a per-node opt-in that weakens validator refusal; only a code change can widen the allowance |
| Dedicated `"dev"` chain id for live dev mints | Reuse empty chain id + net-scoped bypass only | **Use `"dev"`** — explicit and greppable; lets E8's empty leg be closed outright instead of policy-laden |
| Extending `SetEraseInterruptHookForTest` | TxPruner-level `SetCycleInterruptHookForTest` | **Both, minimally** — the GlobalDB seam already covers mid-compose steps; add a cycle-level seam (between per-tx erases, before/after evaluation) rather than distorting the substrate's seam |
| TxPruner holds CrdtSet lock externally | Lock-scoped compose variant on GlobalDB/CrdtDatastore | **Use the lock-scoped compose variant** — `mutex_` is private; the owner vends the discipline (API shape makes the critical section impossible to forget); closes WR-02 for EVERY caller, not just TxPruner |
| True process-kill tests (fork + `_exit`) | Seam-simulated kill + DB reopen | **Seam-first, true-kill optional** — the Phase-16 D-03 deferral says process-kill belongs to this phase's criterion; a fork-based test is feasible (child opens DB, killed mid-cycle, parent reopens) but has portability cost (fork+threads caveats); land the seam matrix first, add one true-kill test if the harness cost is acceptable — planner decides |

**Installation:** none — zero registry installs. (Package audit: not applicable.)

## Package Legitimacy Audit

No external packages are installed, recommended, or required. All work lands in existing internal libraries (`transaction`, `account`, `crdt_*`) and existing test targets. Vendored dependencies untouched. Slopcheck gate: not applicable (zero registry installs) — carried convention from Phases 16/17 (both audited clean, AR-16-2/17 supply-chain notes).

## Architecture Patterns

### System Architecture Diagram

```
 sgns_config.json                         sgns_config.json
 pruning.{enabled=false,                  net_id=<333|963|369|144>
   finality_window=N, dry_run,            (process-global net identity — D-03 key)
   max_erases_per_cycle=K, settle}
        │                                        │
        ▼                                        ▼
 GeniusNode::LoadSgnsConfig ── PruningConfig::FromJSON    DevMintPolicy (constexpr nets {333,144})
        │  (absent keys ⇒ nothing constructed)                │
        ▼                                                    │
 ┌──────────────────────────────────────────────────────────────────┐
 │ TICK (timer, weak-self asio; per tick: SnapshotAccountServices)  │
 │  GATE: enabled ∧ Light ∧ State::READY ∧ MissingTxSet empty       │
 │        ∧ settle-window elapsed  ── any miss ⇒ skip tick          │
 │  (dry_run ⇒ Phase-17 report-only cycle, byte-identical)          │
 └──────────┬───────────────────────────────────────────────────────┘
            ▼ dry_run=false
 ┌──────────────────────────────────────────────────────────────────┐
 │ EVALUATE (reused verbatim, read-only): PruneEligibility::Evaluate│
 │   + EvaluateDeltaBlockErasable ⇒ eligible set + block verdicts   │
 │   generation-superseded? ⇒ tag + abort before any erase          │
 └──────────┬───────────────────────────────────────────────────────┘
            ▼ first K eligible, BLOCK-GROUPED (group-or-defer)
 ┌──────────────── PER-TX ERASE (repeat ≤ K) ───────────────────────┐
 │ 1. spent-ness RE-VERIFY under UTXO lock (fresh, per outpoint)    │
 │     any reverted ⇒ skip tx (reason recorded)                     │
 │ 2. ACQUIRE CrdtSet compose lock (D-01 — owner-vended)            │
 │ 3. guards (head/pending/LRU/resolved — re-checked inside)        │
 │ 4. STAGE one batch: CRDT entries → block bytes → map entry       │
 │     → /prune/tx/<hash> marker record  (value XOR marker)        │
 │ 5. COMMIT (single db->Write, sync) — INSIDE the lock             │
 │ 6. tracked-map cleanup (revert-free, landed driver)              │
 └──────────┬───────────────────────────────────────────────────────┘
            ▼
 status_.Update({watermark=maxHeight, examined, erased, held,
                 skipped, errors, duration, generation})   ← RECL-04
 INFO per-cycle summary                                    ← RECL-03

 ── D-02 GUARD (concurrent, every broadcast) ──────────────────────
 HandleCIDBroadcast(cid): HasBlock? ─yes─▶ skip (today)
        │no
        isResolved(cid)? ─yes─▶ SKIP (processed-and-pruned — NEW)
        │no                       (seal survives deletion ⇒ the
        ▼                         dedup of record; no new state)
   cache/pending checks → EnqueueRootCID (fetch)

 ── D-03 DEV-MINT POLICY (net-scoped, full consensus) ─────────────
 mint-v2 chain "dev" ─▶ SelectInputValidator ─▶ DevMintValidator
   proposer: no witness required (RequiresConsensusUTXOData=false)
   validator kValid ⟺ validator's OWN net ∈ {333, 144}
 GatherVerificationEvidence bypass (empty-source / empty-chain legs)
   ⟺ same net predicate — on test/main both legs REJECT
 FAILED mint transition ─▶ RollbackUTXOs(burn)   (folded todo)
```

Entry points: config → timer tick → gate → evaluate → per-tx erase; broadcast intake → D-02 guard; mint send/proposal → D-03 routing. A reader traces one prune: config → tick gate → evaluate → re-verify → lock → stage → commit → status; and one re-delivery: broadcast → HasBlock miss → isResolved hit → skip.

### Recommended Project Structure

```
src/transaction/
├── TxPruner.hpp / .cpp                     # NEW — the service: cycle orchestration,
│                                            #   tick-gate checks, block-grouped K budget,
│                                            #   per-tx re-verify + compose-lock driver,
│                                            #   PruneStatus (mutex-guarded) + watermark
├── PruneEligibility.hpp / .cpp             # MODIFIED — PruningConfig gains max_erases_per_cycle
│                                            #   + settle knob; NEW StagePrunedMarker(batch,…)
│                                            #   (the production marker staging — key home stays here)
└── TransactionManager.hpp / .cpp           # MODIFIED — public missing-set accessor (tick gate);
                                            #   EraseTransactionEntry unchanged (the driver);
                                            #   FAILED-transition burn cleanup (folded todo)
src/account/
├── DevMintPolicy.hpp (or in sgns_version-adjacent home)  # NEW — designated-net constant set
├── DevMintInputValidator.hpp / .cpp        # NEW — production twin of TestMintInputValidator,
│                                            #   kValid iff validator's net designated
├── PublicChainInputValidator.cpp           # MODIFIED — both GatherVerificationEvidence bypass
│                                            #   legs net-scoped behind DevMintPolicy
└── GeniusNode.hpp / .cpp                   # MODIFIED — real-cycle arm (replace the dry_run==false
                                             #   WARN-return), GetPruneStatus() delegation
src/crdt/
├── crdt_set.hpp (+ crdt_datastore/globaldb) # MODIFIED — compose-lock exposure for D-01
└── impl/crdt_datastore.cpp                  # MODIFIED — D-02 isResolved check after HasBlock miss
test/src/account/
├── tx_pruner_test.cpp (name TBD)            # NEW — service-level: SC-1/SC-2/SC-4/SC-5 + status
└── dev_mint_policy_test.cpp (or folded)     # NEW — D-03: designated-net certifies, others refuse
test/src/crdt/
└── (extend existing erase/globaldb suites)  # D-02 re-broadcast test (EraseNodeCollection harness)
```

No new libraries; new files join existing `addtest` targets (`test/src/account/CMakeLists.txt`, `test/src/crdt/CMakeLists.txt`).

### Pattern 1: Per-tx atomic batch, K-bounded cycle, block-grouped
**What:** each erased transaction is ONE `WriteBatch` (CRDT entries + block + map entry + marker — value XOR marker established atomically). The cycle erases at most K txs per tick; erases are grouped by delta block: a block's eligible-tx group is taken whole or deferred whole when it exceeds the remaining budget.
**Why:** per-tx atomicity is the landed substrate's contract (PRUNE-02 proven); the K bound is D-04; the grouping avoids Pitfall 3 (stranded siblings). Crash between per-tx commits = fixed point partially advanced; the sweep reconverges.

### Pattern 2: Owner-vended compose lock (D-01)
**What:** `CrdtSet` (through `CrdtDatastore`/`GlobalDB`) vends a lock-scoped compose — either a locking `EraseEntry` variant (guards re-checked + staging + commit ALL under `mutex_`) or an exposed guard type. `StageEraseElement`'s internal `lock_guard` (`crdt_set.cpp:690`) is reentrant under the outer hold (`std::recursive_mutex`, `crdt_set.hpp:378`).
**Precedent:** `PutElems` holds `mutex_` across build+commit (`crdt_set.cpp:761-806`) — the exact discipline D-01 cites. Prefer the locking-variant API: the critical section becomes impossible to forget, and WR-02 closes for every future caller, not just TxPruner.

### Pattern 3: Seal-as-dedup (D-02) — no new durable state
**What:** one check in `HandleCIDBroadcast` between the `HasBlock` miss (`crdt_datastore.cpp:712-717`) and the `IsCIDInCache` check (`:719`): `isResolved(cid)` true ⇒ skip. The never-erased seal markers (written at head-processing time, `crdt_datastore.cpp:2136/:2179/:2187`; deletion never reverts them — Phase-16 GATE-03) ARE the "already processed" record.
**Why one site suffices:** both re-broadcast (peers' periodic `RebroadcastHeads`) and restart head exchange deliver CIDs through this single function — the SC-3 test's two delivery modes share one guard.

### Pattern 4: Net-scoped policy, validator-side refusal (D-03)
**What:** one constexpr predicate (`DevMintPolicy::BurnlessMintsPermitted()` reading `version::GetNetworkID()`), consulted at exactly two production sites: the evidence-bypass legs (E8) and the dev validator's verdict (E9). The proposer-side needs NO new allowance — the routing already exempts no-witness validators at send.
**Precedent:** the ctest seam (`TestMintInputValidator`) is the behavioral template; the net predicate is the production addition. Same discipline as GATE-01's fail-closed enum gate: refusal is the default; permission is an explicit, code-reviewed set.

### Pattern 5: Status owned by the service, delegated by the facade (RECL-04)
**What:** `PruneStatus` (watermark = maxHeight at last completed cycle — monotonic; last-cycle stats: examined/eligible/erased/held-blocks/skipped/errors/duration/generation-tag) lives on TxPruner behind a small mutex; `GeniusNode::GetPruneStatus()` returns a copy. The unit tests target the SERVICE's status transitions (fixture-driven), not the facade.
**Why:** GeniusNode is not unit-testable (established repo fact); injecting the status ownership down keeps RECL-04 testable.

### Pattern 6: Containment-gate evolution (gates, not promises)
**What:** the EraseSurfaceGate target list (`crdt_erase_test.cpp:782-788`) gains the new tracked bodies: `StagePrunedMarker` (the production marker staging — its PutRaw-equivalent `batch->put` is sanctioned ONLY there), the TxPruner per-tx erase function, and the dev-policy predicate. The Phase-17 gates that must be REWORKED, not extended: the "zero production erase callers" inventories (16-SECURITY notes, 17-SECURITY T-17-0406 PutRaw-count) — Phase 18 legitimately creates the first production caller; the gates' invariants shift from "no production caller" to "exactly the sanctioned caller set" (grep-exact list) and "the dry-run path still erases nothing" (the `RunPruneDryRunCycle`-body token grep stays valid; the real-cycle function needs its own dry_run-conditional gate).

### Anti-Patterns to Avoid
- **Erasing a tx whose block is not erasable** — destroys other accounts' data in the same delta (Pitfall 2 from milestone research; the landed `EraseEntry` deletes the block unconditionally — the CALLER owns the block-gate verdict).
- **Building a second window/eligibility implementation in TxPruner** — reuse `Evaluate()` verbatim (hard invariant 2).
- **A persistent cursor "for safety"** — D-04 rejected it; erased txs vanish from enumeration; measure before building.
- **Config-gating the dev-mint net set** — weakens validator refusal to a per-node opt-in (see D-03c).
- **Holding the CrdtSet lock across the whole EVALUATION** — D-01 scopes the lock to the per-tx compose; evaluation stays lock-free read-only (bounded holds, D-04).
- **Firing CRDT hooks / `RemoveTransactionFromProcessedMaps` on erase** — revert hazard, Phase-16 Pitfall 1; the landed driver already avoids it — do not "fix" it.
- **Keying D-03 refusal on anything the proposer sends** — chain id, source ref, proposal fields are all attacker-chosen; only the validator's own `GetNetworkID()` decides.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Atomic multi-key erase + marker | Custom two-phase sequencing | `GlobalDB::EraseEntry` + `StagePrunedMarker` in the same batch | WriteBatch atomicity + sync WAL proven in Phase 16 (SC-2 tests green) |
| Eligibility / window / block gate | Re-derivation in the service | `PruneEligibility::Evaluate` + `EvaluateDeltaBlockErasable` | One home per rule; 22-test suite behind it |
| "Already processed" state for re-delivery | A pruned-CID record set | Seal markers via `isResolved` | D-02 locked: no new durable state; seals are never erased by construction |
| Compose mutual exclusion | A new lock manager / epoch scheme | `CrdtSet::mutex_` via the owner-vended compose lock | D-01 locked; the recursive mutex PutElems already serializes against |
| Net identity for the policy | Parsing config again / trusting tx fields | `version::GetNetworkID()` + the constexpr designated set | One net-id home; spoofing = actually joining that net |
| Crash simulation | Fault-injection RocksDB Env / process orchestration everywhere | The interruption seams + DB reopen assertions | Phase-16 precedent; true process-kill is ONE optional test, not the mechanism |
| Multi-node re-delivery harness | New network harness | `EraseNodeCollection` (real pubsub + GlobalDB nodes) | Landed, destructor-cleans the seam |
| Validator registration for dev mints | Ad-hoc branches in consensus code | The `IInputValidator` registry + `SelectInputValidator` routing | The exact mechanism ctests already use; zero consensus-core changes |

**Key insight:** Phase 18 is orchestration + policy on proven substrates. The only new *mechanisms* are the compose-lock exposure (an API shape), the one-line D-02 guard, the D-03 policy predicate + validator, and the marker's production staging — each landing in the module that owns its domain.

## Common Pitfalls

### Pitfall 1: The stranding edge — K budget vs shared delta blocks
**What goes wrong:** tx A and tx B are both eligible and share one delta block. The cycle erases A (deleting the shared block) but K runs out before B. Next tick, B's block gate parses a MISSING block → fail-closed `DESERIALIZE_FAILED` → B never erases; its CRDT value + map entry strand forever (and re-evaluate every tick, wasted work).
**How to avoid:** (1) block-grouped budgeting — a block's eligible group is taken whole or deferred (Pattern 1); (2) defense in depth — when the block gate finds the block ABSENT for an otherwise-eligible tx, treat "block already erased" as erase-completing evidence (the erase reduces to CRDT entries + map + marker; block staging is an idempotent no-op on an absent key; `isResolved` still true since the seal survives) with an explicit reason code, never silent.
**Warning signs:** per-cycle log showing perpetual `DESERIALIZE_FAILED` block reports for the same txs.

### Pitfall 2: Dry-run path regression while adding the real arm
**What goes wrong:** the `RunPruneDryRunCycle` zero-write guarantees (T-17-0603 grep) erode when the real cycle lands beside it — or the real cycle runs while `dry_run: true` is set.
**How to avoid:** two distinct functions with distinct gates; the dry-run body's token-cleanliness grep stays; the real cycle's arm checks `pruning_config_.dry_run == false` explicitly AND the containment gate pins the sanctioned-caller set (Pattern 6).
**Warning signs:** a shared "cycle" function branching on dry_run deep inside erase loops.

### Pitfall 3: Lock-scope overreach (D-01)
**What goes wrong:** holding the CrdtSet compose lock across the whole evaluation (or across all K erases) — starves PutElems merges, contradicts "bounded hold" (D-01), risks io-thread stalls (the IN-03 residual).
**How to avoid:** lock per-tx around guard→stage→commit only; evaluation outside; K bound keeps each hold to ~one batch commit. The spent-ness re-verify happens immediately BEFORE acquiring the compose lock (each UTXO call internally locked; no inversion — UTXOManager never calls into CRDT under its lock).
**Warning signs:** merge latency spikes correlated with prune ticks; lock held across `Evaluate()` calls.

### Pitfall 4: Seal-vs-block consult confusion in D-02 placement
**What goes wrong:** placing the isResolved check BEFORE the `HasBlock` hit-check, or replacing `HasBlock` with `isResolved` — breaks the normal "already have it" fast path ordering; or placing it after `EnqueueRootCID`.
**How to avoid:** exactly one insertion — after the `HasBlock` miss branch (`crdt_datastore.cpp:717`), before the `IsCIDInCache` check (`:719`). A sealed-but-present block still takes the HasBlock path (unchanged behavior when nothing is pruned — byte-identical default).
**Warning signs:** D-02 diffs touching more than the one function; behavior change on unpruned nodes.

### Pitfall 5: Dev-mint refusal keyed on proposer claims
**What goes wrong:** any D-03 check that reads the proposal's chain id / net field to decide permission — a main-net validator trusting a "net: 333" claim inside the tx.
**How to avoid:** the predicate reads `version::GetNetworkID()` — the validator's own process-global — and nothing else. Test: a `"dev"`-chain mint proposed on a non-designated-net fixture is rejected by that net's validators (the "regardless of proposer" test).
**Warning signs:** policy code touching any tx/proposal field.

### Pitfall 6: Watermark misunderstandings (RECL-03/04)
**What goes wrong:** reporting a watermark as "history below H is pruned" when it means "evaluated" — or advancing it on a partially-skipped cycle (gate miss mid-cycle, errors).
**How to avoid:** define once: watermark = maxHeight observed at the last cycle that COMPLETED its K-budget without hard error (skipped-for-reason txs don't block advancement — they re-appear next tick by design); document in the status struct. Bytes-reclaimed stays an explicit not-yet-measured placeholder until Phase 20 (SC-5 wording).
**Warning signs:** watermark moving on aborted cycles; operators reading it as a pruning guarantee.

### Pitfall 7: FAILED-transition cleanup overreach (folded todo)
**What goes wrong:** rolling back UTXOs on EVERY failed tx — reverts genuine normal-tx state; or keying the cleanup on the wrong reservation id.
**How to avoid:** discriminate exactly the bridge-mint shape: mint-v2 whose inputs are the reserved `UTXO_BRIDGE` burn outpoint (reservation id = the burn hash string, `MintFunds:1016-1030`); mirror the exception path's `RollbackUTXOs(rollback_inputs, transaction_hash, UTXO_BRIDGE)` (`:1050-1060`). Idempotent on re-fire.
**Warning signs:** cleanup firing for transfers; double-rollback errors on repeated FAILED transitions.

### Pitfall 8: Resurrected-entry sweep assuming the block gate passes
**What goes wrong:** an add-wins re-merge re-creates a pruned tx's CRDT entries AND re-fetches the delta block (if D-02's guard somehow missed, e.g. a pre-guard delivery already in flight). The re-created state differs from the original evaluation snapshot.
**How to avoid:** the sweep is re-EVALUATION (D-02b) — the next tick evaluates whatever EXISTS with fresh maxHeight/window inputs; re-verify under UTXO lock per-tx before erasing (D-01). No stale candidate lists.
**Warning signs:** any caching of eligible sets across ticks.

### Pitfall 9: GeniusNode wiring reaching past the service
**What goes wrong:** erase-loop logic accreting in GeniusNode (the god-class drift the milestone research flagged).
**How to avoid:** GeniusNode: timer arm/cancel, gate inputs, `GetPruneStatus()` delegation — nothing else. All cycle logic in TxPruner (unit-testable on fixtures).
**Warning signs:** erase tokens appearing in GeniusNode.cpp beyond the service call.

## Code Examples

### The per-tx erase step (shape — final signatures are planner's)
```cpp
// Sources: GeniusNode.cpp:4913-4931 (snapshot), PruneEligibility (Evaluate),
//          TransactionManager.cpp:4277-4309 (driver), globaldb.cpp:797-884 (compose)
// Per tick (D-C4): construct from a consistent snapshot; NEVER cache.
const auto snapshot = SnapshotAccountServices();          // GeniusNode.cpp:4144
if ( generation_superseded( snapshot ) ) { return; }      // :4982 pattern — before ANY erase

auto result = evaluator->Evaluate();                      // reused verbatim
for ( const auto &group : block_grouped_eligible( result, remaining_K ) )
{
    for ( const auto &tx : group.txs )
    {
        // (1) D-01: fresh spent-ness re-verify under the UTXO lock
        //     (IsOutPointGenuinelyConsumed per outpoint — internally shared_lock'd)
        if ( !reverify_genuinely_consumed( tx ) ) { record_skip( tx ); continue; }
        // (2)-(5) D-01: compose under the CrdtSet lock: guards → stage
        //     (4 steps + StagePrunedMarker) → commit — owner-vended lock
        OUTCOME_TRY( transaction_manager.EraseTransactionEntryPruned( tx ) );
    }
}
status_.Update( /* watermark=maxHeight, examined, erased, held, … */ );
```

### The D-02 guard (one insertion)
```cpp
// Source: crdt_datastore.cpp:703-719 (current chain)
for ( const auto &bCastHeadCID : decodeResult.value() )
{
    auto dagSyncerResult = dagSyncer_->HasBlock( bCastHeadCID );   // :706
    ...
    if ( dagSyncerResult.value() )
    {
        continue;                                                  // known block — unchanged
    }
    // D-02 NEW: block gone but seal present ⇒ processed-and-pruned ⇒ terminal.
    // No new durable state: the never-erased seal markers ARE the dedup record.
    BOOST_OUTCOME_TRY( auto pruned_resolved, dagSyncer_->isResolved( bCastHeadCID ) );
    if ( pruned_resolved )
    {
        logger_->trace( "{}: Pruned root {} re-delivered — terminal, skipping", __func__, ... );
        continue;
    }
    if ( dagSyncer_->IsCIDInCache( bCastHeadCID ) )                // :719 — unchanged
    ...
```

### The D-03 policy + net-scoped bypass (shape)
```cpp
// One home (beside sgns_version.hpp net constants):
inline constexpr std::array<uint16_t, 2> kDevMintPermittedNets{ version::STAGING_NET_ID,   // 333
                                                                version::DEV_NET_ID };      // 144
inline bool DevMintBurnlessPermitted()   // reads the VALIDATOR'S OWN net — never a tx field
{
    const auto net = version::GetNetworkID();
    return std::find( kDevMintPermittedNets.begin(), kDevMintPermittedNets.end(), net ) !=
           kDevMintPermittedNets.end();
}

// PublicChainInputValidator.cpp:316-334 — both bypass legs gain the same conjunct:
if ( source_reference.empty() )                    // was: unconditional valid=true
{
    if ( !DevMintBurnlessPermitted() ) { return evidence; }   // invalid on non-designated nets
    evidence.valid = true;
    return evidence;
}
const auto chain_id = tx.GetChainId();
if ( chain_id.empty() || chain_id == "supergenius" )
{
    if ( !DevMintBurnlessPermitted() ) { return evidence; }   // closes the global bypass
    evidence.valid = true;
    return evidence;
}
```

### Status surface (shape)
```cpp
// TxPruner-owned (unit-testable); GeniusNode::GetPruneStatus() returns a copy.
struct PruneCycleStats      // RECL-03/04 — one cycle
{
    uint64_t    examined = 0, eligible = 0, erased = 0, held_blocks = 0, skipped = 0, errors = 0;
    std::chrono::milliseconds duration{};
    bool        generation_superseded = false;
};
struct PruneStatus
{
    std::optional<uint64_t> watermark;         // maxHeight at last COMPLETED cycle (monotonic)
    PruneCycleStats         last_cycle;
    // bytes_reclaimed: Phase 20 (compaction) — placeholder, never a fake number
};
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| "Zero production erase callers" (16/17 residual) | First sanctioned production caller (TxPruner) + exact-caller-set gates | This phase | Containment gates shift from absence-proof to enumeration-proof (Pattern 6) |
| WR-02 check-then-act window documented, deferred | Compose critical section owner-vended under `CrdtSet::mutex_` | This phase (D-01) | The 16-SECURITY documented residual closes by construction |
| `dry_run=false` WARNs and does nothing (`GeniusNode.cpp:4879-2883`) | The real cycle arms | This phase | SC-1..SC-5 become executable |
| Unconditional evidence bypass in released code | Net-scoped dev-mint policy through full consensus | This phase (D-03) | Resolves the P1 witness-bypass todo; staging demo gains real prunable history |
| Exception-only MintFunds rollback | FAILED-transition cleanup (D-18 mirror) | This phase (folded todo) | Burn UTXO no longer leaks + wedges boot (companion GATE-05 todo stays Phase 19) |
| Guard placement open question (milestone research) | DECIDED: seal-based dedup at broadcast entry (D-02) | 18-CONTEXT | Do not re-open |

**Deprecated/outdated:** "RequestRelevantHeads" (milestone-research name) — no such symbol exists in `src/crdt`; the restart head-exchange mechanism is the periodic `RebroadcastHeads` (`crdt_datastore.cpp:382-416`) whose deliveries land in `HandleCIDBroadcast` (same guard site).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Staging validators run the released binary with `net_id: 333`, so the constexpr net predicate gates them correctly | D-03 | Deployment fact, not code — if staging runs a custom build, the constant set needs that net added; confirm at UAT |
| A2 | SC-3's "restarting head exchange" == `RebroadcastHeads` deliveries into `HandleCIDBroadcast` (no separate head-request path exists — `RequestRelevantHeads` not found in src) | D-02 / PRUNE-05 | If a second intake exists, the guard needs mirroring there; grep `HandleCIDBroadcast` callers + pubsub message routing in the SC-3 test to confirm coverage |
| A3 | True process-kill (fork + `_exit`) test is feasible against the temp-DB fixture on macOS/Linux | Validation | If fork+threads proves flaky, the seam-simulated matrix carries SC-2 alone (planner judgment; roadmap wording tolerates kill-simulation per Phase-16 D-03 precedent) |
| A4 | `BuildUTXOWitness`'s tail beyond `:1110` (payload-mismatch branches) contains no further mint-specific allowance | D-03 trace | Read up to the load-bearing gates (`:1004`, `:1057-1064`, produced-outputs block); tail is additional rejection paths — verify during implementation |
| A5 | Line numbers cited drift by a few lines as Phase-18 edits land (files read this session at the cited lines) | throughout | Citation drift only; contracts re-verifiable by symbol name |
| A6 | The dev-mint chain literal `"dev"` collides with no chainlist public-chain id | D-03c | Verify against chainlist data at implementation; if collision, any unused literal works — the constant's one home makes renaming trivial |

**All other claims were verified against source this session** (file:line cited inline).

## Open Questions (RESOLVED)

All four adopted by the plans: Q1 settle knob → 18-04 (settle_seconds=60); Q2 true-kill → 18-07 (seam matrix is the gate, one optional true-kill); Q3 missing-block depth → both mitigations (18-03-T3 + 18-04-T1); Q4 validator strictness → 18-02 (ctest twin shape).

1. **Settle-window knob name and default** (discretion)
   - What we know: D-C2 requires a settle window; the dry-run timer uses 1-min first tick / 10-min cadence; SC-4 needs "settle elapsed" as a gate conjunct.
   - Recommendation: `pruning.settle_seconds` (or reuse the first-tick delay semantics — e.g. the real cycle's first tick is `settle_seconds` after READY∧empty, re-armed if the gate re-opens); default ~60s; the simplest shape that cannot fire during initial sync.
2. **True process-kill test: include or not** (planner)
   - What we know: 16-CONTEXT D-03 deferred process-kill to "Phase 18's service criterion"; SC-2 says "killing and restarting"; A3 flags harness cost.
   - Recommendation: land the seam-simulated kill matrix (all steps, DB reopen, fixed point) as the gate; add ONE fork-based true-kill test as a stretch if it runs stably on macOS; do not block the phase on it.
3. **Missing-block sibling handling depth** (planner)
   - What we know: Pitfall 1's two mitigations (group-or-defer + missing-block-tolerant erase evidence).
   - Recommendation: implement BOTH — group-or-defer prevents the common case; the tolerant reason code (block absent ∧ tx otherwise eligible ⇒ erase completes without block staging) makes the fixed point total. If forced to one, take group-or-defer (simpler; residue is bounded and reported).
4. **Dev validator's `ValidateUTXOParameters` strictness**
   - What we know: ctest twin returns true unconditionally.
   - Recommendation: keep the twin's shape (true for non-empty params) — the net gate in `ValidateWitness` is the policy; adding param strictness here duplicates the commitment/binding checks that already run (`HandleNonceConsensusSubject:347-372`).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Configured build tree | builds/tests | ✓ | `build/OSX/Release` — `prune_eligibility_test`, `crdt_test`, `globaldb_integration_test`, `transaction_manager_certificate_fallback_test`, `utxo_manager_test`, `multi_node_finality_fault_test` binaries verified present | reconfigure |
| cmake / ctest | build/test | ✓ | 3.31.4 / 3.31.4 | — |
| clang-format / clang-tidy | repo verification gate | ✓ (repo memory: keg-only llvm@22 paths + compile-db filter; challenge "can't run" claims) | — | must be run; state if unavailable |
| Vendored RocksDB / ipfs-lite-cpp | substrate (untouched) | ✓ | 9.4 / workspace fork | — |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** none.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest via repo `addtest` macro (`cmake/functions.cmake` — auto ctest registration, auto `disable_clang_tidy`) |
| Config file | none — per-target registration in `test/src/account/CMakeLists.txt` / `test/src/crdt/CMakeLists.txt` |
| Quick run command | `cmake --build build/OSX/Release --target tx_pruner_test && build/OSX/Release/test_bin/tx_pruner_test --gtest_filter='*TxPruner*'` (target name TBD) |
| Full suite command | `cmake --build build/OSX/Release && ctest --test-dir build/OSX/Release --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PRUNE-04 (SC-1 real erase) | Light node with spent-beyond-window history: cycle erases eligible txs (both levels gone, marker present, `/utxo/` keys byte-identical, unspent siblings untouched); re-run on clean node = no-op | integration (CRDTFixture family + real prune cycle; three-part assertions + negative control) | `test_bin/tx_pruner_test --gtest_filter='*RealPrune*'` | ❌ Wave 0 |
| PRUNE-04 (SC-2 crash) | Interrupt at every step (seam, incl. NEW cycle-level points: between per-tx erases, pre/post evaluation) → DB reopen → re-run → fixed point; no partial tx (no value-without-block, no orphan map entry); optional one true-kill fork test | integration (seam matrix + reopen) | `test_bin/tx_pruner_test --gtest_filter='*CrashFixedPoint*'` | ❌ Wave 0 |
| PRUNE-04 (idempotence) | Second cycle after full drain: examined>0, erased=0, nothing changes | integration | `test_bin/tx_pruner_test --gtest_filter='*Idempotent*'` | ❌ Wave 0 |
| PRUNE-05 (D-02 guard) | Two-node EraseNodeCollection harness: put on A → erase on B → re-broadcast pruned root (A) + head-exchange redelivery ⇒ ZERO fetch attempts on B (counter seam) and ZERO re-created elements; pre-guard delivery (unpruned) still fetches (negative control) | integration (real pubsub) | `ctest --test-dir build/OSX/Release -R globaldb_integration` (new file in target) | ❌ Wave 0 |
| PRUNE-05 (sweep/re-prune) | Manually re-create a pruned tx's CRDT entries (simulated add-wins resurrect) → next cycle re-erases them | integration | `test_bin/tx_pruner_test --gtest_filter='*SweepReprunes*'` | ❌ Wave 0 |
| SC-4 (tick gate) | Gate refuses: non-Light, non-READY, non-empty missing-set, settle not elapsed; default-off config ⇒ service never constructed, zero behavior change (full-suite green) | unit + integration | `test_bin/tx_pruner_test --gtest_filter='*TickGate*'` | ❌ Wave 0 |
| SC-5 / RECL-03 (observability) | Per-cycle log fields (examined/erased/watermark; bytes placeholder); generation-superseded tagging | integration (log-capture or status-driven assertion) | `test_bin/tx_pruner_test --gtest_filter='*Observability*'` | ❌ Wave 0 |
| RECL-04 (status) | `PruneStatus` snapshot correctness across cycles: watermark monotonic, stats match fixture actions, thread-safe concurrent read during cycle | unit + integration | `test_bin/tx_pruner_test --gtest_filter='*PruneStatus*'` | ❌ Wave 0 |
| PRUNE-04 (D-01 lock) | Compose holds the CrdtSet lock: a concurrent PutElems on the same key during the compose window cannot interleave (deterministic fixture: race-injection thread + ordering assert); guards re-checked inside the lock | integration | `test_bin/tx_pruner_test --gtest_filter='*ComposeLock*'` | ❌ Wave 0 |
| D-03 (designated net permits) | `"dev"`-chain burn-less mint through REAL consensus on a designated-net fixture (net=333 shape): certifies, UTXOs land; SendTransactionItem preflight passes (no witness required) | integration (finality-test-style multi-peer or cert-fallback family with the dev validator) | `test_bin/dev_mint_policy_test --gtest_filter='*DesignatedNet*'` (or folded into tx target) | ❌ Wave 0 |
| D-03 (non-designated refuses) | Same mint on a NON-designated-net fixture (net=963/369 shape): validators reject (no quorum, no certificate) regardless of proposer; empty-chain-id mint also rejected (bypass closed) | integration | `test_bin/dev_mint_policy_test --gtest_filter='*Refuses*'` | ❌ Wave 0 |
| D-03 (evidence bypass net-scoped) | `GatherVerificationEvidence` unit: empty-source and empty/supergenius-chain legs ⇒ valid ONLY under the net predicate; otherwise requires endpoints | unit | `test_bin/dev_mint_policy_test --gtest_filter='*EvidenceBypass*'` | ❌ Wave 0 |
| Folded todo (FAILED cleanup) | Mint rejected by consensus ⇒ burn UTXO removed (not READY on disk), reservation released; idempotent on re-fire; normal-tx FAILED does NOT roll back UTXOs | unit + integration | `test_bin/tx_pruner_test --gtest_filter='*BurnCleanup*'` (or transaction tests) | ❌ Wave 0 |
| Containment gates | Updated EraseSurfaceGate targets (StagePrunedMarker, TxPruner erase fn, dev policy); sanctioned-caller-set greps; dry-run body still token-clean | gate tests (source scan) | `test_bin/crdt_test --gtest_filter='*EraseSurfaceGate*'` | ✅ (extend) |
| SC-1 "unspent untouched" | `/utxo/` prefix byte-identical across a full prune cycle; certs/heads/seals untouched (raw-key assertions) | integration | covered by RealPrune suite | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** targeted `--gtest_filter` on the new/extended suites + clang-format/clang-tidy on changed files
- **Per wave merge:** `ctest --test-dir build/OSX/Release --output-on-failure` (full suite — the default-off/no-change guarantee)
- **Phase gate:** full suite green + all new suites green + containment gates green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `TransactionManager` public missing-set accessor (`missing_tx_hashes_` private at hpp:748-749) — tick-gate input
- [ ] `CrdtSet`/`CrdtDatastore`/`GlobalDB` compose-lock exposure (D-01) — the owner-vended critical section
- [ ] `StagePrunedMarker` production staging on the marker owner (PruneEligibility home) — moves the marker write into the erase batch; update EraseSurfaceGate + the T-17-0406 PutRaw containment to the sanctioned-set shape
- [ ] D-02 guard insertion + a fetch-attempt observable seam (counter or hook on `EnqueueRootCID`/root-job creation — planner picks; static-seam precedent `SetEraseInterruptHookForTest`)
- [ ] `DevMintPolicy` home + constants; `DevMintInputValidator`; the two-leg net-scoping in `GatherVerificationEvidence`
- [ ] `PruningConfig` new keys (`max_erases_per_cycle` locked; settle knob discretionary) + fail-closed validation
- [ ] `TxPruner.{hpp,cpp}` + `PruneStatus`; GeniusNode real-cycle arm + `GetPruneStatus()`
- [ ] `test/src/account/tx_pruner_test.cpp` (CRDTFixture family) + dev-mint policy tests (finality-test-style multi-peer harness for the real-consensus cases)
- [ ] SC-3 re-broadcast test file in the `globaldb_integration_test` target (EraseNodeCollection base)
- [ ] Framework install: none needed (gtest integrated)

## Security Domain

ASVS level 1 (config: `security_enforcement: true`, `security_asvs_level: 1`, `security_block_on: high`).

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|------------------|
| V2 Authentication | no | no new network/auth surface (mint signature checks are the existing consensus trust model) |
| V3 Session Management | no | — |
| V4 Access Control | yes | D-03 is the phase's core access-control change: burn-less mint permission scoped by the validator's OWN net id (immutable process-global, config-validated against known ids); refusal is the default state of released binaries; designated set is compile-time (config cannot widen). Erase access: tick gate conjuncts (Light ∧ READY ∧ missing-empty ∧ settle) + config consent |
| V5 Input Validation | yes | D-02 guard validates via the seal record (persistent, attacker-undeletable through the erase surface); marker records validated on read (17-landed WR-04 checks); config type-checked per key with fail-close; policy predicate reads only the process-global net id — never proposal fields |
| V6 Cryptography | no | existing signature/cert machinery untouched |
| V7 Error handling | yes | `outcome::result`/reason codes; POST_COMMIT_ERASE_ANOMALY semantics preserved; per-tx erase failure skips the tx and continues the cycle (reported), never aborts silently |
| V8 Data protection | yes (invariant) | the destructive surface stays the landed guarded compose; marker staging joins the one batch (value XOR marker); `/utxo/`, `/cert/`, heads, seals outside the erase key set by construction; unspent-set byte-identity asserted |

### Known Threat Patterns for this stack
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Unbacked mint certification on non-designated nets (empty-chain-id bypass; "dev"-chain mint; "supergenius"-leg via endpoint wiring) | Elevation/Tampering | D-03: net-scoped bypass legs + dev validator gated on the validator's own net; refusal tests for both mints on non-designated-net fixtures; registry ids audited (no public-chain id collision, A6) |
| Policy keyed on proposer-controlled fields | Elevation | The predicate reads only `version::GetNetworkID()`; grep-verifiable (no tx/proposal field access in policy code); test asserts rejection regardless of proposer |
| Config spoofing to enable dev mints | Elevation | Designated set is compile-time; config can only select among the four known nets (validated); claiming 333 = joining 333 (network partition makes spoofing self-defeating) |
| Re-delivery resurrection / fetch storm (DoS on the single DAG worker) | DoS/Tampering | D-02 seal guard at the single broadcast intake; SC-3 zero-fetch assertion; sweep re-prunes resurrected entries; seals never erasable (GATE-03 gate) |
| Partial-erase crash state | Tampering | Per-tx single atomic batch (marker included); seam-extended crash matrix; fixed-point assertions |
| Erase of in-use/wrong data (shared blocks, unspent refs) | Tampering/Destruction | Block-gate verdict required before erase; block-grouped budgeting; UTXO spent-ness re-verify under lock immediately before staging; guards re-checked inside the compose lock (D-01) |
| Burn-UTXO leak wedging boot (fabricated burn refs persisted READY) | DoS | Folded-todo FAILED-transition cleanup; idempotent rollback; companion GATE-05 boot-tolerance work stays Phase 19 |
| Status/reporting lies (watermark overclaiming) | Repudiation | Watermark defined as last COMPLETED cycle only; per-cycle stats from the structured result; generation-superseded tagging (17 pattern) |

## Sources

### Primary (HIGH confidence — verified in this repo this session)
- `src/account/GeniusNode.cpp` — `LoadSgnsConfig` net_id :411-416, node_type :438-457, pruning block :473-489; timer teardown :2456-2460/:2545-2548; `StartPruneDryRunTimerIfConfigured` :4862-4911 (the dry_run==false arm :4879-4883); `RunPruneDryRunCycle` :4913-5020 (snapshot :4931-4943, error split :4954-4973, generation tag :4975-4998); `SnapshotAccountServices` :4144; MintFunds RPC :3388; hpp `AccountServiceSnapshot` :1022-1030, timer/config members :1169/:1179
- `src/transaction/PruneEligibility.{hpp,cpp}` — full evaluator/config/marker contract (hpp :55-200 enums+structs, :210-239 PruningConfig, :264-405 evaluator+marker API); FromJSON impl :84-97; marker helpers :147-227 (per 17-audit)
- `src/crdt/globaldb/globaldb.{hpp,cpp}` — `EraseEntry` :797-884 (guards :814-847, compose+seam :851-867, post-commit anomaly :875-881); contract + WR-02 note hpp :203-258; seam static :790-795, hpp :434-436
- `src/crdt/impl/crdt_set.cpp` — `StageEraseElement` :669-737 (lock :690, IsTopLevel :712-717, unconditional value/priority :726-732); `PutElems` :739-806 (lock across build+commit :761-806); `mutex_` recursive private (hpp :378)
- `src/crdt/impl/crdt_datastore.cpp` — `HandleCIDBroadcast` :677-772 (HasBlock :706-717, IsCIDInCache :719, pending :751, EnqueueRootCID :759); `EnqueueRootCID` :2238-2253; markResolved sites :2136/:2179/:2187; `StartRebroadcastHeads`/`RebroadcastHeads` :382-416/:1209+; GetPriority/GetLocalDeltaKeyValues :2260+
- `src/crdt/impl/graphsync_dagsyncer.cpp` — `HasBlock` :524-530 (block store + LRU only); `isResolved`/`markResolved` :812-822
- `src/transaction/TransactionManager.{hpp,cpp}` — `SelectInputValidator` :1942-1982 (registry init :72); send preflight :2036-2057; post-commit witness :2131-2163; `MintFunds` :918-1063 (canonicalize :932-936, burn insert :995-1002, TryReserveOutpoint :1016-1030, exception rollback :1050-1060); `EraseTransactionEntry` :4277-4309; `ChangeTransactionState` :4784+; `GetMonitoredNetworkIDs` :2227-2236; `GetState` hpp :330; `missing_tx_hashes_` private hpp :748-749; subject-handler registration :166-176
- `src/transaction/TransactionConsensusHandler.{hpp,cpp}` — `HandleNonceConsensusSubject` :312-513 (commitment binding :347-372, reject_and_maybe_fail_local :414-453); `ValidateTransactionForConsensus` :515-554; `CheckTransactionTypeRules` :801-819; `ValidateWitnessForConsensus` :821-916 (requirement :881-889, ValidateWitness call :897); `BuildUTXOTransitionCommitment` :919-975 (params-only); `BuildUTXOWitness` :978-1110 (GetUnconsumedUTXO :1004, producer gate :1057-1064)
- `src/account/PublicChainInputValidator.{hpp,cpp}` — `ValidateUTXOParameters` :42-64 (trivial accept); `ValidateWitness` :66-148 (commitment checks, source_reference :111-120, evidence call :122); `GatherVerificationEvidence` :306-334+ (empty-source bypass :316-323, chain-id bypass :326-334, no-endpoints reject :336-344); `RequiresConsensusUTXOData` false hpp :190-193; `RpcVerificationEvidence.valid=false` default hpp :65-68; `SetRpcEndpoints`/`AddRpcEndpoints` :150-233
- `src/account/GeniusInputValidator.{hpp,cpp}` — `Register` :113-120 ("supergenius"/"supergenius_chain"/""); `ValidateUTXOParameters`→`VerifyParameters` :122-155; `RequiresConsensusUTXOData` true hpp :56-59
- `src/account/MintTransactionV2.{hpp,cpp}` — chain_id storage/round-trip :18/:27/:65/:103/:161-164; `GetSlotID` :212-235; outputs fallback :132-135
- `src/account/GeniusTransaction.hpp` — `GENIUS_CHAIN_ID="supergenius_chain"` :37; `GetChainId` default :156-159
- `src/base/sgns_version.{hpp,cpp}` — net constants hpp :12-15 (MAIN 369/TEST 963/DEV 144/STAGING 333); global `network_id_` default DEV cpp :18; `GetNetworkID` :56; `SetNetworkId` validation :75-86
- `src/account/UTXOManager.{hpp,cpp}` — `VerifyParameters` :681+ (shared_lock :685); `RollbackUTXOs` hpp :345-347
- `src/blockchain/Consensus.cpp` — `HandleProposal` :2803-2972 (subject-handler validation before voting)
- Tests: `test/testutil/TestMintInputValidator.hpp` (whole file — the ctest seam; static registration :48); `test/src/blockchain/multi_node_finality_fault_test.cpp` (MintFor :1097-1112, CommitmentFor :1114+, subject+proposal+certificate :1227-1274); `test/src/account/transaction_manager_certificate_fallback_test.cpp` (MakeCompetingMintV2 :353-371, fixture :378-449); `test/src/crdt/globaldb_erase_test.cpp` (EraseNodeCollection :73-161); `test/src/crdt/crdt_erase_test.cpp` (gates :706-807, banned tokens :712-719, targets :782-788)
- Build tree verified: `build/OSX/Release/test_bin/` contains prune_eligibility_test, crdt_test, globaldb_integration_test, transaction_manager_certificate_fallback_test, utxo_manager_test, multi_node_finality_fault_test; cmake/ctest 3.31.4

### Secondary (MEDIUM-HIGH confidence)
- `.planning/phases/16-*/16-{CONTEXT,RESEARCH,SECURITY}.md` + `17-*` — substrate contracts, audited mitigations, documented residuals landing here (WR-02, IN-03 io-thread scaling, containment-gate inventories)
- `.planning/research/{SUMMARY,PITFALLS}.md` — milestone research (erase-flow order, fetch-resurrect, collect-then-delete, batch caps, never-from-callbacks); cross-checked this session where load-bearing

### Tertiary (LOW confidence)
- none used

## Metadata

**Confidence breakdown:**
- D-03 ctest-mint trace: HIGH — followed end-to-end in source (send preflight → registry routing → witness layer → evidence bypass → ctest fixtures), every gate read at the cited lines
- D-01/D-02 mechanics: HIGH — lock types, holders, call sites, seal-vs-block consult all read directly; the one-insertion guard verified against both delivery modes
- Architecture (service shape, per-tx commit, block-grouping): HIGH — built on audited landed surfaces; the stranding edge (Pitfall 1) is a derived-but-source-grounded consequence of the landed fail-closed block gate
- Pitfalls: HIGH — 9 hazards; the top ones (stranding, dry-run regression, lock overreach, proposer-claim keying) grounded in read source
- Validation plan: HIGH with A3 caveat (true-kill harness feasibility)

**Research date:** 2026-10-21
**Valid until:** 2026-11-20 (stable internal codebase; re-verify if `src/transaction/TransactionConsensusHandler.cpp`, `src/account/PublicChainInputValidator.cpp`, or `src/crdt/impl/crdt_datastore.cpp` changes land first)
