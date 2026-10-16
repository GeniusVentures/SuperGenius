# Phase 17: Eligibility, Policy & Gating (Dry-Run) - Research

**Researched:** 2026-10-16
**Domain:** Pruning eligibility policy (genuine-spend ∧ spend-recency window), multi-element block-parse gate (dry-run), cert-slot previous_hash generalization, Light-only fail-closed gating with tx-prefix allowlist, pruning config surface, `/prune/` marker design — all on the Phase-16 erase substrate, with zero production erasure.
**Confidence:** HIGH (every load-bearing claim re-verified against this repo's source this session with file:line citations; the two decision-critical verifications — D-02 Candidate A and D-01 cert-slot scope — were traced end-to-end in source, not taken from prior research)

## Summary

**D-02 Candidate A FAILED verification.** The premise "the CONSUMED UTXO entry is a GlobalDB put at spend time, so it carries its own CRDT priority (`/crdt/k/<utxo-key>/p`)" is false in this codebase: `ConsumeUTXOs` persists through `StoreUTXOsLocked → StoreUTXOSnapshot`, which writes **raw RocksDB batch puts** via the `storage::rocksdb` facade (`db->batch()->put()`, `UTXOManager.cpp:1006-1067`) at raw keys `/utxo/<owner>/<txid>:<idx>` (`UTXOManager.cpp:18-24`) — outside the `/crdt/` namespace, with **no CRDT priority anywhere**. `UTXOManager::db_` is the raw facade of the shared DB, wired at `TransactionManager.cpp:3156` (`LoadUTXOs(globaldb_m->GetDataStore())`); no code path ever calls `CrdtSet::SetPriority` for a UTXO key. Per the locked D-02 instruction, **Candidate B is therefore the window mechanism**: populate `spent_by_txid` at `ConsumeUTXOs` (the consuming tx is in scope at all four call sites — `TransactionManager.cpp:2637, :2657, :2702, :2871`) and define `spend-recency = maxHeight − priority(spender tx key) ≥ N`, taking the **most-recent spend across all outputs**. The schema plumbing for B already exists and is symmetric: `UTXOEntryRecord` proto fields persist/restore (`UTXOManager.cpp:1033-1043, :925-937`), and the CONSUMED→READY revert path already resets both fields (`RestoreConsumedUTXOs`, `:389-390`). **This requires amending the REQUIREMENTS.md out-of-scope row** ("Populating `spent_epoch`/`spent_by_txid`…") — exactly the conditional amendment D-02 authorizes.

**D-01's minimal generalization is smaller than expected.** The previous_hash ladder `GetOutgoingPreviousHash` (`TransactionManager.cpp:1660-1686`) has four rungs: tracked (in-memory) → persisted-head (**reads the tx value** at `:1847`) → cert-slot (**registration-type only**, `:1720`) → full CRDT scan (reads all tx values). The second consumer, `TransactionConsensusHandler::EvaluateTransactionReplayProtection` (`TransactionConsensusHandler.cpp:618-741`), **already resolves a missing predecessor from its slot certificate** (`:666-688`) — inbound replay validation is already pruning-tolerant. The minimal fix is to relax rung 3's `GetType() == "registration"` restriction so any certified embedded transaction resolves from `/cert/<addr>:<nonce-1>`; rung order is preserved, so behavior when history is intact is unchanged (rungs 1-2 answer first), and when they fail the generalized rung returns the same cert-bound hash the CRDT scan would have found — but without needing any tx value. Certificates live at `/cert/` (raw-prefix CRDT keys, `Consensus.hpp:726-727`, read via `GetCertificateBySlot` `Consensus.cpp:4730-4758`), outside the `/bc-<netid>/tx/` allowlist, hence never pruned.

Everything else this phase needs is already landed and public: `GlobalDB::GetLocalDeltaKeyValues` (block parse, `globaldb.hpp:349`), `GlobalDB::GetCRDTHeadList` → maxHeight (`:327`), `IsOutPointGenuinelyConsumed` (`UTXOManager.cpp:777-783`), the Phase-16 erase substrate (`EraseEntry`/`EraseTransactionEntry`/`ResolveTransactionPathAndCID` — all present with documented contracts), the config surface (`LoadSgnsConfig`, `GeniusNode.cpp:389-518`, rapidjson + INFO-resolved-values convention), and a gold-standard test fixture family (`CertificateFallbackTest : public ::test::CRDTFixture` with `BuildSignedCertificate`/`PersistCertificateAtSlot`/`PersistTransaction` helpers). The one small API gap: **no public per-key CRDT priority read** exists on `CrdtDatastore`/`GlobalDB` (`CrdtSet::GetPriority` is public at `crdt_set.hpp:208` but `set_` is private inside `CrdtDatastore`) — a ~2-line passthrough is a Wave-0 item.

**Primary recommendation:** Implement Candidate B exactly as specified below (4-field change at `ConsumeUTXOs` + signature extension carrying the spender hash), generalize cert-slot rung 3 by relaxing its type restriction only, build the evaluator as a pure structured-result function over `QueryKeyValues(base+"tx/")` enumeration with the Light+allowlist gates at its entry, and design the `/prune/tx/<hash>` marker with a proto record whose write (Phase 18) joins the Phase-16 erase WriteBatch. Prove SC-1/SC-3 with the `CertificateFallbackTest` fixture family extended with Phase-16 `EraseTransactionEntry` to simulate the pruned state.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-C1:** Physical local RocksDB delete only — never the CRDT tombstone path; one atomic WriteBatch per erase (Phase 16 substrate: `CrdtSet::StageEraseElement`, `GraphsyncDAGSyncer::StageBlockRemoval`, `GlobalDB::EraseEntry`, `TransactionManager::EraseTransactionEntry` — all landed, zero production callers)
- **D-C2:** Genuine spend = `IsOutPointGenuinelyConsumed` (`UTXOManager.hpp:397`) — synthesized zero-amount CONSUMED tombstones are not spend evidence
- **D-C3:** Block gate parses the delta block itself at evaluation time (`GetNodeWithoutRequest` + `GetDeltaFromNode`, `GetLocalDeltaKeyValues` pattern, `crdt_datastore.cpp:2280-2302`) — never "what this node applied"
- **D-C4:** Seal markers never erased; `maxHeight` comes from the existing `GetCRDTHeadList()` — no new API
- **D-C5:** Config knobs in `sgns_config.json` beside `node_type` (read in `LoadSgnsConfig`), default off, resolved values INFO-logged; absent keys behave exactly as today
- **D-C6:** The evaluator runs on committed state only — never from CRDT element callbacks (they fire pre-batch-commit; proven repo invariant)
- **D-C7:** Allowlist prefix from the verified constant: `TRANSACTION_BASE_FORMAT = "/bc-%hu/"` (`TransactionManager.hpp:433`) — `/chain/<net>/tx` naming is stale; do not use it
- **D-01:** Cert-slot previous_hash generalization. Resolve the outgoing `previous_hash`/nonce chain from certificate data (certificates are never pruned — GATE-02 — and embed the winning tx hash), generalizing the existing `GetRegisteredOutgoingPreviousHash` precedent (`TransactionManager.cpp:1688-1726`). Own-nonce exemption (last-K anchor retention) is a FALLBACK only if planning proves cert-slot too invasive — K-magic-number fragility and burst re-exposure are the reasons it is not primary.
- **D-02:** **The window measures the recency of the SPEND, never the age of the transaction's creation, and never wall-clock.**
  - **Candidate A (preferred — researcher verifies):** the CONSUMED UTXO entry is itself a GlobalDB put at spend time, so it carries its own CRDT priority (`/crdt/k/<utxo-key>/p`). `spend-recency = maxHeight − priority(consumed entry) ≥ N`. Zero schema change; the signal is exactly the spend event. Research MUST verify consumed entries receive per-key priority and that the consumed entry's key is enumerable for the evaluator.
  - **Candidate B (fallback):** populate `spent_by_txid` at `ConsumeUTXOs` and require the consuming transaction's depth ≥ N. Explicit and auditable; adopting B amends the REQUIREMENTS.md out-of-scope row "populating spent_epoch/spent_by_txid" — amend that row ONLY if A fails verification.
  - `N` is a config knob, required when pruning is enabled; no default value (default-off makes it unnecessary).
- **D-03:** The evaluator is a pure structured result over candidate enumeration — eligible/ineligible sets with per-item reasons — unit-testable against fixtures (the SC-1 exact-set assertion targets this result). Production dry-run mode emits a per-cycle INFO summary derived from it. NO queryable report surface this phase; operator introspection is Phase 18's RECL-04.
- **D-04:** Per-tx marker records under a node-local `/prune/` prefix in the **same shared RocksDB** (the `CRDTWorkJournal` `/crdt/work/` precedent: direct datastore writes, never broadcast, same batch discipline). Record = tx path + erased CID. The format, key shape, and lookup semantics (PRUNED vs NOT_FOUND) are DESIGNED in this phase; the write happens in Phase 18's erase flow; the query-surface answer lands in Phase 19 (SURV-02).

### Claude's Discretion
- Evaluator module placement (standalone evaluator class in `src/transaction/` vs TransactionManager-owned section — follow the one-owner-per-domain rule; TxPruner service itself is Phase 18)
- Exact config key names (`pruning.*` in sgns_config.json) and the INFO log line format
- Fixture construction for the exact-set test (fresh temp DB per D-C6 discipline from 16)

### Deferred Ideas (OUT OF SCOPE)
- **"Extra final prune"** (owner, 2026-10-16): a future prune capability distinct from the spent-based sliding window (retirement/whole-history-style) — new capability, own phase if pursued; NOT folded into v1.2 phases
- **Cert-tightened window** (require certificate presence per candidate) — deferred until a concrete need appears (research recommendation, owner concurred via A-first choice)
- **Queryable dry-run/operator report** — Phase 18 (RECL-04)
- **Own-nonce exemption** — fallback only (D-01)
- Reviewed Todos (not folded): `bridge-race-not-all-11-mint-within-window` (P1), `bridge-startup-wiring-mock-rpc` (P1) — stopword-noise matches, bridge scope, remain pending
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PRUNE-01 | A transaction is erasable only when every output is genuinely consumed (`IsOutPointGenuinelyConsumed`, not synthesized zero-amount tombstones) AND its depth beyond the current DAG head exceeds the configured finality window | Genuine-consumed discriminator verified (`UTXOManager.cpp:777-783`); window = spend-recency via **Candidate B** (A disproven — see D-02 section); output enumeration via `GetProducedUTXOs` (`GeniusTransaction.cpp:118-141`); priority read via `CrdtSet::GetPriority` (needs passthrough); maxHeight via `GetCRDTHeadList` |
| PRUNE-03 | A delta block is erased only when every element it carries is prune-eligible (parse the block at prune time; deltas are multi-element) | `GlobalDB::GetLocalDeltaKeyValues` is public (`globaldb.hpp:349`); unfiltered parse path `GetNodeWithoutRequest + GetDeltaFromNode(node, true)` verified; Delta proto carries `elements` + `tombstones` + `priority` — the gate must cover BOTH lists (the precedent returns elements only) |
| GATE-01 | Pruning runs only on `NodeType::Light` (fail-closed gate on the enum) and only within the `/bc-<netid>/tx/` prefix allowlist | `NodeType` enum (`NodeType.hpp:34-40`); TransactionManager already receives and stores `node_type_m` (`TransactionManager.hpp:155, :506, :669`); `TRANSACTION_BASE_FORMAT` + `GetBlockChainBase` verified (`TransactionManager.cpp:2235-2242`); fail-closed construction + out-of-prefix rejection + ctest design below |
| GATE-04 | Outgoing `previous_hash` resolution survives pruning of the node's own frontier transaction (cert-slot based resolution; certificates are never pruned) — must land before the first real prune | Ladder mapped (`TransactionManager.cpp:1660-1686`); value-dependent rungs identified (`:1847` FetchTransaction; `:1870` CRDT scan); minimal generalization = relax `:1720` type restriction; cert store API verified (`GetCertificateBySlot`, `/cert/` prefix, slot = `<addr>:<nonce>`); replay-protection consumer already cert-resolves (`TransactionConsensusHandler.cpp:666-688`); fixture machinery exists (`BuildSignedCertificate`) |
| GATE-06 | Pruning config knobs live beside `node_type` in `sgns_config.json`, default off, resolved values INFO-logged | `LoadSgnsConfig` pattern verified (`GeniusNode.cpp:389-518`): rapidjson `HasMember` + type check + INFO resolved value + safe default; knob schema + fail-closed validation designed below |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **C++17 only** — no C++20; Allman braces; brace all control statements; `enum class`; explicit casts; `constexpr` over macros; `.hpp/.cpp`; GNUS naming; Doxygen file headers + documented public functions.
- **`outcome::result<T>`** for fallible operations — the evaluator, config parse, cert resolution, and marker lookup return results; errors are part of the contract; never swallow failures.
- **One owning module per domain rule** — the spend-recency rule change lands in `UTXOManager::ConsumeUTXOs` (UTXO lifecycle owner); the previous_hash generalization lands in `TransactionManager` (nonce-chain owner); key-layout knowledge stays in owners (`CrdtSet` owns `/crdt/k…`, `TransactionManager` owns tx paths, the marker owns `/prune/`).
- **Never duplicate logic** — spend-recency has ONE home (B's populate-at-ConsumeUTXOs + one priority-lookup); the allowlist prefix check has ONE home; the window arithmetic has ONE home (the evaluator), reused verbatim by Phase 18's TxPruner. Grep before writing: `spent_by_txid` writers must be exactly ConsumeUTXOs (populate) and RestoreConsumedUTXOs (reset).
- **No domain logic in the transport layer** — evaluator is domain code; nothing in gRPC/relay surfaces.
- **Refactor first, then change** — the cert-rung generalization is a behavior-preserving refactor of `GetRegisteredOutgoingPreviousHash`'s binding logic + a behavior change (type restriction relaxed); land the binding refactor separately if the diff mixes both.
- **Verification is a gate** — clang-format/clang-tidy on changed files (test targets auto-exempt via `disable_clang_tidy`), build affected targets, run relevant tests; never claim a check passed that was not run.
- **Build-system pre-PR checklist** applies if CMakeLists changes (expected: only new test-file lines in existing `addtest` targets).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Spend-recency signal (write side) | `UTXOManager` (`src/account`) | — | Sole owner of outpoint lifecycle + the `/utxo/` records; `spent_by_txid` population belongs in `ConsumeUTXOs`, its reset already in `RestoreConsumedUTXOs` |
| Spend-recency signal (read side: spender priority) | `CrdtSet` via `CrdtDatastore`/`GlobalDB` passthrough | — | `CrdtSet::GetPriority` owns `/crdt/k/<key>/p`; no other module may read the raw priority key |
| maxHeight (window reference) | `CrdtHeads` via `GlobalDB::GetCRDTHeadList()` | — | D-C4: existing API returns `{topic→CIDs, maxHeight}` (`crdt_heads.cpp:259-284`) |
| Genuine-spend discriminator | `UTXOManager::IsOutPointGenuinelyConsumed` | — | D-C2 locked; `UTXOManager.cpp:777-783` |
| Eligibility evaluation (dry-run rule) | new evaluator in `src/transaction/` (placement discretion) | `TransactionManager` (enumeration + CID map) | The pruning rule is a transaction-domain rule; D-03 requires a pure structured result reusable by Phase 18's TxPruner |
| Candidate enumeration | `TransactionManager`/`GlobalDB::QueryKeyValues` | — | Reuses the `QueryTransactions` prefix-scan shape (`:2473-2503`) read-only |
| Block-parse gate (PRUNE-03) | `CrdtDatastore::GetLocalDeltaKeyValues` via `GlobalDB` | — | D-C3 locked; public on GlobalDB (`globaldb.hpp:349`) |
| NodeType fail-closed gate | evaluator construction (takes `NodeType`) | `TransactionManager::node_type_m` (`hpp:669`) | Evaluated once at construction; fail-closed (refuse unless Light) |
| Prefix allowlist | evaluator (one check function) | — | Built from `TRANSACTION_BASE_FORMAT`/`GetBlockChainBase` — D-C7 |
| previous_hash cert-slot resolution | `TransactionManager` (nonce-chain owner) | `Blockchain/ConsensusManager::GetCertificateBySlot` (cert store) | D-01; the ladder and its rungs live in TransactionManager |
| Config surface | `GeniusNode::LoadSgnsConfig` | — | D-C5; rapidjson convention beside `node_type` |
| `/prune/` marker design | new marker contract in `src/transaction/` (pruner-private) | `GlobalDB::GetRaw/PutRaw/QueryRaw` (node-local raw surface) | D-04; `CRDTWorkJournal` `/crdt/work/` precedent (`crdt_work_journal.hpp:117`) |
| Dry-run wiring (production INFO cycle) | `GeniusNode` (beside `TransactionManager::New`, `GeniusNode.cpp:1121`) | — | Config-gated, default-off ⇒ zero construction |

## D-02 Window Mechanism: Candidate A Verification (FAILED) and Candidate B Specification

> This is the phase's load-bearing finding. Every claim below was traced in source this session.

### Candidate A verification, point by point

**(a) Do CONSUMED entries persist as CRDT keys with per-key priority? — NO.**
Trace: `ConsumeUTXOs` (`UTXOManager.cpp:265-365`) flips the in-memory `utxo_outpoints_` entry to `UTXO_CONSUMED` under `unique_lock(utxos_mutex_)`, then persists via `StoreUTXOsLocked(owner)` (`:348`) → `StoreUTXOSnapshot(db, owner, entries)` (`:985-1071`). `StoreUTXOSnapshot`:
- queries existing records with `db->query(prefix)` (`:996-1004`) — the raw wrapper's prefix scan,
- stages `batch->remove(old)` for the whole address bucket (`:1006-1017`),
- stages `batch->put(BuildUTXORecordKey(address, outpoint), serialized_proto)` per entry (`:1052-1059`),
- commits once (`:1063`).

`db` is `UTXOManager::db_` — `std::shared_ptr<storage::rocksdb>` (`UTXOManager.hpp:532`), the RAW storage facade. `BuildUTXORecordKey` = `fmt::format("/utxo/{}/{}/{}:{}", owner, txid, idx)` (`UTXOManager.cpp:18-24`) — a raw key outside the `/crdt/` namespace. No `GlobalDB::Put`, no `CrdtDatastore::PutKey`, no `CrdtSet::SetValue/SetPriority` is ever invoked for UTXO records; `UTXOManager` holds no GlobalDB reference at all (wiring: `account_m->GetUTXOManager().LoadUTXOs( globaldb_m->GetDataStore() )`, `TransactionManager.cpp:3156`). A raw-key put creates **no** `/crdt/k/<utxo-key>/p` entry. Grep confirms no other `/utxo` writer in `src/` (only the migration's legacy-prefix constant read, `Migration3_6_0To3_7_0.cpp:21`).

**(b) Can the evaluator enumerate consumed entries and the outpoint→tx linkage?** The entries ARE enumerable (raw `/utxo/` prefix + the in-memory map), and the outpoint↔owner linkage exists — but the entry carries **no spend-height signal**: `spent_epoch` and `spent_by_txid` are `std::nullopt` everywhere today (set to nullopt at `PutUTXO` `:178-179`; synthesized CONSUMED tombstone entries carry two nullopts `:305-310`; the only other writers are the resets in `RestoreConsumedUTXOs` `:389-390`). So enumeration alone cannot answer "when was this spent".

**(c) Does the consumed entry's priority update on any later touch?** Moot — there is no priority. The relevant adjacent fact for B: the raw record IS rewritten on every later address-bucket touch (whole-bucket remove+reput), and fields inside the serialized record survive those rewrites because the in-memory entry carries them (`:1038-1043` serialize; `:925-937` restore on load).

**Verdict: Candidate A fails at its first premise. Per D-02, Candidate B is the mechanism, and the REQUIREMENTS.md out-of-scope row ("Populating `spent_epoch`/`spent_by_txid` schema fields | Dead schema — never populated at runtime; window comes from height/priority or certificates") must be amended in this phase's plan. Narrow amendment: `spent_by_txid` becomes the spend-recency carrier per D-02 Candidate B; `spent_epoch` stays unpopulated (wall-clock-adjacent, and D-02 forbids wall-clock semantics).**

### Candidate B specification (verified against source)

1. **Populate at spend:** in `ConsumeUTXOs`, set `entry.spent_by_txid = <consuming tx hash>` for genuinely-found entries (and uniformly for synthesized zero-amount tombstones is acceptable — the `amount != 0` discriminator at `IsOutPointGenuinelyConsumed` already excludes those from eligibility; uniform population is simpler and keeps "one rule per field").
2. **Signature extension:** `ConsumeUTXOs(infos, address, type)` has no spender identity today. Every call site has the spending transaction in scope: `:2637` (`TransferInputOwner(*transfer_tx)` — the tx object), `:2657` (`migration_tx`), `:2702`, `:2871` (`escrow_tx->GetSrcAddress()`). Add an overload/parameter carrying the consuming tx hash (planner picks exact shape; keep existing callers compiling).
3. **Window rule (the ONE home for this arithmetic — the evaluator):** candidate T is window-eligible ⟺ `maxHeight − max(priority(spender(output_i))) ≥ N` over all outputs i — i.e. the **most-recent spend** across outputs binds. Spender path from hash: `ResolveTransactionPathAndCID` (landed, `TransactionManager.hpp:864`) or `GetTransactionPath`. Spender priority: `CrdtSet::GetPriority(path)` (public, `crdt_set.hpp:208`) — needs a small passthrough (see Wave 0).
4. **Lifecycle symmetry (already present, verified):** CONSUMED→READY revert resets both fields (`RestoreConsumedUTXOs`, `:389-390`); `LoadUTXOs` restores them (`:925-937`); the rollback-repair path re-stores from restored memory so disk and memory agree (`:352-358`).
5. **Spender-pruned-first edge (Phase 18 relevance, design now):** if the spender's own CRDT entries were already erased (possible only after Phase 18 exists), `GetPriority` fails. Sound resolution: a tx can only be pruned when its own window elapsed ⇒ `maxHeight − p(spender) ≥ N` holds by construction ⇒ treat "spender priority unavailable because spender is in the pruned-marker set" as window-satisfied, with an explicit reason code — never silently.
6. **Restart:** fields are durable in the raw record and restored at boot — spend-recency survives restart with zero extra work.

### Why not a third option (considered, rejected)

A per-cycle full scan that parses every tx's inputs to reconstruct the outpoint→spender map in memory would avoid the schema change — but it parses O(all transactions) per cycle (the candidate scan parses only candidates), duplicates the spend knowledge the UTXO record already owns, and loses the signal entirely once spenders are pruned. B is the auditable, O(1)-per-outpoint mechanism the owner pre-approved as fallback.

## Standard Stack

**No new external dependencies, no vendored changes, no new libraries.** Everything rides existing internal APIs (verified this session):

### Core
| API | Location | Purpose | Why Standard |
|-----|----------|---------|--------------|
| `UTXOManager::IsOutPointGenuinelyConsumed` | `UTXOManager.cpp:777-783` (decl `hpp:397`) | Spend discriminator (CONSUMED ∧ amount ≠ 0) | D-C2 locked; takes `shared_lock(utxos_mutex_)` per call |
| `UTXOManager::ConsumeUTXOs` / `RestoreConsumedUTXOs` | `UTXOManager.cpp:265-365` / `:367-407` | Candidate B populate site / existing reset site | Sole owner of the outpoint lifecycle + `/utxo/` records |
| `GeniusTransaction::GetProducedUTXOs` | `GeniusTransaction.cpp:118-141` (decl `hpp:150`) | Output enumeration per candidate (idx 0..N-1 with amounts/tokens/dests) | Returns `nullopt` for tx types without UTXO params — see Pitfall 6 |
| `CrdtSet::GetPriority(key)` | `crdt_set.hpp:208`, impl `crdt_set.cpp:498-519` | Spender depth read (`/crdt/k/<key>/p`) | Owner of the priority key; needs `CrdtDatastore`→`GlobalDB` passthrough (Wave 0 — no public priority read exists above CrdtSet; `set_` is private, `crdt_datastore.hpp`) |
| `GlobalDB::GetCRDTHeadList()` | `globaldb.hpp:327` → `CrdtHeads::GetList` (`crdt_heads.cpp:259-284`) | maxHeight (window reference) | D-C4: existing API; `CRDTListResult{topic→CIDs, maxHeight}` |
| `GlobalDB::QueryKeyValues(prefix)` | `globaldb.hpp:148-157` | Candidate enumeration (per monitored network) | Same surface `QueryTransactions` uses (`TransactionManager.cpp:2473-2503`), read-only |
| `TransactionManager::GetMonitoredNetworkIDs()` | `TransactionManager.cpp:2224` (decl `hpp:555`) | Which `/bc-<net>/` prefixes are valid | One home for the network set (allowlist derives from it) |
| `TransactionManager::GetBlockChainBase(net)` / `TRANSACTION_BASE_FORMAT` | `TransactionManager.cpp:2235-2242` / `hpp:433` | Allowlist prefix construction | D-C7 locked constant |
| `GlobalDB::GetLocalDeltaKeyValues(cid_string)` | `globaldb.hpp:349` → `crdt_datastore.cpp:2280-2302` | Block-parse gate: `CID::fromString` → `GetNodeWithoutRequest` → `GetDeltaFromNode(node, true)` (unfiltered) | D-C3 locked; read-only, never fetches |
| `Blockchain::GetCertificateBySlot(slot_key)` | `Blockchain.hpp:322` → `ConsensusManager::GetCertificateBySlot` (`Consensus.cpp:4730-4758`) | Cert-slot reads (`/cert/<slot>`, slot-key recheck + `ValidateCertificate`) | Certificates are never pruned (outside allowlist) |
| `ConsensusManager::DecodeNonceSubject` / `CreateNonceSubject` | used at `TransactionManager.cpp:1709`, `:476-481` (test) | Extract `{nonce, tx_hash, embedded tx}` from cert subject | The cert's winning-tx-hash binding (`nonce_subject.tx_hash()`) |
| `TransactionManager::GetOutgoingPreviousHash(nonce)` | `TransactionManager.cpp:1660-1686` | The ladder D-01 generalizes | 4 rungs; consumers `:1556` (FillDAGStruct) + `TransactionConsensusHandler.cpp:631` |
| `GeniusNode::LoadSgnsConfig` | `GeniusNode.cpp:389-518` | Config surface for `pruning.*` knobs | D-C5; rapidjson `HasMember`+type-check+INFO+default convention |
| Phase-16 substrate: `EraseTransactionEntry`, `ResolveTransactionPathAndCID`, `EraseEntry` | `TransactionManager.hpp:864, :912, :938`; `globaldb.hpp:248`, impl `globaldb.cpp:797` | Test-side simulation of the pruned state (SC-3); CID/path resolution | Landed, documented, zero production callers — tests are the sanctioned consumers |

### Supporting
| API | Location | Purpose | When to Use |
|-----|----------|---------|-------------|
| `GlobalDB::GetRaw/PutRaw/QueryRaw` | `globaldb.hpp:174-196` | Node-local raw surface — the `/prune/` marker read/write vehicle (D-04) | Marker contract tests now; marker writes Phase 18 |
| `CRDTWorkJournal::NAMESPACE_PREFIX = "/crdt/work/"` | `crdt_work_journal.hpp:117` | Precedent: node-local prefix, direct datastore writes, never broadcast | Marker design reference |
| `GlobalDB::GetTransactionCID(tx_hash)` | `TransactionManager.hpp` (public, ~`:903`); accessor wired at `.cpp:295-301` | Candidate's CID (block-gate input) | Already searches all monitored networks |
| `UTXOManager` test seams | `SetFailNextPutUTXOStoreForTest` etc., `UTXOManager.cpp:967-983` | Fault injection for B's populate path | B's unit tests |
| `CrdtDatastore::AddHead(cid, topic, priority)` | `crdt_datastore.cpp:2270` | Fixture: drive maxHeight beyond the window deterministically | SC-1/SC-3 window fixtures |
| `::test::CRDTFixture` | `test/testutil/storage/base_crdt_test.hpp:27` | Real GlobalDB + gossip pubsub + temp dir base | SC-1/SC-3/SC-4 fixture family |
| Certificate fixture helpers | `transaction_manager_certificate_fallback_test.cpp:421-533` | `PersistTransaction`, `BuildSignedCertificate` (subject→proposal→vote→certificate), `PersistCertificateAtSlot`, `FetchAndProcess` | SC-3 and B fixtures |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Candidate B (`spent_by_txid`) | Per-cycle input-scan reverse map | **Rejected** — O(all txs) parse per cycle, duplicates owned knowledge, dies when spenders are pruned |
| Candidate B | `spent_epoch` = height captured at spend time | **Rejected as primary** — at `ConsumeUTXOs` time the delta's height is not yet known to UTXOManager (the tx is mid-processing); B's spender-priority lookup IS the DAG-height authority. (`spent_epoch` stays unpopulated) |
| Generalizing cert rung 3 in place | New dedicated `ResolvePreviousHashFromCertificates` entry point | **Use in-place relaxation** — the rung, its binding checks, and its position in the ladder already exist; a parallel function would duplicate the binding logic (hard invariant 2) |
| Relaxing rung-3 type check only | Reordering the ladder (cert rung first) | **Keep the order** — rungs 1-2 answering first preserves current behavior exactly when history is intact; reorder changes hot-path cost and behavior surface for no gain |
| New marker prefix encoding (path-embedded keys) | `/prune/tx/<tx-hash>` with record value | **Use hash-keyed** — lookups are by hash (SURV-02/AccountMessenger shape); the path travels in the value |

**Installation:** none — zero registry installs. (Package audit: not applicable.)

## Package Legitimacy Audit

No external packages are installed, recommended, or required. All work lands in existing internal libraries (`transaction`, `account`, `crdt_*`) and existing test targets (`test/src/account/*`, optionally `test/src/crdt/*`). Vendored dependencies untouched. Slopcheck gate: not applicable (zero registry installs).

## Architecture Patterns

### System Architecture Diagram

```
                       ┌──────────────────────────────────────────────────────┐
                       │ GeniusNode::LoadSgnsConfig  (D-C5)                   │
                       │  pruning.{enabled=false, window=N, dry_run}          │
                       │  absent keys ⇒ nothing constructed (SC-5)            │
                       └───────────────┬──────────────────────────────────────┘
                                       │ config resolves (INFO-logged)
                       ┌───────────────▼──────────────────────────────────────┐
                       │ GATES (fail-closed, evaluated once)                   │
                       │  NodeType == Light? ── no ─▶ REFUSE (GATE-01)         │
                       │  candidate key ∈ /bc-<net>/tx/ allowlist (D-C7)?      │
                       └───────────────┬──────────────────────────────────────┘
                                       │ pass
  ┌────────────────────────────────────▼───────────────────────────────────────┐
  │ EVALUATOR (pure, committed state only — D-C6; timer-driven, never callback) │
  │ 1. enumerate: QueryKeyValues(GetBlockChainBase(net) + "tx/") ∀ monitored net│
  │ 2. per candidate T: DeSerializeTransaction → GetProducedUTXOs               │
  │       outputs = ∅/nullopt ─▶ INELIGIBLE (fail-closed, reason)               │
  │ 3. ∀ output outpoint: IsOutPointGenuinelyConsumed?  (D-C2)                  │
  │       any false ─▶ INELIGIBLE (reason: unspent/synthesized)                 │
  │ 4. window (D-02/B): spender_i = spent_by_txid(outpoint_i)                   │
  │       spend_height = max over outputs of priority(spender_i path)           │
  │       maxHeight − spend_height ≥ N ?  (maxHeight from GetCRDTHeadList)      │
  │ 5. structured result: {eligible[{path,cid,reasons}], ineligible[{path,     │
  │       reasons}]}  (D-03)  → per-cycle INFO summary in production dry-run    │
  └───────────────┬────────────────────────────────────────────────────────────┘
                  │ eligible set (cid per tx from GetTransactionCID)
  ┌───────────────▼─────────────────────────────────────────────────────────────┐
  │ BLOCK-PARSE GATE (PRUNE-03, dry-run: reads only, erases nothing)            │
  │  GlobalDB::GetLocalDeltaKeyValues(cid) → elements' keys + tombstones' keys  │
  │  block erasable ⟺ EVERY key ∈ eligible set ∧ every key allowlist-conformant │
  └───────────────┬────────────────────────────────────────────────────────────┘
                  │ (Phase 17: report only. Phase 18: drives EraseTransactionEntry)

  ┌──────────────────────────────────────────────────────────────────────────────┐
  │ GATE-04: previous_hash ladder after generalization                           │
  │  GetOutgoingPreviousHash(nonce):                                             │
  │   rung1 tracked (in-memory) ─▶ rung2 persisted head (FetchTransaction —     │
  │   READS TX VALUE :1847; fails gracefully when pruned) ─▶ rung3 CERT-SLOT     │
  │   (GENERALIZED: any certified embedded tx from /cert/<addr>:<nonce-1>;       │
  │    value-independent) ─▶ rung4 CRDT scan (value-dependent last resort)      │
  │  Consumer B: TransactionConsensusHandler replay protection — already         │
  │  cert-resolves missing predecessors (:666-688) — verified pruning-tolerant   │
  └──────────────────────────────────────────────────────────────────────────────┘

  ┌──────────────────────────────────────────────────────────────────────────────┐
  │ D-04 MARKER (design this phase; write Phase 18; answer Phase 19)             │
  │  /prune/tx/<tx-hash> → PrunedTransactionRecord{tx_path, cid, erased_height}  │
  │  node-local (PutRaw), never broadcast, joins the Phase-18 erase WriteBatch    │
  │  lookup: marker∧¬value ⇒ PRUNED · ¬marker∧¬value ⇒ NOT_FOUND                 │
  └──────────────────────────────────────────────────────────────────────────────┘
```

Entry points: config resolution → gates → enumeration. Decision points: Light/allowlist gates, per-output genuine-consumed, window check, block-gate all-eligible. A reader traces: config → gate → candidate → outputs → spenders → window → eligible set → block gate. No erasure anywhere (dry-run).

### Recommended Project Structure

```
src/transaction/
├── TransactionManager.hpp / .cpp          # MODIFIED — generalized cert rung (D-01);
│                                            candidate enumeration exposure if the
│                                            evaluator is standalone (planner choice)
src/account/
├── UTXOManager.hpp / .cpp                 # MODIFIED — Candidate B: spent_by_txid
│                                            populated at ConsumeUTXOs (+ signature
│                                            carries spender hash); resets already exist
src/account/GeniusNode.cpp / .hpp          # MODIFIED — LoadSgnsConfig pruning knobs;
│                                            dry-run wiring beside TransactionManager::New
│                                            (GeniusNode.cpp:1121), config-gated
src/crdt/
├── crdt_datastore.hpp (+globaldb.hpp)     # MODIFIED (~2 lines) — GetPriority passthrough
└── (nothing else — Phase-16 substrate is complete)
src/transaction/ (or wherever the one-owner rule puts it)
└── PruneEligibility.hpp / .cpp (+ marker  # NEW — evaluator pure result + reasons;
    record header if separate)              #   /prune/ marker constants + record +
│                                            lookup contract (D-04 design)
test/src/account/
└── prune_eligibility_test.cpp (or split)  # NEW — SC-1 exact set, SC-3 cert-slot,
                                            # SC-4 gates, B unit tests (CRDTFixture base)
```

No new build targets; new test files join existing `addtest` targets in `test/src/account/CMakeLists.txt` (the fixture family and its link set — including `transaction_manager_certificate_fallback_test`'s dependencies — already live there).

### Pattern 1: Pure structured-result evaluator (D-03)
**What:** a stateless evaluation over inputs (window N, NodeType, GlobalDB&, UTXOManager&) returning `{eligible, ineligible}` with per-item reason enums. No I/O of its own beyond the references it is handed; no scheduling; no erasure. Phase 18's TxPruner reuses it verbatim and adds the re-verify-under-lock + erase flow.
**Why:** SC-1's exact-set assertion targets exactly this object; fixtures construct the DB state, call `Evaluate()`, compare sets.

### Pattern 2: Fail-closed construction + one allowlist home
**What:** the evaluator refuses at construction unless `NodeType::Light`; candidate keys are checked against `GetBlockChainBase(net) + "tx/"` for net ∈ `GetMonitoredNetworkIDs()` — one function, one home. Out-of-prefix keys get a reason code and appear in the ineligible set (visible in the INFO summary), never silently dropped.
**Precedent:** `NodeType.hpp` exists specifically so lower layers take a NodeType without the GeniusNode facade (header comment, `NodeType.hpp:1-8`); `TransactionManager::New` already takes `node_type` (`hpp:155`).

### Pattern 3: Cert-slot binding discipline (D-01)
**What:** the generalized rung keeps every binding check the registration rung has today — `DecodeNonceSubject` succeeds, embedded tx case set, `nonce == nonce-1`, `account_id == own address`, embedded deserializes, embedded `GetHash()` returned — only the `GetType() == "registration"` restriction (`TransactionManager.cpp:1720`) is relaxed. The cert is the single authority (same discipline as `CertificateMatchesTransaction`, `:1790-1816`, and the replay handler's cert fallback, `TransactionConsensusHandler.cpp:666-688`).

### Pattern 4: Marker as erase-batch participant (D-04 design)
**What:** `/prune/tx/<tx-hash>` → proto record `{tx_path, cid, erased_at_height}` written via the node-local raw surface. Contract: the marker write joins the SAME WriteBatch as the Phase-16 erase (Phase 18), establishing the invariant **value XOR marker** atomically — a crash can never leave both or neither. Lookup: `LookupPrunedEntry(tx_hash) → record | NOT_FOUND`; the PRUNED-vs-NOT_FOUND distinction is `marker ∧ ¬value` vs `¬marker ∧ ¬value`; `marker ∧ value` is an invariant violation (report loudly, never guess).
**Precedent:** `CRDTWorkJournal` node-local prefix discipline; proto-record precedent `SGTransaction::UTXOEntryRecord`.

### Anti-Patterns to Avoid
- **Window on creation priority** (`GetPriority(candidate)`) — exactly the owner's counterexample; the fixture must contain the counterexample case (ancient tx, recent spend ⇒ ineligible).
- **Bare `IsOutPointConsumed`** — synthesized zero-amount tombstones count as consumed (D-C2).
- **Enumerating with prefix `base + "tx"`** (no trailing slash) — matches sibling keys like `…/txfoo`; use `base + "tx/"` (Phase-16 trailing-slash lesson; note `QueryTransactions` itself uses the slashless form at `:2477` — do not copy it).
- **Gate on "elements I applied"** — parse the block unfiltered (`GetDeltaFromNode(node, true)`); light nodes store elements they never applied (PITFALLS 2).
- **Evaluator work on the CRDT callback path** (D-C6) — callbacks fire pre-commit; timer/loop only.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Spend-recency signal | Timestamps, wall-clock epochs, in-memory caches | `spent_by_txid` + `CrdtSet::GetPriority(spender)` + `GetCRDTHeadList` maxHeight | DAG heights are the authority; wall-clock races finality (D-02); the field + resets + persistence already exist |
| Spend discriminator | Amount/state re-derivation from tx inputs | `IsOutPointGenuinelyConsumed` | Owner API; the zero-amount tombstone subtlety is encoded there (D-C2) |
| Block element enumeration | Raw protobuf parse of block bytes | `GlobalDB::GetLocalDeltaKeyValues` (extended to tombstones) | D-C3; the unfiltered parse path exists and is the one home for delta-shape knowledge |
| maxHeight | Head-table scans, new head APIs | `GlobalDB::GetCRDTHeadList()` | D-C4 — explicitly locked, no new API |
| Cert reading/validation | Direct `/cert/` raw reads + manual validation | `Blockchain::GetCertificateBySlot` | Slot-key recheck + `ValidateCertificate` are inside; one authority |
| tx path/CID resolution | String-building paths per network | `GetTransactionPath` / `ResolveTransactionPathAndCID` / `GetTransactionCID` | Landed Phase-16 shape; network-consistent pairing is its documented contract |
| Simulating the pruned state in tests | Deleting raw keys by hand | `TransactionManager::EraseTransactionEntry` (Phase-16, test-sanctioned consumer) | Exercises the real guarded atomic flow — the SC-3 proof is then about the real post-prune state |
| Marker storage | New column family / separate DB / CRDT-published marker | `/prune/` prefix via `PutRaw/GetRaw/QueryRaw` in the shared DB | D-04; CRDTWorkJournal precedent; a CRDT-published marker would broadcast node-local knowledge |

**Key insight:** Phase 17 is decision + composition work on verified substrates; the only new *mechanisms* are B's 4-line populate (+ signature) and the rung-3 relaxation — both in the modules that own those domains.

## Common Pitfalls

### Pitfall 1: Building on Candidate A's phantom priority
**What goes wrong:** implementing `maxHeight − priority(/crdt/k/<utxo-key>/p)` compiles nothing — the key family does not exist for `/utxo/` records; lookups fail at runtime or, worse, someone "fixes" it by CRDT-publishing UTXO keys (replicates node-local state network-wide).
**How to avoid:** B is the mechanism (this research, D-02 section). The REQUIREMENTS.md out-of-scope row must be amended in the plan (D-02's conditional).
**Warning signs:** any code constructing `/crdt/k//utxo…` keys; any `GlobalDB::Put` of a `/utxo/` key.

### Pitfall 2: Creation-age window regression (the owner's counterexample)
**What goes wrong:** using the candidate tx's own priority (`/crdt/k/<bc-N/tx/hash>/p`, set at creation) silently reverts D-02 — a just-spent ancient UTXO becomes instantly prunable.
**How to avoid:** the window arithmetic reads the SPENDER's priority only; the SC-1 fixture MUST include: ancient-creation tx whose outputs were all spent recently ⇒ INELIGIBLE, and recent-creation tx spent recently ⇒ INELIGIBLE, and ancient tx spent anciently ⇒ ELIGIBLE (three-way discriminator).
**Warning signs:** `GetPriority(candidate_path)` anywhere in the window check.

### Pitfall 3: Most-recent-spend binding
**What goes wrong:** averaging, first-output, or any-output semantics for the window (outputs of one tx can be spent at different times by different spenders).
**How to avoid:** window uses `max` over outputs of `priority(spender_i)` (the latest spend binds). One home (the evaluator), fixture with split-time spends (output 0 spent anciently, output 1 recently ⇒ ineligible).

### Pitfall 4: `GetLocalDeltaKeyValues` returns elements only
**What goes wrong:** the block gate checks `delta.elements()` keys and misses `delta.tombstones()` keys (`delta.proto`: both are `repeated Element`); a block carrying a tombstone for a live key is judged erasable.
**How to avoid:** the gate enumerates elements AND tombstones; every key must be in the eligible set and allowlist-conformant. (The precedent function's own TODOs — "not filtering", "just grabbing elements" — are documented gaps, not contracts.)
**Warning signs:** a gate that iterates only `elements()`.

### Pitfall 5: Prefix discipline (trailing slash + monitored networks)
**What goes wrong:** `base + "tx"` matches sibling keys; or a crafted `/bc-<unmonitored>/tx/…` key passes a naive string check.
**How to avoid:** allowlist = key starts with `GetBlockChainBase(net) + "tx/"` for net ∈ `GetMonitoredNetworkIDs()` — one function, ctest-asserted with crafted out-of-prefix keys (proof keys, `/cert/`, `reg/`, wrong-net `/bc-999/tx/`, slashless sibling `…/txfoo`) — each rejected with a reason.

### Pitfall 6: Vacuous eligibility for no-output transactions
**What goes wrong:** `GetProducedUTXOs()` returns `nullopt` for tx types without UTXO parameters (`GeniusTransaction.cpp:121-126`); "every output consumed" is vacuously true.
**How to avoid:** fail closed: `nullopt` ⇒ INELIGIBLE with an explicit reason code (planner may override, but the default posture for a destructive decision is refusal). Note registrations don't live under `tx/` at all, so the exposure is other no-UTXO tx types under `tx/`.

### Pitfall 7: Ladder behavior change beyond the pruned case
**What goes wrong:** the generalized rung 3 now answers in situations where today the (slow) rung-4 CRDT scan answers — if the binding checks diverge, the two rungs can return different hashes for the same nonce.
**How to avoid:** rung 3 keeps the identical binding discipline as `CertificateMatchesTransaction` + the replay handler's fallback (same cert authority ⇒ same hash by construction); add an equivalence test: for certified non-registration predecessors, rung-3-generalized result == rung-4 scan result.

### Pitfall 8: NodeType-default-Light mistaken for consent
**What goes wrong:** `LoadSgnsConfig` defaults `node_type` to Light on missing/unrecognized key (`GeniusNode.cpp:448-456`); gating pruning on NodeType alone would enable evaluation on nodes that never opted in.
**How to avoid:** the consent gate is the pruning config (default off); NodeType==Light is the *additional* fail-closed role gate (GATE-01's two conjuncts). Absent `pruning` object ⇒ nothing constructed, no thread, no log cycle — byte-identical behavior (SC-5).

### Pitfall 9: Evaluator triggered from CRDT callbacks (D-C6)
**What goes wrong:** wiring evaluation onto `NewElementCallback` reads pre-commit state (proven repo invariant — cert-receipt memory).
**How to avoid:** timer-driven cycle on committed state only; the INFO summary is emitted from the cycle, not from callbacks.

### Pitfall 10: Spender identity lost for synthesized tombstones / reverts
**What goes wrong:** B populates `spent_by_txid` on the genuine path but the revert path (`RestoreConsumedUTXOs`) or a bucket rewrite drops it inconsistently, or rollback-repair persists a stale value.
**How to avoid:** verified in source: reverts reset both fields (`:389-390`), rollback-repair re-stores from restored memory (`:352-358`), snapshot serialization carries the fields (`:1038-1043`). B's tests should assert field round-trip through spend → bucket-rewrite → restart-load and spend → revert → reset.

### Pitfall 11: Dry-run fixture without real blocks/CIDs
**What goes wrong:** candidates stored via raw `Put`-local seeds have no delta blocks, so the block gate vacuously passes/fails and SC-2 proves nothing.
**How to avoid:** seed through the real publish path on a real topic (Phase-16 STATE lesson: empty-topics Puts create no destinations and no heads) or persist+seal CIDs explicitly; assert block presence before gating.

### Pitfall 12: Cert-validation cost on the resolution path
**What goes wrong:** `GetCertificateBySlot` runs `ValidateCertificate` (signature checks) on every call (`Consensus.cpp:4751-4757`); a per-candidate cert sweep in the evaluator would be hot.
**How to avoid:** the evaluator needs NO certs for eligibility (B is priority-based; cert-tightening is a deferred idea). Certs are read only by the previous_hash resolution path (per outgoing tx, rare) and its tests.

## Code Examples

### Candidate B populate (shape — final signature is planner's)
```cpp
// Source: UTXOManager.cpp:265-365 (ConsumeUTXOs), :305-310 (synth tombstone),
//         :389-390 (existing resets in RestoreConsumedUTXOs)
outcome::result<bool> UTXOManager::ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos,
                                                 const std::string                &address,
                                                 UTXOType                          type,
                                                 const std::optional<base::Hash256> &consuming_txid /* NEW */ )
{
    ...
    if ( ( entry.state == UTXOState::UTXO_READY || entry.state == UTXOState::UTXO_RESERVED ) && ... )
    {
        utxo_found  = true;
        entry.state = UTXOState::UTXO_CONSUMED;
        entry.spent_by_txid = consuming_txid;   // <-- the spend-recency carrier (B)
    }
    ...
}
// Call sites with spender in scope: TransactionManager.cpp:2637 (transfer_tx->GetHash()),
// :2657 (migration_tx), :2702, :2871 (escrow_tx->GetHash()).
```

### Evaluator result + window check (shape)
```cpp
// Sources: GetCRDTHeadList (globaldb.hpp:327), CrdtSet::GetPriority (crdt_set.hpp:208 — via the
// new passthrough), GetProducedUTXOs (GeniusTransaction.cpp:118-141),
// IsOutPointGenuinelyConsumed (UTXOManager.cpp:777-783)
struct PruneCandidateResult
{
    std::string                 tx_path;
    std::optional<std::string>  cid;     // from GetTransactionCID
    bool                        eligible{ false };
    std::vector<PruneReason>    reasons; // enum class: UNSPENT_OUTPUT, SYNTHESIZED_TOMBSTONE,
                                         // SPEND_WITHIN_WINDOW, NO_OUTPUTS, OUT_OF_PREFIX,
                                         // SPENDER_PRUNED_WINDOW_SATISFIED, ...
};
// Window (ONE home — D-02/B): the MOST RECENT spend binds.
//   spend_height = max over outputs of GetPriority(path(spent_by_txid(outpoint_i)))
//   eligible_on_window = ( maxHeight - spend_height ) >= N
//   SPENDER priority unreadable AND spender in pruned-marker set  =>  window satisfied
//   (a pruned spender implies its own window elapsed => this spend is older still).
```

### Generalized cert rung (the one-line relaxation + kept discipline)
```cpp
// Source: TransactionManager.cpp:1688-1726 (GetRegisteredOutgoingPreviousHash)
// KEEP: nonce==nonce-1, account_id==own, subject decodes, embedded tx decodes.
// RELAX: the registration-only restriction:
-  if ( embedded.has_error() || !embedded.value() || embedded.value()->GetType() != "registration" )
+  if ( embedded.has_error() || !embedded.value() )   // any certified embedded tx type
   {
       return "";
   }
   return embedded.value()->GetHash();
// Rung order unchanged (TransactionManager.cpp:1660-1686): tracked -> persisted ->
// cert-slot(generalized) -> CRDT scan. Behavior when history is intact: rungs 1-2
// answer first, unchanged. Consumer already cert-resolves inbound replay checks
// (TransactionConsensusHandler.cpp:666-688).
```

### Config knobs (LoadSgnsConfig pattern — GeniusNode.cpp:411-457)
```cpp
// pruning.* — D-C5/GATE-06: default off; absent object = exactly today's behavior.
if ( config_json.HasMember( "pruning" ) && config_json["pruning"].IsObject() )
{
    const auto &p = config_json["pruning"];
    if ( p.HasMember( "enabled" ) && p["enabled"].IsBool() ) { pruning_enabled_ = p["enabled"].GetBool(); }
    if ( p.HasMember( "finality_window" ) && p["finality_window"].IsUint64() )
    {
        pruning_finality_window_ = p["finality_window"].GetUint64();
    }
    if ( p.HasMember( "dry_run" ) && p["dry_run"].IsBool() ) { pruning_dry_run_ = p["dry_run"].GetBool(); }
    // fail closed: enabled without a window => disable + WARN (N required when enabled — D-02)
    if ( pruning_enabled_ && !pruning_finality_window_.has_value() ) { pruning_enabled_ = false; /* WARN */ }
    node_logger_->info( "sgns_config.json: pruning enabled={} window={} dry_run={}", ... );
}
```

### Marker contract (design; write in Phase 18)
```cpp
// Key:   "/prune/tx/" + tx_hash            (node-local, PutRaw — never broadcast)
// Value: SGTransaction::PrunedTransactionRecord { string tx_path; string cid;
//                                                   uint64 erased_at_height; }
// Invariant (Phase 18): the marker joins the SAME WriteBatch as
//   TransactionManager::EraseTransactionEntry  =>  value XOR marker, atomically.
// Lookup:  marker ∧ ¬value ⇒ PRUNED;  ¬marker ∧ ¬value ⇒ NOT_FOUND;
//         marker ∧ value  ⇒ invariant violation — report, never guess.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| "spent_epoch/spent_by_txid are dead schema — never build on them" | `spent_by_txid` populated at spend time as the spend-recency carrier (D-02 Candidate B) | This phase (A failed verification) | REQUIREMENTS.md out-of-scope row amended; `spent_epoch` stays dead (wall-clock-adjacent) |
| Window concept = tx depth/creation age (roadmap PRUNE-01 wording) | Window = recency of the SPEND (D-02) | Owner decision 2026-10-16 | Roadmap wording superseded by CONTEXT D-02; evaluator implements spend-recency |
| Cert-slot previous_hash for registrations only | Cert-slot resolution for any certified predecessor (D-01) | This phase | Pruning-safe nonce chain; replay-validation consumer already cert-resolves |
| Dry-run = disabled service | Evaluator = pure policy + config-gated dry-run cycle (D-03/D-06) | This phase | Phase 18's TxPruner composes the same evaluator + service shell |

**Deprecated/outdated (this repo):** `/chain/<net>/tx` naming (stale — D-C7); "window comes from height/priority or certificates" REQUIREMENTS row (conditionally amended per D-02).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `StoreTransactionCID` now lives at `TransactionManager.cpp:4373` (drifted from the Phase-16-cited `:4322-4345` after Phase-16's edits) | Standard Stack | Line-citation drift only; contract unchanged (verified header decl `hpp:849`) |
| A2 | Build tree `build/OSX/Release` with `test_bin/{transaction_manager_certificate_fallback_test, utxo_manager_test, crdt_test, globaldb_integration_test}` present (verified by ls this session) | Validation Architecture | Wrong build-dir name in commands only |
| A3 | The evaluator's production dry-run trigger should be a minimal repeating timer (asio steady_timer, config-gated) rather than a new service — exact trigger shape is planner discretion; the CONTEXT only requires a "per-cycle INFO summary" in production dry-run mode | Architecture Patterns | A heavier scheduler than needed (scope creep into Phase 18) or none at all (D-03's production summary unmet) — planner must pick one and keep it minimal |
| A4 | `GetNodeWithoutRequest` is strictly local (no network fetch) per its name and the ARCHITECTURE research's traversal analysis — re-verified indirectly, not by reading its body this session | Block gate | If it could fetch, the gate would cause network traffic in dry-run; mitigation: it is the same call `GetLocalDeltaKeyValues` has always made synchronously |

**All other claims were verified against source this session** (file:line cited inline).

## Open Questions

1. **Evaluator placement — standalone header/class vs TransactionManager-owned section (discretion)**
   - What we know: the rule needs GlobalDB (query/priority/heads/delta-parse), UTXOManager queries, NodeType, config N; TxPruner (18) reuses it.
   - Recommendation: standalone `PruneEligibility` in `src/transaction/` (free functions + result structs; a class only if state justifies it) — keeps TransactionManager un-widened and gives Phase 18 a clean reuse seam. Decide in plan wave 0.
2. **Exact config key names + INFO line format (discretion)** — recommendation above: `pruning.enabled` / `pruning.finality_window` / `pruning.dry_run`.
3. **Whether B populates `spent_by_txid` on synthesized zero-amount tombstones** — recommendation: uniform (simpler; discriminator already excludes them); alternative: genuine-only. Either way, test it.
4. **Priority passthrough surface** — `CrdtDatastore::GetPriority` + `GlobalDB::GetKeyPriority` (recommended, mirrors existing passthroughs) vs exposing `CrdtSet` directly. Planner picks names.
5. **Whether the block-gate exposure extends `GetLocalDeltaKeyValues` (adding tombstones) or adds a sibling (e.g., `GetLocalDeltaKeys`)** — extending changes an existing public function's result set (currently elements-only); a sibling avoids touching existing callers. Recommendation: sibling returning keys-only (elements + tombstones), leaving the precedent untouched (open-closed with evidence: the second use just changed shape).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Configured build tree | incremental builds/tests | ✓ | `build/OSX/Release` (test binaries verified present) | reconfigure |
| cmake / ctest | build/test | ✓ | 3.31.4 | — |
| clang-format / clang-tidy | repo verification gate | ✓ (per 16-HUMAN-UAT; ran clean on changed lines) | — | must be run; state if unavailable |
| Vendored RocksDB / ipfs-lite-cpp | storage substrate (untouched) | ✓ | 9.4 / workspace fork | — |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** none.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest via repo `addtest` macro (`cmake/functions.cmake` — auto ctest registration, auto `disable_clang_tidy`) |
| Config file | none — per-target registration in `test/src/account/CMakeLists.txt` (fixture family + link set already established there) |
| Quick run command | `cmake --build build/OSX/Release --target prune_eligibility_test && build/OSX/Release/test_bin/prune_eligibility_test --gtest_filter='*PruneEligibility*'` (target name TBD by planner) |
| Full suite command | `cmake --build build/OSX/Release && ctest --test-dir build/OSX/Release --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PRUNE-01 (SC-1 exact set) | Evaluator on a live light-node fixture reports EXACTLY the eligible set: all-outputs-genuinely-consumed ∧ spend-recency ≥ N; unspent/in-window/synthesized/negative cases excluded (incl. the owner's ancient-tx-recent-spend counterexample) | integration (CRDTFixture family; real puts on a real topic; window driven via `AddHead` priority) | `test_bin/prune_eligibility_test --gtest_filter='*ExactEligibleSet*'` | ❌ Wave 0 |
| PRUNE-01 (B unit) | `spent_by_txid` populated at ConsumeUTXOs; round-trips bucket-rewrite + restart-load; reset on RestoreConsumedUTXOs | unit (`utxo_manager_test`-style fresh temp DB) | `test_bin/utxo_manager_test --gtest_filter='*SpentByTxid*'` | ❌ Wave 0 |
| PRUNE-03 (SC-2 block gate) | Delta block erasable only when EVERY element AND tombstone key is eligible + allowlist-conformant; mixed block ⇒ not erasable; non-tx key in block ⇒ not erasable | integration (multi-element delta fixture — batched sends in one block) | `test_bin/prune_eligibility_test --gtest_filter='*BlockGate*'` | ❌ Wave 0 |
| GATE-01 (SC-4 fail-closed) | Evaluator refuses construction for Full/Archive; out-of-prefix candidates rejected with reasons (cert key, proof key, `reg/`, wrong-net, slashless sibling) | unit + integration | `test_bin/prune_eligibility_test --gtest_filter='*FailClosed*'` | ❌ Wave 0 |
| GATE-04 (SC-3 own frontier) | With frontier tx value ERASED (Phase-16 `EraseTransactionEntry`) deep in window + cert present: `GetOutgoingPreviousHash(nonce+1)` == frontier hash via cert rung; cold-start nonce chain correct; negative: no cert ⇒ "" (fail closed, never a wrong hash); equivalence: rung-3 == rung-4 result when both available | integration (CertificateFallback fixture + BuildSignedCertificate + PersistCertificateAtSlot) | `test_bin/transaction_manager_certificate_fallback_test --gtest_filter='*CertSlotPreviousHash*'` (or new file in same target) | ❌ Wave 0 |
| GATE-06 (SC-5 config) | Absent `pruning` keys ⇒ identical behavior (no construction, no log cycle); enabled-without-window ⇒ disabled + WARN; resolved values INFO-logged; valid values parsed | unit (config-file fixture + LoadSgnsConfig behavior) | `test_bin/prune_eligibility_test --gtest_filter='*PruningConfig*'` | ❌ Wave 0 |
| D-04 marker contract | `/prune/tx/<hash>` record round-trip via PutRaw/GetRaw; lookup distinction PRUNED vs NOT_FOUND; invariant-violation case reports | unit (raw surface) | `test_bin/prune_eligibility_test --gtest_filter='*PruneMarker*'` | ❌ Wave 0 |
| SC-5 no behavior change (default) | Full existing suite green with default config; evaluator/dry-run never constructed | full suite | `ctest --test-dir build/OSX/Release` | ✅ (existing suite) |

### Sampling Rate
- **Per task commit:** targeted `--gtest_filter` on the new suites + clang-format/clang-tidy on changed files
- **Per wave merge:** `ctest --test-dir build/OSX/Release --output-on-failure` (full suite — SC-5)
- **Phase gate:** full suite green + all new suites green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `src/crdt/crdt_datastore.hpp` (+ `globaldb.hpp`) — public per-key priority read passthrough to `CrdtSet::GetPriority` (no public path exists today)
- [ ] `test/src/account/prune_eligibility_test.cpp` (name TBD) — CRDTFixture-based evaluator suite: SC-1 exact set, SC-2 block gate, SC-4 fail-closed, config, marker contract
- [ ] SC-1 fixture discipline: fresh temp DB (D-C6-from-16), real-topic seeds so blocks/CIDs/seals exist (Phase-16 lesson), three-way window discriminator (ancient-ancient / ancient-recent / recent-recent), negative control proving the exact-set assertion CAN fail
- [ ] B unit tests (extend `utxo_manager_test` or the new file): populate, round-trip, revert-reset
- [ ] GATE-04 tests: extend `transaction_manager_certificate_fallback_test.cpp` (fixture machinery reused) or new file in that target; uses `EraseTransactionEntry` to simulate pruning
- [ ] Framework install: none needed (gtest integrated)

## Security Domain

ASVS level 1 (config: `security_enforcement: true`, `security_asvs_level: 1`, `security_block_on: high`).

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|------------------|
| V2 Authentication | no | no new network/auth surface (local evaluation only) |
| V3 Session Management | no | — |
| V4 Access Control | yes | fail-closed gate ensemble: NodeType==Light at construction (immutable enum, read post-config); allowlist = `TRANSACTION_BASE_FORMAT`-built `/bc-<net>/tx/` prefix over monitored networks only; out-of-prefix rejected with reasons + ctest |
| V5 Input Validation | yes | config: rapidjson type-checks per key, enabled-without-window fails to disabled; DB inputs: malformed tx values → deserialize failure → ineligible-with-reason (never crash/never eligible); block parse failure → gate fails closed (not erasable); marker records validated on read |
| V6 Cryptography | no | cert reads use existing `GetCertificateBySlot` validation; no new crypto |
| V7 Error handling | yes | `outcome::result`/reason enums throughout; no swallowed failures |
| V8 Data protection | yes (invariant) | dry-run erases NOTHING in production; the only erase-path use is test-side via the guarded Phase-16 API; `/utxo/`, `/cert/`, heads, seals are outside the candidate set by construction |

### Known Threat Patterns for this stack
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Allowlist bypass via prefix confusion (`…/txfoo`, wrong-net `/bc-999/`, case tricks) | Tampering | exact `base + "tx/"` startswith over `GetMonitoredNetworkIDs()` only; ctest with crafted keys (Pitfall 5) |
| Config injection enabling pruning silently (default-on foot-gun; Bitcoin GUI precedent) | Tampering/Elevation | default off; enabled requires explicit `finality_window`; absent keys = today's behavior byte-identical (SC-5); resolved values INFO-logged |
| Elevation via NodeType default (missing node_type defaults Light) | Elevation | NodeType is a role gate, never the consent gate — pruning knob is (Pitfall 8) |
| Repudiable pruning decisions (why was X eligible?) | Repudiation | per-item reason codes in the structured result + per-cycle INFO summary (D-03); Phase 18 adds the durable marker |
| Wrong-hash nonce-chain resolution after pruning (integrity of previous_hash) | Tampering | cert is the single authority; binding checks kept identical to `CertificateMatchesTransaction`; fail closed to "" (never a guessed hash); negative test |

## Sources

### Primary (HIGH confidence — verified in this repo this session)
- `src/account/UTXOManager.{hpp,cpp}` — `BuildUTXORecordKey` :18-24; `PutUTXO` :156-226 (nullopt fields :178-179); `ConsumeUTXOs` :265-365 (synth tombstone :305-310, owners-to-store :316-362); `RestoreConsumedUTXOs` :367-407 (resets :389-390); `AcquireStorage`/`db_` :940-950, hpp:532; `StoreUTXOSnapshot` :985-1071 (raw batch, bucket rewrite, field serialize :1033-1043); `LoadUTXOs` field restore :925-937; `IsOutPointGenuinelyConsumed` :777-783; `utxos_mutex_` hpp:534
- `src/transaction/TransactionManager.{hpp,cpp}` — ladder `GetOutgoingPreviousHash` :1660-1686; `GetRegisteredOutgoingPreviousHash` :1688-1726 (type restriction :1720); `GetTrackedOutgoingPreviousHash` :1728-1788; `CertificateMatchesTransaction` :1790-1816; `GetTransactionCertificate` :1818-1826; `GetPersistedOutgoingPreviousHash` :1828-1868 (value fetch :1847); `QueryOutgoingPreviousHashFromCRDT` :1870+; `FillDAGStruct` :1550-1571 (consumer :1556); `FillDAGStructForAddress` :1573+; `QueryTransactions` :2473-2503; `GetBlockChainBase` :2235-2242; `GetMonitoredNetworkIDs` :2224; `ConsumeUTXOs` call sites :2637, :2657, :2702, :2871; `LoadUTXOs` wiring :3156; `StoreTransactionCID` :4373 (drift note A1); `NewElementCallback` :4421; `TRANSACTION_BASE_FORMAT` hpp:433; `GetTransactionPath` hpp:317-319; `node_type_m` hpp:669; Phase-16 contracts hpp:849, :864, :912, :938
- `src/transaction/TransactionConsensusHandler.cpp` — `EvaluateTransactionReplayProtection` :618-741 (own-address ladder use :631; reg fallback :653-665; **cert-slot fallback :666-688**; pending/reject :690-715; prev-cert requirement :717-726)
- `src/account/GeniusTransaction.{hpp,cpp}` — `GetSlotID` hpp:290-293 (`src:nonce`); `GetProducedUTXOs` cpp:118-141 (nullopt semantics)
- `src/account/GeniusNode.{hpp,cpp}` — `LoadSgnsConfig` cpp:389-518 (rapidjson pattern, node_type :438-456, defaults); `GetNodeType` hpp:303; `TransactionManager::New` wiring cpp:1121
- `src/account/NodeType.hpp` — enum :34-40, header rationale :1-8
- `src/account/GeniusAccount.cpp` — `GetLocalConfirmedTxHash` :963-975; local-history load/persist :1290-1322, :1423-1440 (node-local keys, survive pruning)
- `src/blockchain/Consensus.{hpp,cpp}` — `CERTIFICATE_BASE_PATH_KEY = "/cert/"` hpp:726-727; `GetCertificateBySlot` cpp:4730-4758 (slot recheck + ValidateCertificate); `Blockchain.hpp:322`
- `src/crdt/globaldb/globaldb.{hpp,cpp}` — `QueryKeyValues` hpp:148-157; `GetCRDTHeadList` hpp:327; **`GetLocalDeltaKeyValues` public hpp:349**; `GetRaw/PutRaw/QueryRaw` hpp:174-196; `GetCRDTDataStore` hpp:347; `EraseEntry` hpp:248, impl cpp:797
- `src/crdt/crdt_datastore.{hpp,cpp}` — `GetDeltaFromNode` hpp:368, impl cpp:961-983 (created_by_self filter); `GetLocalDeltaKeyValues` impl :2280-2302 (elements-only + TODOs); `AddHead` :2270
- `src/crdt/crdt_set.{hpp,cpp}` — `GetPriority` hpp:208, impl :498-519; `StageEraseElement` hpp:294
- `src/crdt/impl/crdt_heads.cpp` — `GetList` :259-284 (maxHeight)
- `src/crdt/graphsync_dagsyncer.hpp` — `StageBlockRemoval` :125
- `src/crdt/proto/delta.proto` — `Delta {elements, tombstones, priority}`
- `src/crdt/globaldb/crdt_work_journal.hpp` — `NAMESPACE_PREFIX = "/crdt/work/"` :117
- Tests: `test/src/account/transaction_manager_certificate_fallback_test.cpp` (fixture :348-415, helpers :421-533: PersistTransaction / BuildSignedCertificate :437-514 / PersistCertificateAtSlot :516-523 / FetchAndProcess; teardown discipline :387-415); `test/testutil/storage/base_crdt_test.hpp` (CRDTFixture :27-44); `test/src/account/CMakeLists.txt` (addtest targets); binaries verified in `build/OSX/Release/test_bin/`
- Grep verifications: no `/utxo` writer outside UTXOManager; `spent_by_txid` writers = resets only; Phase-16 substrate present with zero production callers

### Secondary (MEDIUM-HIGH confidence)
- `.planning/research/{SUMMARY,ARCHITECTURE,PITFALLS}.md` — milestone research (file:line-verified 2026-10-15); re-cross-checked this session where load-bearing (window rationale, cert-slot precedent, callback invariant, block-parse pattern) — all consistent with direct source reads
- `.planning/phases/16-*/16-RESEARCH.md` + 16-CONTEXT — substrate contracts and test discipline (spot-verified against current headers)

### Tertiary (LOW confidence)
- none used

## Metadata

**Confidence breakdown:**
- Standard stack (internal APIs): HIGH — every cited symbol read in source this session
- D-02 verdict (Candidate A failed → B): HIGH — full write-path trace from ConsumeUTXOs to raw batch put; no CRDT priority exists for `/utxo/` keys
- D-01 scope (minimal rung-3 relaxation): HIGH — ladder + both consumers + cert store read end-to-end; the replay consumer already cert-resolves
- Architecture (evaluator/gates/config/marker shapes): HIGH — built entirely on verified surfaces; discretion items flagged as Open Questions
- Pitfalls: HIGH — 12 hazards; the top ones (phantom priority, creation-age regression, tombstone gap, prefix discipline) verified line-level

**Research date:** 2026-10-16
**Valid until:** 2026-11-15 (stable internal codebase; re-verify if `src/account/UTXOManager.cpp`, `src/transaction/TransactionManager.cpp`, or `src/crdt/globaldb/` changes land first)
