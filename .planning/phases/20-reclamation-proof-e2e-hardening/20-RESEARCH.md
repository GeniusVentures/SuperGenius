# Phase 20: Reclamation Proof & E2E Hardening - Research

**Researched:** 2026-10-24
**Domain:** RocksDB 9.4 manual-compaction reclamation through the SuperGNUS storage wrappers, measured-bytes CI gating, post-compaction byte-identity proofs, mesh retention, and the window-edge race fault test — v1.2's closing phase.
**Confidence:** HIGH (every load-bearing claim re-verified against vendored RocksDB 9.4 headers/implementation and this repo's source this session, file:line cited; the two order-of-magnitude items — compaction wall-clock and compression ratio — are flagged `[ASSUMED]`/MEDIUM in the Assumptions Log)

## Summary

**The compaction recipe is implementable exactly as D-C1/D-01 locked, and RocksDB 9.4 gives it better cancellation semantics than the milestone research knew about.** The vendored 9.4 `CompactRangeOptions` carries a `std::atomic<bool>* canceled` member (options.h, CompactRangeOptions struct) — a first-class in-progress cancellation channel: pre-set, `CompactRange` returns `Status::Incomplete(kManualCompactionPaused)` before starting (`db_impl_compaction_flush.cc:972-974`); set mid-flight, the compaction iterator aborts promptly. This maps one-to-one onto D-01's "cancellable at shutdown (checked before starting; aborted cleanly if already running)" without any polling or thread gymnastics. Critically verified: whole-DB `CompactRange` (`begin = end = nullptr`) **unconditionally flushes the memtable first** (`CompactRangeInternal`, `bool flush_needed = true;` — the skip check only runs when both bounds are non-null) — which makes the before/after measurement deterministic and rescues the CI fixture from the 32MB-membuffer trap below.

**The measurement has two traps the plan must bake in.** First: `write_buffer_size = 32MB` (`globaldb.cpp:199-202`) means a 200×10KB CI fixture lives *entirely in the memtable* — a `GetApproximateSizes` snapshot with the default `INCLUDE_FILES` flag reads ~0 before compaction (RocksDB's own `db_test.cc:1617-1619` asserts exactly this "Zero if not including mem table"). Both snapshots must use `INCLUDE_FILES | INCLUDE_MEMTABLES`. Second: unlike `CompactRange`, `GetApproximateSizes` has **no nullptr/unbounded-range semantics** — `Range(Slice(), Slice())` is the EMPTY range `["", "")` (implementation converts both slices to internal keys unconditionally, `db_impl.cc:4690+`). The whole-DB measurement needs an explicit sentinel high bound (all real key classes here — binary CIDv1 keys starting `0x01`, ASCII `/…`, `:sgns:…`, base58 seal keys — sort below a `0xFF…` sentinel).

**Everything above the raw `std::shared_ptr<DB>` is missing and must be owner-vended.** Grep confirms **zero** `CompactRange`/`GetApproximateSizes` callers in `src/` or `test/` (the only hits are Phase 20's own Doxygen hooks, `TxPruner.hpp:65,117` / `TxPruner.cpp:284`), and neither `GlobalDB`, `CrdtDatastore`, nor `DAGSyncer` exposes `getDB()` — only the base wrapper does (`src/storage/rocksdb/rocksdb.hpp:100-103`; vendored twin `ipfs_lite/rocksdb/rocksdb.hpp:74`), and `GlobalDB` holds that wrapper privately (`m_datastore`). Per the one-owner rule and D-01's "no domain logic in callers" discipline, GlobalDB is the right vendor: a small `CompactStorage(…)` + `GetApproximateDiskUsageBytes()` pair, called from TxPruner's post-cycle tail (which already holds `crdt::GlobalDB&`). The shutdown story is already structurally safe: the compaction runs synchronously on the prune-timer callback (io thread), and GeniusNode drains the io pool **before** `ReleaseRuntimeMembersAfterIoStopped` resets `tx_pruner_` (`GeniusNode.cpp:2449-2467`, and the wiring's own comment "reset only after the io pool has been drained", `:5108`) — an in-flight compaction can never outlive its service or the DB handle; the `canceled` atomic adds promptness on top.

**Primary recommendation:** Add `GlobalDB::CompactStorage(const std::atomic<bool> *canceled)` + `GlobalDB::GetApproximateDiskUsageBytes()` (whole-DB sentinel range, files+memtables flags) as the one compaction surface; drive it from the tail of `TxPruner::RunCycle` (after `RecordCompletedCycle`) gated on erased≥1 ∧ cumulative-erased-bytes watermark ≥ `pruning.compact_threshold_mb` (default 16, degenerate-0 rejected per the WR-03 convention); stamp `PruneStatus.bytes_reclaimed` at compaction from the stored cycle-start size snapshot vs post-compaction re-read; keep the erase-time INFO line counts-only (drop the "not measured until Phase 20" clause) and emit a second INFO at compaction completion. Tests: the dedicated `addtest` gate target (200×~10KB, prune, ONE compaction, ≥60% bounded assertion), the post-compaction extension of 18's snapshot helpers (GATE-02), the mesh retention ctest on the 19-07 EraseNodeCollection recipe (SC-4), and the seam-held race with a blocking hook at `kInterruptAfterEvaluation`/`kInterruptBeforeErase` (SC-5).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked carry-forwards (do not re-litigate)
- **D-C1:** Compaction recipe fully specified (18-STACK research): whole-DB `CompactRange` with `bottommost_level_compaction = kForce`, `allow_write_stall = false`, scheduled off the DAG workers/hot path; ONE shared DB (the two facades are the same handle)
- **D-C2:** `bytes_reclaimed` absent from PruneCycleStats by design (Doxygen points here); RECL-03's log currently states not-measured; RECL-04's status query exists — this phase completes both with real numbers
- **D-C3:** Substrate + survival behavior (16-19) complete: erase flow, compose lock, markers, restart matrix, tri-state, mesh E2E — this phase proves the DISK claim, not new node behavior
- **D-C4:** The full/archive retention and non-tx immutability tests extend 18's byte-identity snapshot patterns (tx_pruner_test / globaldb_erase_test fixtures)
- **D-C5:** Anti-vacuous rule: compaction rewrites files — identity assertions must be taken AFTER compaction, not just after erase

### Compaction scheduling policy
- **D-01:** Post-cycle + byte threshold. Compact after every real prune cycle that erased ≥1 tx AND whose cumulative erased-bytes watermark crosses a threshold (default ~16MB — no compacting after tiny cycles); cancellable at shutdown (checked before starting; aborted cleanly if already running); never during the settle window. No new timer thread — the compaction runs on the pruner's own post-cycle path (off the DAG workers per D-C1). The cumulative watermark is in-memory only (rebuilds from zero at restart — worst case one extra early compaction; NOT persisted; no /prune/ cursor).

### Measurement & reporting shape
- **D-02:** `GetApproximateSizes` over the whole DB, taken at cycle start vs after compaction; the delta stamps `PruneStatus.bytes_reclaimed` AT COMPACTION TIME (erase-time stamping would be a lie — deletes are tombstones until compaction). The per-cycle INFO keeps its honest split: at erase it reports the erase counts; the measured reclaimed number is reported (and stored) when compaction completes. The RECL-03 log wording updates accordingly (drop the "not measured until Phase 20" note).

### Measured-bytes CI gate design
- **D-03:** Dedicated ctest target (NOT in the quick matrix): seed a fixture with many known-size transactions (e.g. 200 × ~10KB), prune them, run ONE compaction, assert the byte drop ≥ a conservative fraction of the erased bytes (≥60%) — a bounded-range assertion immune to RocksDB writer/versioning variance. Generous timeout; runs in the standard full suite; skipped on very slow runners via the existing timeout mechanism. Exact-delta rejected (brittle against metadata overhead).

### Race + immutability harness
- **D-04:** Seam-held race + post-compaction identity. SC-5 uses the 18-landed cycle interrupt seam to hold the cycle between evaluation and the per-tx erase; the spend (a genuine consumed record) lands on the held tx's outpoint; release; assert (a) the under-lock spent-ness re-verify skips the erase THIS cycle, (b) the NEXT cycle picks it up — deterministic interleaving, debug+release timing legs per SC-5. The immutability ctest (GATE-02) extends 18's byte-identity snapshots to AFTER compaction (certificates, SecureCRDT policy, processing results, heads, seal markers) and the retention ctest (SC-4) runs full/archive nodes in the same mesh across the light's prune+compact cycles.

### Claude's Discretion
- The threshold's exact knob name (`pruning.compact_threshold_mb`?) and default value (~16MB) — config surface per the PruningConfig convention
- Where the compaction call lives (TxPruner post-cycle vs a small GlobalDB surface — the recipe wraps `getDB()`; owner-vended per the one-owner rule)
- The gate target's name and the fixture's exact seeding sizes

### Deferred Ideas (OUT OF SCOPE)
- Persistent compaction watermark (in-memory only per D-01 — worst case one early compaction per restart)
- Exact-delta byte assertions (rejected per D-03)
- True concurrent-writer racing (rejected per D-04 — the seam-held interleaving is deterministic)
- Disk-budget auto-GC / watermark-driven GC (v2 HIST-03 territory)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| RECL-01 | Deleted bytes actually leave the disk — periodic light-node-only `CompactRange` (`bottommost_level_compaction = kForce`) scheduled off the hot path | Vendored-9.4 API verified: overload shape, `kForce` requirement (default `kIfHaveCompactionFilter` compacts NOTHING without a filter), `allow_write_stall=false`, `canceled` atomic; TxPruner post-cycle tail is the locked hook point (off-DAG by construction — the prune timer rides the node io_context, never the DAG worker); Light-only inherited from the `TxPruner::New` factory gate |
| RECL-02 | CI gate proves real reclamation — measured SST/`du` bytes before-vs-after a prune+compaction cycle (not key counts) | `GetApproximateSizes` mechanics verified (files+memtables flags mandatory on the 32MB-buffer fixture; sentinel range; compression-aware); `du` via `boost::filesystem` over the fixture's `db_path_` as belt; dedicated `addtest` target shape (default TIMEOUT 600 already generous); ≥60% bounded assertion rationale |
| GATE-02 | Non-transaction data is provably untouched — certificates, SecureCRDT policy values, processing results, DAG heads, and seal markers are byte-identical across a prune run (ctest) | 18's snapshot helpers verified in `tx_pruner_test.cpp:1204-1318` (SnapshotPrefix / SnapshotCrdtEntriesContaining / SnapshotHeads / SnapshotSeals / SnapshotBlocks / SnapshotMapEntry); compaction rewrites files but never value bytes — assertions re-taken AFTER compaction per D-C5; seals/heads are ordinary keys, no compaction filter exists (v1.2-STACK rejected it) |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **C++17 only** — no C++20; Allman braces; brace all control statements; `enum class`; explicit casts; `constexpr` over macros; `.hpp/.cpp`; GNUS naming; Doxygen file headers + documented public functions.
- **`outcome::result<T>`** for fallible operations — the compaction surface, the size query, and the threshold-config parse return results; never swallow failures (a failed compaction is WARN + retry-by-watermark-still-high, never silent).
- **One owning module per domain rule** — the compaction *mechanism* (recipe, options, sentinel range, flags) has ONE home (GlobalDB surface); the *scheduling policy* (threshold, watermark, post-cycle trigger) has ONE home (TxPruner); GeniusNode wires nothing new (timer already exists).
- **Never duplicate logic** — the recipe is not re-stated at call sites; the erased-bytes estimate assembles evaluator-carried sizes, not a second fetch path; the snapshot helpers are reused, not forked.
- **No domain logic in the transport layer** — nothing here touches gRPC/relay.
- **Refactor first, then change** — the `PruneStatus`/INFO-line changes are behavior-additive (new field, reworded line) and separable from the compaction trigger; keep the GlobalDB surface landing separate from the TxPruner trigger if diffs mix concerns.
- **Verification is a gate** — clang-format/clang-tidy on changed files, build affected targets, run relevant tests; never claim a check passed that was not run.
- **Build-system pre-PR checklist** applies (new test file in `test/src/account/CMakeLists.txt` via `addtest` — the established copy-block pattern; no artifact routing concerns for a test exe).
- KISS/YAGNI: no persistent watermark, no compaction queue, no auto-GC (all explicitly deferred).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Compaction mechanism (CompactRange + options + cancellation plumbing) | `GlobalDB` (`src/crdt/globaldb/`) — NEW small surface wrapping `m_datastore->getDB()` | `sgns::storage::rocksdb::getDB()` (raw handle, already public at wrapper level) | The DB owner vendors the recipe (one-owner rule, D-01 discretion note "owner-vended"); callers never touch `getDB()`; `ActiveDataStore()` null-after-shutdown (`globaldb.cpp:151-159`) gives fail-closed for free |
| Whole-DB approximate size (telemetry input) | `GlobalDB` — NEW `GetApproximateDiskUsageBytes()` | — | Same handle ownership; the sentinel-range + flags knowledge must have exactly one home (never re-derived at call sites) |
| Compaction scheduling policy (threshold, cumulative-erased-bytes watermark, ≥1-erased conjunct, settle exclusion) | `TxPruner` (`src/transaction/`) — post-cycle tail of `RunCycle` | `PruningConfig` (the knob's parse/validate home) | D-01 locks "the pruner's own post-cycle path"; RunCycle already receives `crdt::GlobalDB&`; Light-only inherited from the factory gate; in-memory watermark = instance member |
| Config surface (`pruning.compact_threshold_mb`) | `PruningConfig::FromJSON` (`PruneEligibility.hpp:210+`) | `GeniusNode` resolved-values INFO | The established one-parse-home pattern (GATE-06); degenerate-0 rejection per the WR-03 convention (`max_erases_per_cycle`/`settle_seconds` precedent) |
| Shutdown cancellation | `TxPruner` member `std::atomic<bool>` + GeniusNode stop path sets it | io-pool drain ordering (already correct: drained BEFORE `tx_pruner_.reset()`) | The atomic must outlive the in-flight `CompactRange` call (it is read by the compaction iterator) — instance member, not a stack local; verified teardown order `GeniusNode.cpp:2449-2467` |
| bytes_reclaimed reporting | `TxPruner` (`PruneStatus` + compaction INFO) | RECL-03 erase-time INFO (counts-only, reworded) | D-02 locks stamp-at-compaction; status mutex already guards `PruneStatus` |
| Measured-bytes CI gate (RECL-02) | NEW dedicated `addtest` target in `test/src/account/` | — | D-03: dedicated, not in the quick matrix, rides the standard full suite (CI runs `ctest -LE requires_foundry` with no include list — registration is inclusion) |
| Post-compaction immutability proof (GATE-02) | `tx_pruner_test.cpp` fixture (extend existing snapshot suites) | — | D-C4: extend 18's helpers, take snapshots AFTER compaction (D-C5) |
| Full/archive retention proof (SC-4) | Mesh fixture — the 19-07 `EraseNodeCollection` recipe (`globaldb_erase_test.cpp:83+`) or a mesh leg on the tx_pruner fixture (19-06 responder pattern) | — | Real pubsub + real GlobalDB nodes; "full/archive" = never constructs a pruner (factory refuses non-Light — already ctest-asserted at `tx_pruner_test.cpp:1787`) |
| Window-edge race proof (SC-5) | `tx_pruner_test.cpp` — seam-held interleaving | `UTXOManager` consumed-record mutation helpers (fixture-side) | D-04: deterministic hold via blocking `SetCycleInterruptHookForTest` lambda; debug+release legs |

## Standard Stack

**No new external dependencies, no vendored changes, no new libraries.** Everything rides landed internal APIs plus vendored RocksDB 9.4 (all verified this session):

### Core
| API | Location | Purpose | Why Standard |
|-----|----------|---------|--------------|
| `DB::CompactRange(const CompactRangeOptions&, ColumnFamilyHandle*, const Slice* begin, const Slice* end)` | vendored `thirdparty/rocksdb/include/rocksdb/db.h:1495`; convenience default-CF overload `:1498-1500` (whole-DB = `nullptr, nullptr`, per the `:1482` doc comment) | The reclamation call | D-C1's verified recipe; implementation `db_impl_compaction_flush.cc:965+` |
| `CompactRangeOptions` | vendored `options.h` (struct): `bottommost_level_compaction` (default `kIfHaveCompactionFilter` — **must set `kForce`**), `allow_write_stall = false` default (sleeps until load low), `canceled` (`std::atomic<bool>*`), `exclusive_manual_compaction` (leave false) | The options shape incl. first-class cancellation | `canceled` is 9.4-native in-progress cancellation (struct comment: "Allows cancellation of an in-progress manual compaction"); entry-check returns `Status::Incomplete(kManualCompactionPaused)` if pre-set (`db_impl_compaction_flush.cc:972-974`) |
| `DB::DisableManualCompaction()` / `EnableManualCompaction()` | vendored `db.h:1589-1600` | Alternative shutdown abort: aborts outstanding manual compactions AND waits | NOT recommended as primary (it overwrites the user `canceled` var per the options.h note; the TxPruner-owned atomic + io-drain ordering is simpler) — documented as the belt |
| `DB::GetApproximateSizes(ColumnFamilyHandle*, const Range*, int n, uint64_t* sizes, SizeApproximationFlags)` | vendored `db.h:1444-1448`; flags enum `db.h:1423-1427` (`NONE/INCLUDE_MEMTABLES/INCLUDE_FILES`), `\|` operator `db.h:2127+` | Measured bytes (D-02 telemetry + RECL-02 assertions) | Measures **file-system space** (compression-aware — sizes shrink with compression, `db.h:1433-1435`); impl `db_impl.cc:4690+` |
| `sgns::storage::rocksdb::getDB()` | `src/storage/rocksdb/rocksdb.hpp:100-103` | The raw `std::shared_ptr<DB>` the new GlobalDB surface wraps | The ONLY public raw-DB accessor above the wrappers; GlobalDB already owns this object (`m_datastore`, `globaldb.cpp:324` passes `getDB()` into `IpfsRocksDb::create` — the same handle twice, D-C1) |
| `GlobalDB::ActiveDataStore()` | `globaldb.cpp:151-159` | Fail-closed null-check inside the new surface | Returns nullptr once `ShutdownNow` moved the datastore — the surface inherits shutdown safety |
| `TxPruner::RunCycle` tail | `src/transaction/TxPruner.cpp:82-241` (`RecordCompletedCycle` at `:239`, `:243-297`) | The D-01 hook point (post-status-update, pre-reschedule) | Runs on the prune-timer callback (node io_context) — off the DAG workers by construction (D-C1); wiring reschedules at `GeniusNode.cpp:5151-5168` AFTER the service returns, so the trigger living in the service tail is still pre-reschedule |
| `PruningConfig::FromJSON` | `PruneEligibility.hpp:210+` (impl `:84-97`) | The threshold knob's parse home | WR-03 degenerate-zero convention precedent (`max_erases_per_cycle`, `settle_seconds` both reject 0 at read time, member keeps safe default) |
| `PruneStatus` / `PruneCycleStats` | `TxPruner.hpp:68-133` | Gains `bytes_reclaimed` (D-02) — the Doxygen hook at `:113-119` retires | Mutex-guarded copy (`GetPruneStatus`, `const std::scoped_lock`) — concurrent-read-safe extension |
| 18's snapshot helpers | `test/src/account/tx_pruner_test.cpp:1204-1318` | GATE-02 post-compaction identity assertions | D-C4: extend, don't fork; `SnapshotPrefix` / `SnapshotCrdtEntriesContaining` / `SnapshotHeads` / `SnapshotSeals` / `SnapshotBlocks` / `SnapshotMapEntry` |
| `EraseNodeCollection` mesh fixture | `test/src/crdt/globaldb_erase_test.cpp:83-161` (`addNode`/`restartNodePendingStart`/`connectNodes` — one-direction dial) | SC-4's full-node baseline holder | 19-07's proven real-pubsub N-node topology; 19-06 also proved a second pubsub host works on the tx_pruner fixture itself (responder leg) — planner picks the host file |
| `SetCycleInterruptHookForTest` + `kInterruptAfterEvaluation`/`kInterruptBeforeErase` | `TxPruner.hpp:239-264`, call sites `TxPruner.cpp:135, :198` | SC-5's deterministic hold | The hook is `bool(int)` invoked INLINE on the cycle thread — a lambda that blocks, mutates, then returns **false** = hold-and-continue (true = abort, the crash-matrix use) |

### Supporting
| API | Location | Purpose | When to Use |
|-----|----------|---------|-------------|
| `Range(Slice, Slice)` sentinel idiom | vendored `db.h` (`struct Range` in types.h lineage) + impl `db_impl.cc:4690+` | Whole-DB size measurement | `Range(Slice(), Slice(std::string(N, '\xFF')))` — NO nullptr semantics in GetApproximateSizes; `["","")` is EMPTY (verified: no unbounded branch in the impl) |
| `SizeApproximationFlags::INCLUDE_FILES \| INCLUDE_MEMTABLES` | `db.h:1423-1427` | Both snapshots | MANDATORY on small fixtures — 32MB write buffer means everything sits in the memtable pre-flush; RocksDB's own test pins files-only ≈ 0 there (`db_test.cc:1617-1619`) |
| `boost::filesystem` directory walk over `db_path_` | fixture member (`base_crdt_test.hpp:42`) | `du` belt assertion (SC-1's "SST/du" wording) | Secondary to GetApproximateSizes; includes WAL/OPTIONS noise — bounded/sanity use only |
| `UTXOManager::IsOutPointGenuinelyConsumed` | `UTXOManager.cpp:909-915` (`UTXO_CONSUMED && amount != 0`, `shared_lock(utxos_mutex_)`) | SC-5's refuse-signal + the re-verify's exact semantics | The re-verify refuses on: unreadable value, no outputs, any output not genuinely consumed (`TxPruner.cpp:316-360`) |
| Fixture helpers `MakeTransfer`/`SeedTransaction`/`TrackOutputs`/`ConsumeOutput`/`MakePruner`/`RunSettledCycle` | `tx_pruner_test.cpp` (e.g. `:167, :684, :712`; `ConsumeOutput` used at `:2274`) | The gate/race/identity tests' seeding | `RunSettledCycle` primes the settle clock deterministically (kSmallSettleSeconds=1) |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| TxPruner-owned trigger (post-cycle tail) | GeniusNode wiring-side trigger (between RunCycle and reschedule) | **Use TxPruner** — D-01's wording ("the pruner's own post-cycle path"); the service already holds `GlobalDB&`; Light-only enforcement comes free from the factory gate (a wiring-side trigger would need its own role gate — duplication); GeniusNode stays wire-only (Pitfall 9 of 18-RESEARCH) |
| `GlobalDB::CompactStorage()` owner-vended surface | TxPruner calling `getDB()` through a new GlobalDB raw-handle accessor | **Use the owner-vended surface** — the recipe (options, kForce, sentinel range, flags) must have ONE home; a raw-handle accessor invites N divergent call sites; matches D-01's discretion note ("owner-vended per the one-owner rule") |
| `canceled` atomic (TxPruner member) | `DB::DisableManualCompaction()` at shutdown | **Use the atomic** — DisableManualCompaction also aborts+waits but "overwrites the user-provided canceled variable" (options.h note) and adds a paired-enable lifecycle; the atomic + verified io-drain ordering is simpler and testable |
| Evaluation-carried sizes for the erased-bytes estimate (value/block bytes already in hand) | EraseEntry out-param accumulating staged bytes | **Use evaluation-carried** — `EvaluateCandidate` already receives the value Buffer (`PruneEligibility.cpp:443`) and the block gate already fetches/parses the block (`GetLocalDeltaKeys`, `:379`); an out-param would need extra Gets (deletes don't read values) or undercount payloads (key sizes only). It is an estimate for a threshold trigger — ±few-% error is immaterial against ~16MB |
| GetApproximateSizes as primary gate assertion | `du` (directory bytes) as primary | **Use GetApproximateSizes primary, du belt** — the API is the production telemetry (D-02) so the gate proves the SAME number operators see; du adds WAL/OPTIONS/file-granularity noise (fine as a sanity bound) |
| Sentinel-high-bound Range | Compute min/max keys via iterator | **Use sentinel** — every real key class here sorts below `0xFF…` (binary CIDv1 keys start `0x01`; `/`, `:`, base58 are ASCII); an iterator walk adds complexity for zero accuracy gain on an approximate API |

**Installation:** none — zero registry installs. (Package audit: not applicable; see Package Legitimacy Audit.)

## Package Legitimacy Audit

No external packages are installed, recommended, or required. All work lands in existing internal libraries (`transaction`, `crdt/globaldb`) and existing/new test targets against vendored RocksDB 9.4 (`thirdparty/rocksdb`, workspace checkout — headers and implementation read directly this session). Vendored dependencies untouched. Slopcheck gate: not applicable (zero registry installs) — carried convention from Phases 16-19 (all audited clean).

## Architecture Patterns

### System Architecture Diagram

```
 sgns_config.json
 pruning.{enabled, finality_window, dry_run,
          max_erases_per_cycle, settle_seconds,
          compact_threshold_mb=16}          ← NEW knob (discretion)
        │
        ▼
 GeniusNode::LoadSgnsConfig → PruningConfig::FromJSON (one parse home;
        │                     degenerate-0 rejected, member keeps default)
        ▼
 PRUNE TIMER TICK (io_context, weak-self; NOT the DAG worker — D-C1)
   GATE: Light ∧ READY ∧ missing-set empty ∧ settle elapsed
        ▼
 TxPruner::RunCycle(globaldb, tm, utxo, generation)
   ├─ SIZE SNAPSHOT: globaldb.GetApproximateDiskUsageBytes()   ← D-02 cycle-start
   │    (files+memtables, sentinel range — ONE home in GlobalDB)
   ├─ Evaluate (verbatim) → block-grouped K budget → per-tx:
   │     re-verify (UTXO lock) → atomic erase (value XOR marker)
   │     erased-bytes ESTIMATE += value_bytes + share of block_bytes
   │                                       (evaluation-carried sizes)
   ├─ RecordCompletedCycle: watermark + status + counts-only INFO
   │     (RECL-03 wording: drop "not measured until Phase 20")
   ▼
 COMPACTION GATE (post-cycle tail — the D-01 hook point)
   erased ≥ 1  ∧  cumulative_erased_bytes_ ≥ threshold
   ∧ !shutdown_canceled_ (checked BEFORE starting)
        │ yes
        ▼
   GlobalDB::CompactStorage(&canceled)          ← owner-vended recipe
     db->CompactRange(CompactRangeOptions{
        bottommost_level_compaction = kForce,
        allow_write_stall          = false,
        canceled                   = &txpruner_member_atomic},
        /*cf*/ nullptr-handle, nullptr, nullptr)   ← whole DB
     └─ memtable FLUSHED first (unconditional for nullptr bounds — verified)
     └─ bottommost SSTs rewritten; deleted keys dropped; files unlinked
        ▼
   POST-COMPACTION re-read: GetApproximateDiskUsageBytes()
   bytes_reclaimed = cycle_start_snapshot − post_read
        ▼
   PruneStatus.bytes_reclaimed = delta (status mutex) + ONE compaction INFO
   cumulative_erased_bytes_ = 0 (watermark reset — in-memory only, D-01)

 ── SHUTDOWN (verified ordering) ─────────────────────────────────
 GeniusNode stop → set TxPruner canceled atomic → io pool DRAINED
   → ReleaseRuntimeMembersAfterIoStopped: tx_pruner_.reset()
   → tx_globaldb_ release → ~GlobalDB → ShutdownNow → DB Close
 (in-flight CompactRange aborts promptly on canceled=true; the drain
  guarantees it returned before the service/DB die — no join needed)

 ── TESTS ────────────────────────────────────────────────────────
 GATE TARGET (RECL-02): seed 200×~10KB → prune → ONE compaction
   → assert (before − after) ≥ 60% of erased logical bytes (bounded)
 GATE-02: snapshot certs/policy/heads/seals AFTER compaction (D-C5)
 SC-4: mesh — full/archive node byte-identical across light's cycles
 SC-5: seam-held race — hold at kInterruptBeforeErase → mutate spend
   state → release → re-verify SKIPS this cycle → genuine spend next
   cycle → erased (debug+release legs)
```

Entry points: config → tick → cycle → compaction gate → CompactStorage → status. A reader traces one reclamation: erase → watermark crosses 16MB → CompactRange(kForce) → flush+rewrite → measured delta → `PruneStatus.bytes_reclaimed`.

### Recommended Project Structure
```
src/crdt/globaldb/
├── globaldb.hpp / .cpp        # MODIFIED — NEW CompactStorage(canceled) +
│                              #   GetApproximateDiskUsageBytes() (the one
│                              #   recipe home; ActiveDataStore fail-close)
src/transaction/
├── TxPruner.hpp / .cpp        # MODIFIED — threshold/watermark members,
│                              #   canceled atomic, post-cycle compaction
│                              #   gate + call, bytes_reclaimed stamping,
│                              #   RECL-03 wording, erased-bytes estimate
├── PruneEligibility.hpp/.cpp  # MODIFIED — PruningConfig gains
│                              #   compact_threshold_mb (parse+validate)
src/account/
└── GeniusNode.hpp / .cpp      # MODIFIED (minimal) — resolved-values INFO
                               #   extension; stop path sets the cancel
                               #   atomic (if not owned inside service stop)
test/src/account/
├── tx_pruner_test.cpp         # EXTENDED — post-compaction identity suites
│                              #   (GATE-02), seam-held race (SC-5)
├── reclamation_gate_test.cpp  # NEW (name TBD — discretion) — the D-03
│                              #   dedicated measured-bytes target
└── CMakeLists.txt             # addtest for the new target (copy-block
                               #   pattern, tx_lookup_tri_state precedent)
test/src/crdt/
└── globaldb_erase_test.cpp    # EXTENDED (or account-side mesh leg) —
                               #   SC-4 full/archive retention across the
                               #   light's prune+compact cycles
```

### Pattern 1: Owner-vended compaction surface
**What:** GlobalDB wraps `m_datastore->getDB()` into two result-returning methods. `CompactStorage` builds `CompactRangeOptions` (kForce, allow_write_stall=false, canceled passthrough), null-checks `ActiveDataStore()` (GLOBALDB_NOT_STARTED after shutdown), calls the whole-DB overload, and returns `outcome::result<void>` (a RocksDB non-OK status maps to a failure code). `GetApproximateDiskUsageBytes()` builds the sentinel `Range`, uses `INCLUDE_FILES | INCLUDE_MEMTABLES`, returns `outcome::result<uint64_t>`.
**Why:** the options/range/flags knowledge is a wire-schema-class fact — one home per DRY invariant; `ShutdownNow`'s handle-move makes the surface fail-closed with zero extra checks; callers (TxPruner, tests) stay at the contract level.

### Pattern 2: Post-cycle trigger, service-owned (D-01)
**What:** the compaction gate runs at the end of `RunCycle` after `RecordCompletedCycle`, BEFORE the stats return (the wiring reschedules only after RunCycle returns — so this position is literally "post-status-update, pre-reschedule"). Members: `cumulative_erased_bytes_` (uint64_t, in-memory only), `compact_pending_size_snapshot_` (the cycle-start size), `compaction_canceled_` (`std::atomic<bool>`). Reset the cumulative watermark after a successful compaction; keep it (retry next cycle) after a failed/canceled one — the threshold still being crossed is the natural retry trigger.
**When to skip:** `erased == 0` (no new deletes — nothing to reclaim), dry-run mode never reaches this code (the wiring arm refuses; also assert at the gate), shutdown-canceled set.

### Pattern 3: Honest split reporting (D-02)
**What:** erase-time INFO stays counts-only (examined/eligible/erased/held/skipped/errors/duration/watermark — drop the trailing "bytes reclaimed: not measured until Phase 20" clause at `TxPruner.cpp:285-287`). Compaction completion emits its own INFO (before/after sizes, delta, duration). `PruneStatus` gains `std::optional<uint64_t> bytes_reclaimed` stamped ONLY at compaction success under the status mutex.
**Why:** deletes are tombstones until compaction — erase-time stamping is the documented lie (D-C2); nullopt = "compaction has not run since restart" is an honest state.

### Pattern 4: Seam-held deterministic interleaving (SC-5 / D-04)
**What:** `SetCycleInterruptHookForTest` with a lambda that, at `kInterruptBeforeErase` (or `kInterruptAfterEvaluation`), BLOCKS on a test-controlled latch, mutates the spend state on the target outpoint (fixture-side: the consumed record on `/utxo/` — `ConsumeOutput`'s inverse), releases, returns **false** (cycle continues into the re-verify with the mutation visible).
**Precedent:** the hook fires inline on the cycle thread (`InterruptRequested` call sites `TxPruner.cpp:135, :198`); the crash matrix already uses counting hooks (`tx_pruner_test.cpp:2351-2360`); `PruneStatus_ConcurrentReadDuringCycleIsCoherent` (`:2044`) already runs the cycle off-thread.

### Pattern 5: Anti-vacuous identity snapshots (D-C5)
**What:** the GATE-02 suite takes the SAME before-snapshots 18 took, runs the prune cycle, runs ONE compaction (through the same `GlobalDB::CompactStorage` production surface), THEN takes after-snapshots and asserts byte-identity. Erase-only identity (18's suites) remains as the pre-compaction baseline; the new proof is that the rewrite itself touches nothing.
**Why:** compaction rewrites every bottommost SST — an assertion suite that never compacts proves nothing about the rewrite (the CONTEXT's exact anti-vacuous concern).

### Anti-Patterns to Avoid
- **Calling `CompactRange` with default options** — default `bottommost_level_compaction = kIfHaveCompactionFilter` with NO filter installed compacts nothing away; `kForce` is load-bearing (D-C1).
- **Measuring with `INCLUDE_FILES` only** — reads ~0 on any fixture under the 32MB write buffer; both snapshots need files+memtables.
- **`Range(Slice(), Slice())` for whole-DB** — that is the EMPTY range; GetApproximateSizes has no nullptr semantics (only CompactRange does).
- **`exclusive_manual_compaction = true` combined with `canceled`** — vendored comment: "might not work well together" (`db_impl_compaction_flush.cc:2089-2090`); leave exclusive false.
- **Stumbling the DAG worker** — compaction must stay on the prune-timer thread (io_context), never posted onto CRDT/DAG executors (D-C1; the archive-replication-flake hazard memory).
- **Persisting the watermark "for safety"** — explicitly deferred (D-01); restart rebuilds from zero and the worst case is one early compaction.
- **Asserting exact byte deltas in CI** — rejected (D-03); compression, SST granularity, and version-set overhead make exact brittle; ≥60% + a sanity upper bound.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Cancellable manual compaction | Polling threads / timeout wrappers / kill-and-reopen | `CompactRangeOptions::canceled` atomic (9.4-native) + `Status::Incomplete(kManualCompactionPaused)` | First-class in the vendored version; the entry check and mid-flight abort are engine-side |
| Whole-DB size measurement | Summing SST file sizes manually / walking LSM metadata | `GetApproximateSizes` with flags + sentinel range | The API IS the approximation contract (compression-aware, range-aware); file-walking duplicates version-set knowledge |
| The compaction recipe at call sites | Inline `db->CompactRange(...)` in TxPruner/tests | The one `GlobalDB` surface | One home per recipe fact; tests exercise the production surface (the gate then proves the operator-visible number) |
| Byte-identity assertions | New snapshot code | 18's helpers (SnapshotPrefix et al.) | D-C4 locked; the helpers already know every non-tx key class |
| Mesh topology for SC-4 | A new network harness | EraseNodeCollection recipe / 19-06 responder leg | Real pubsub, proven teardown order, one-direction dial lesson baked in |
| Deterministic race interleaving | Sleeps/timing windows | The blocking interrupt hook | D-04 locked seam-held determinism; sleeps are the flake machine the 19-07 deviations document |
| Erased-bytes accounting | Exact byte ledgers / extra Gets on the erase path | Evaluation-carried sizes (value Buffer in hand, block fetched by the gate) | It is a threshold trigger estimate; exactness buys nothing and costs reads |

**Key insight:** Phase 20 is a thin scheduling + measurement layer over a proven engine primitive. The only genuinely NEW mechanism is the GlobalDB compaction surface (~40 lines); everything else is policy (threshold/watermark), reporting (status field + two INFO lines), and tests extending landed fixtures.

## Runtime State Inventory

Not a rename/refactor/migration phase — **SKIPPED** (no stored-data renames, no service config migrations, no OS-registered state, no secret/env-key changes, no build-artifact renames). The one runtime-state note that matters (already locked by D-01): the cumulative erased-bytes watermark is in-memory only and resets at restart — tests must not assert cross-restart watermark continuity.

## Common Pitfalls

### Pitfall 1: The memtable-blind snapshot (measurement reads zero)
**What goes wrong:** the gate fixture seeds 200×~10KB (2MB) — far under the 32MB `write_buffer_size` (`globaldb.cpp:199`); nothing is in SSTs until a flush. A files-only `GetApproximateSizes` "before" reads ≈0 and the delta assertion is vacuous or inverted.
**Why it happens:** RocksDB defaults the simple overload to `INCLUDE_FILES`; the intuition "data is on disk" is wrong for small DBs.
**How to avoid:** `INCLUDE_FILES | INCLUDE_MEMTABLES` at BOTH ends (the flags must match); note the whole-DB CompactRange flushes unconditionally first, so the "after" read is files-regardless — the symmetric flags keep the pair honest.
**Warning signs:** a gate test that "passes" with before≈after≈0; local runs green but assertions vacuous (assert `before ≥ erased_logical_bytes × ~0.8` as a sanity precondition so a blind snapshot fails loudly, not passes silently).

### Pitfall 2: Empty-range whole-DB measurement
**What goes wrong:** `Range(Slice(), Slice())` = `["", "")` = EMPTY; `GetApproximateSizes` returns 0 for every DB size.
**Why:** unlike `CompactRange` (nullptr = unbounded, `db.h:1482`), the sizes API converts both bounds to internal keys unconditionally (verified `db_impl.cc:4690+`) — no unbounded branch exists.
**How to avoid:** sentinel high bound (`Slice(std::string(N, '\xFF'))`, N≥8) — all real key classes (binary CIDv1 `0x01…`, `/…`, `:sgns:…`, base58 seals) sort strictly lower under the bytewise comparator.
**Warning signs:** telemetry reporting 0 on a node with known history.

### Pitfall 3: Forgetting kForce (the compaction that reclaims nothing)
**What goes wrong:** default `bottommost_level_compaction = kIfHaveCompactionFilter`; no filter is installed (v1.2-STACK explicitly rejected compaction filters) → the bottommost level is not rewritten → deleted keys survive in bottom SSTs → no reclamation, gate fails or (worse) telemetry quietly shows ~0 delta.
**How to avoid:** the recipe sets `kForce` in the ONE home (GlobalDB surface); a unit test asserting the surface's options (or the gate test itself) pins it.
**Warning signs:** bytes_reclaimed ≈ 0 despite erased>0; SST file count unchanged after "compaction".

### Pitfall 4: Shutdown racing an in-flight compaction
**What goes wrong:** a CompactRange running when the DB closes → undefined behavior; or the cancel atomic freed while the engine still reads it.
**Why:** the atomic is read by the compaction iterator asynchronously.
**How to avoid (verified safe by ordering):** the atomic is a TxPruner MEMBER (outlives the call); the compaction runs on the io thread; GeniusNode drains the io pool BEFORE `tx_pruner_.reset()` (`GeniusNode.cpp:2449-2467` + the wiring's own "reset only after the io pool has been drained" comment at `:5108`) and before `tx_globaldb_` release — the drain IS the join. Setting the atomic early in stop only shortens the wait.
**Warning signs:** any design that posts compaction onto a thread NOT drained before member release; a stack-local `std::atomic<bool>` at the call site.

### Pitfall 5: Timing-compounding on the settle/cycle gates
**What goes wrong:** compacting during the settle window or overlapping the next tick's evaluation (two engine-heavy operations colliding with sync writes).
**How to avoid:** D-01's hook point is by-construction safe — the compaction runs AFTER a completed cycle on the SAME thread that would run the next cycle (the 10-min reschedule cannot fire while the compaction holds the callback); the settle gate belongs to the NEXT cycle's start, not to compaction. Never move the call onto a different executor without re-deriving this serialization.
**Warning signs:** any PR that makes the compaction async/posted; merge-latency spikes correlated with compaction ticks.

### Pitfall 6: du-based assertions with WAL noise
**What goes wrong:** directory size includes the WAL (pre-flush contents), OPTIONS files, MANIFEST — deltas wobble beyond SST reclamation.
**How to avoid:** GetApproximateSizes is the PRIMARY assertion (it is also the production telemetry — the gate then proves the operator-visible number); du only as a loose sanity bound (e.g., post < pre, or post ≤ pre + small epsilon), never the ≥60% carrier.
**Warning signs:** gate flakes on runners with different WAL/manifest churn.

### Pitfall 7: Identity snapshots taken before compaction only (the D-C5 vacuity)
**What goes wrong:** the suite proves erase-time identity (18 already does) and re-runs it after a compaction that never happens — or snapshots before the compaction and compares stale bytes.
**How to avoid:** the compaction step is INSIDE the test between before/after snapshots; run it through the production `GlobalDB::CompactStorage` surface (not a test-side raw `CompactRange` — that would prove a different code path than production runs).
**Warning signs:** a GATE-02 test that never calls the compaction surface.

### Pitfall 8: Over-tight gate bounds
**What goes wrong:** exact-delta or ≥95% assertions flake across RocksDB versions/writers (D-03 rejected exact for exactly this).
**How to avoid:** ≥60% of erased logical bytes (compression ~10-30% on serialized protobuf + SST metadata absorb the rest — MEDIUM confidence, see Assumptions); an UPPER sanity bound (e.g., delta ≤ erased logical bytes × 1.5 + 1MB) catches double-counting bugs without brittleness.
**Warning signs:** CI-only failures on specific runners; assertions sensitive to fixture payload entropy (use low-entropy payloads for stable compression).

### Pitfall 9: The race test drifting into timing land
**What goes wrong:** sleep-based interleaving (wait-for-window) — the flake machine 19-07's deviations document exhaustively.
**How to avoid:** the hold is a blocking hook on the cycle thread; the mutation happens while the hook blocks; release via latch; asserts on STATS (skipped==1 this cycle, erased==1 next cycle) + state (value present→present→erased; marker timeline).
**Warning signs:** any `sleep_for` in the interleaving path; debug-only green (run the release-timing leg in the phase verification, per SC-5).

### Pitfall 10: Watermark assertion across restart
**What goes wrong:** tests assuming the cumulative watermark survives restart (it does not — D-01 in-memory only).
**How to avoid:** restart-related suites assert the documented behavior: watermark rebuilds from zero; worst case ONE early compaction per restart; `bytes_reclaimed` nullopt until the first post-restart compaction.
**Warning signs:** a status assertion expecting stale bytes_reclaimed after reopen.

## Code Examples

### The GlobalDB compaction surface (shape — final signatures are planner's)
```cpp
// Sources: rocksdb.hpp:100-103 (getDB), globaldb.cpp:151-159 (ActiveDataStore),
//          vendored db.h:1495-1500 (overloads), options.h (CompactRangeOptions)
outcome::result<void> GlobalDB::CompactStorage( const std::atomic<bool> *canceled )
{
    auto datastore = ActiveDataStore();          // nullptr after ShutdownNow
    if ( !datastore )
    {
        return outcome::failure( Error::GLOBALDB_NOT_STARTED );
    }
    ::ROCKSDB_NAMESPACE::CompactRangeOptions opts;
    opts.bottommost_level_compaction =
        ::ROCKSDB_NAMESPACE::BottommostLevelCompaction::kForce;  // load-bearing
    opts.allow_write_stall = false;              // D-C1: never stall writers
    opts.canceled          = const_cast<std::atomic<bool> *>( canceled );
    const auto status = datastore->getDB()->CompactRange( opts, nullptr, nullptr );
    if ( !status.ok() )
    {
        // Incomplete/kManualCompactionPaused on pre-set cancel == clean skip
        return outcome::failure( Error::ROCKSDB_IO );
    }
    return outcome::success();
}

outcome::result<uint64_t> GlobalDB::GetApproximateDiskUsageBytes() const
{
    auto datastore = ActiveDataStore();
    if ( !datastore ) { return outcome::failure( Error::GLOBALDB_NOT_STARTED ); }
    // NO nullptr/unbounded semantics here (unlike CompactRange): an explicit
    // sentinel high bound. Every real key class sorts below 0xFF bytes.
    const std::string high_bound( 16, '\xFF' );
    const ::ROCKSDB_NAMESPACE::Range whole( ::ROCKSDB_NAMESPACE::Slice(),
                                            ::ROCKSDB_NAMESPACE::Slice( high_bound ) );
    uint64_t bytes = 0;
    const auto     flags = ::ROCKSDB_NAMESPACE::DB::SizeApproximationFlags::INCLUDE_FILES
                         | ::ROCKSDB_NAMESPACE::DB::SizeApproximationFlags::INCLUDE_MEMTABLES;
    const auto     status =
        datastore->getDB()->GetApproximateSizes( &whole, 1, &bytes, flags );
    if ( !status.ok() ) { return outcome::failure( Error::ROCKSDB_IO ); }
    return bytes;
}
```

### The post-cycle compaction gate (TxPruner tail — shape)
```cpp
// Inside RunCycle, AFTER RecordCompletedCycle(globaldb, stats, cycle_start):
MaybeCompactAfterCycle( globaldb, stats );   // never fails the cycle

void TxPruner::MaybeCompactAfterCycle( crdt::GlobalDB &globaldb, const PruneCycleStats &stats )
{
    if ( stats.erased == 0 || compaction_canceled_.load() )
    {
        return;                               // D-01: nothing new / shutting down
    }
    if ( cumulative_erased_bytes_ < config_m.compact_threshold_bytes )
    {
        return;                               // below ~16MB — not worth a rewrite
    }
    const auto before = size_snapshot_this_cycle_;   // taken at cycle start (D-02)
    if ( globaldb.CompactStorage( &compaction_canceled_ ).has_error() )
    {
        logger_->warn( "Prune compaction failed — watermark kept, retrying next cycle" );
        return;                               // threshold still crossed = retry
    }
    const auto after = globaldb.GetApproximateDiskUsageBytes();
    if ( after.has_value() && before.has_value() )
    {
        const uint64_t reclaimed = before.value() > after.value()
                                 ? before.value() - after.value() : 0;
        const std::scoped_lock lock( status_mutex_ );
        status_m.bytes_reclaimed = reclaimed;  // stamped AT COMPACTION (D-02)
        cumulative_erased_bytes_ = 0;           // in-memory only (D-01)
        logger_->info( "Prune compaction: reclaimed {} bytes ({} -> {})", ... );
    }
}
```

### The seam-held race (SC-5 — test shape)
```cpp
// The re-verify's refuse paths (TxPruner.cpp:316-360): unreadable value,
// no outputs, ANY output not genuinely consumed (UTXO_CONSUMED && amount!=0,
// UTXOManager.cpp:909-915). Leg (a): mutate spend state while held -> skip
// THIS cycle. Leg (b): genuine consumed record present -> NEXT cycle erases.
std::binary_semaphore hold( 0 );
std::binary_semaphore release( 0 );
TxPruner::SetCycleInterruptHookForTest( [&]( int step )
{
    if ( step == TxPruner::kInterruptBeforeErase )
    {
        RevertSpendStateOnOutpoint( *target_tx, 0 );  // fixture /utxo/ write
        hold.release();                                // signal: mutation done
        release.acquire();                             // HOLD the cycle here
        return false;                                  // continue into re-verify
    }
    return false;
} );
// run cycle on a thread; wait for hold; assert nothing erased yet; release;
// join; EXPECT stats.skipped == 1 && erased == 0; value still present;
// LandGenuineConsumedRecord( *target_tx, 0 );           // the spend lands
// next cycle: EXPECT erased == 1; LookupPrunedEntry == PRUNED.
TxPruner::SetCycleInterruptHookForTest( nullptr );       // fixture discipline
```

### The gate assertion core (RECL-02 — shape)
```cpp
const uint64_t erased_logical_bytes = /* sum of seeded payload sizes (known) */;
auto before = db_->GetApproximateDiskUsageBytes();  // files+memtables, after prune
ASSERT_GE( before.value(), erased_logical_bytes * 8 / 10 );  // anti-vacuous pin
ASSERT_TRUE( db_->CompactStorage( nullptr ).has_value() );   // ONE compaction
auto after = db_->GetApproximateDiskUsageBytes();
const uint64_t reclaimed = before.value() - after.value();
EXPECT_GE( reclaimed, ( erased_logical_bytes * 60 ) / 100 )  // D-03 bound
    << "before=" << before.value() << " after=" << after.value();
EXPECT_LE( reclaimed, erased_logical_bytes * 2 + ( 1u << 20 ) );  // sanity ceiling
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `bytes_reclaimed` absent with Phase-20 Doxygen hook (`TxPruner.hpp:113-119`) | Real measured field stamped at compaction | This phase | RECL-03/04 complete; the hook text retires |
| RECL-03 INFO tail "bytes reclaimed: not measured until Phase 20" (`TxPruner.cpp:285-287`) | Counts-only line + separate compaction INFO | This phase (D-02 wording) | Honest split reporting |
| Deletes-only reclamation understanding | Tombstone-until-compaction proof in CI | This phase | The milestone's headline claim ("physically from the disk") becomes measured fact |
| 18's identity snapshots end at erase | Snapshots re-taken AFTER compaction (D-C5) | This phase | The rewrite itself is proven non-mutating |
| 19-07 mesh proves serving/failover | Mesh extended across prune+compact (SC-4) | This phase | Full/archive retention proven in the same topology |

**Deprecated/outdated:** none in source — the phase is additive. Note the recovered compaction recipe lives in git history (commit `fc228ef2f`, `.planning/research/STACK.md` as of 2026-10-15) because the live `.planning/research/STACK.md` on this branch is the v1.3 security stack; the recipe's facts were re-verified directly against vendored 9.4 this session and are restated above (the one correction: 9.4's `canceled` option post-dates the original recipe's "no cancellation" implication).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | A kForce whole-DB CompactRange over a 2-10MB DB completes in well under a few seconds on CI hardware (order: tens-to-hundreds of ms of rewrite + flush) | Runtime/CI timing | If pathological (e.g., allow_write_stall=false sleeping under load), the 600s default TIMEOUT still absorbs it; the fixture is quiescent (no concurrent writers) so the stall-wait path should not engage — MEDIUM confidence, no direct measurement run this session (building vendored RocksDB standalone for a bench was disproportionate); anchored indirectly: `globaldb_integration_test` 17 real-pubsub tests in 83s (19-07 summary); `tx_pruner_test` 31 tests inside the default timeout; real StartCore boot ~2.5s (19-06) |
| A2 | Default compression (RocksDB default = snappy when built in; options set none explicitly, `globaldb.cpp:194-203`) compresses the serialized-tx fixture payload ~0-40%, leaving the ≥60% reclamation bound comfortable | Gate bound rationale | If compression is off, reclamation is HIGHER (bound safer); if some future config enables heavy compression, the bound may need revisiting — use low-entropy payloads in the fixture for stability |
| A3 | The sentinel `0xFF…` high bound covers every key class in the shared DB | Measurement | Verified against the v1.2-STACK key map (binary CIDv1 `0x01…`, `/crdt/…`, `/utxo…`, `/bc-…`, `:sgns:…`, base58 `sealed` keys) — all sort below 0xFF under bytewise comparison; risk only if a new key class starts with 0xFF bytes (none exists; CIDv0 multihash-prefix keys also start low) |
| A4 | The D-04 race legs are: (a) spend-state change on the outpoint while held → re-verify skips THIS cycle; (b) genuine consumed record present → NEXT cycle erases ("picks it up") — assembled from the verified re-verify refuse paths + CONTEXT wording | SC-5 design | The CONTEXT's phrasing ("the spend lands… skips the erase THIS cycle") compresses both directions; the planner should confirm the fixture's exact mutation sequence against the owner's intent — the both-legs test satisfies either reading (never-erased-on-mutation + next-cycle-pickup) |
| A5 | `PruneCandidateResult`/`BlockGateResult` size extensions (or an equivalent assembly) suffice for the threshold estimate; exact byte accounting is unnecessary | Erased-bytes watermark | It is a ~16MB threshold trigger — a ±20% estimate is immaterial; if the owner wants exactness later it is an additive change, never a correctness bug |
| A6 | Line numbers cited drift by a few lines as phase edits land (files read this session at the cited lines) | throughout | Citation drift only; contracts re-verifiable by symbol name |

**All other claims were verified against vendored RocksDB 9.4 source or this repo's source this session** (file:line cited inline).

## Open Questions (RESOLVED)

1. **The compaction surface's exact naming and error mapping** (planner, discretion-adjacent)
   - What we know: two methods needed (compact + size); GlobalDB is the owner; `Error::ROCKSDB_IO` is the existing generic mapping.
   - Recommendation: `CompactStorage(const std::atomic<bool>*)` + `GetApproximateDiskUsageBytes()`; map non-OK statuses to ROCKSDB_IO with the status string WARN-logged (the cancel-skip Incomplete should return success-with-skipped or a distinct code so the tail logs once, not spams — planner picks the result shape).

2. **Where the cycle-start size snapshot lives when NO compaction runs that cycle** (planner)
   - What we know: D-02 needs cycle-start vs post-compaction; the threshold may cross cycles LATER than the deletes happened.
   - Recommendation: keep a `compact_pending_size_snapshot_` member = the size at the LAST compaction (or first completed cycle after restart/compaction); bytes_reclaimed then measures baseline→post-compaction across the whole accumulated span (the honest "total reclaimed since last compaction"). Alternative (per-cycle fresh snapshot) under-counts multi-cycle accumulation. Flag for a one-line decision in the plan.

3. **SC-4's host fixture** (planner)
   - What we know: two proven shapes — EraseNodeCollection (real pubsub GlobalDB mesh, no TransactionManager) vs the tx_pruner fixture + a second-node leg (19-06 responder precedent, full account stack).
   - Recommendation: the light node needs the TxPruner stack, so host on the tx_pruner-test side and add the full-node(s) as EraseNodeCollection-recipe GlobalDB nodes subscribed to the seed topic; assert full-node key-set/value byte-identity (tx values + CID blocks + heads) before/after the light's prune AND compact cycles. The NodeType::Full/Archive factory refusal is already covered (`TickGate_NonLightRolesRefusedAtConstruction`, `tx_pruner_test.cpp:1787`) — do not duplicate it.

4. **Whether `PruneCycleStats` also gains a compacted-this-cycle flag** (planner, small)
   - What we know: Observability_StatsAndStatusMatchFixtureActions asserts on stats; the compaction outcome lands in PruneStatus per D-02.
   - Recommendation: a `bool compacted` (+ optional `compaction_duration_ms`) on the stats keeps the per-cycle INFO and the status testable through the existing patterns.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Vendored RocksDB headers + impl | compaction surface (read-verified this session) | ✓ | 9.4 (`thirdparty/rocksdb`, `version.h` MAJOR 9 / MINOR 4) | — |
| Configured build tree | builds/tests | ✓ | `build/OSX/Release` (verified existing target layout) | reconfigure |
| cmake / ctest | build/test | ✓ | 3.31.4 | — |
| clang-format / clang-tidy | repo verification gate | ✓ (repo memory: keg-only llvm@22 paths + compile-db filter; challenge "can't run" claims) | — | must be run; state if unavailable |
| Standalone RocksDB timing bench | A1 (optional) | ✗ (not built) | — | order-of-magnitude estimate + the 600s default timeout; the gate test itself is the measurement |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** the timing bench (A1) — covered by estimate + timeout headroom.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest via repo `addtest` macro (`cmake/functions.cmake`) — auto ctest registration, `set_tests_properties(... TIMEOUT 600)` built in, `disable_clang_tidy` for test targets |
| Config file | none — per-target registration in `test/src/account/CMakeLists.txt` (gate target) + existing targets for extensions |
| Quick run command | `cmake --build build/OSX/Release --target tx_pruner_test reclamation_gate_test && build/OSX/Release/test_bin/reclamation_gate_test` (gate target name TBD) |
| Full suite command | `cmake --build build/OSX/Release && ctest --test-dir build/OSX/Release --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| RECL-01 (scheduled compaction runs) | Post-cycle trigger: erased≥1 ∧ watermark≥threshold fires ONE CompactRange through the GlobalDB surface; erased=0 or below-threshold never compacts; canceled-set never starts; failed compaction retries next cycle (watermark kept) | unit+integration (fixture-driven; status/stats-driven asserts) | `test_bin/tx_pruner_test --gtest_filter='*Compact*'` | ❌ Wave 0 |
| RECL-01 (off-hot-path / Light-only) | Trigger lives in the service tail (io/timer thread), factory gate already refuses non-Light (existing test `:1787` stays green); no DAG-worker posting (grep gate: no executor posts added) | gate + existing | `ctest -R tx_pruner_test` | ✅ (extend) |
| RECL-01 (du drops, SC-1 belt) | Directory bytes drop across prune+compaction on a known-history node | integration (gate target, belt bound) | `ctest -R reclamation_gate` | ❌ Wave 0 |
| RECL-02 (measured-bytes gate) | 200×~10KB seed → prune → ONE compaction → GetApproximateSizes delta ≥60% of erased logical bytes + sanity ceiling + anti-vacuous before-pin; du belt | integration (dedicated target, D-03: not in the quick matrix) | `ctest -R reclamation_gate` | ❌ Wave 0 |
| RECL-02/RECL-03 (bytes_reclaimed honesty) | nullopt until first compaction; stamped only on compaction success; erase-only cycles never fabricate; concurrent read coherent (existing `:2044` pattern extended) | unit+integration | `test_bin/tx_pruner_test --gtest_filter='*PruneStatus*'` | ✅ (extend) |
| GATE-02 (post-compaction identity) | Certs, SecureCRDT policy values (`/seed/` class), processing results, heads, seal markers byte-identical across prune AND compaction (snapshots re-taken after CompactStorage — D-C5) | integration | `test_bin/tx_pruner_test --gtest_filter='*PostCompaction*'` | ❌ Wave 0 |
| SC-4 (full/archive retention) | Full-node(s) in the mesh: tx key-set/value bytes + CID blocks + heads identical before/after the light's prune+compact cycles; light's re-broadcast causes no full-node mutation | integration (real pubsub mesh) | `ctest -R tx_pruner_test` (or the mesh host target) | ❌ Wave 0 |
| SC-5 (window-edge race) | Seam-held: spend-state change while held → re-verify skips THIS cycle (value+marker untouched); genuine spend next cycle → erased; debug+release legs | integration (blocking hook) | `test_bin/tx_pruner_test --gtest_filter='*SpendRace*'` | ❌ Wave 0 |
| Config knob | `pruning.compact_threshold_mb` parsed beside existing keys; absent keeps default; degenerate-0 rejected (WR-03 convention); resolved-values INFO extended | unit | `ctest -R prune_eligibility` (PruningConfig suite) | ✅ (extend) |
| Containment | Erase-surface gates still green; no new PutRaw/erase tokens outside sanctioned homes; the compaction surface adds NO key writes (CompactRange only) — EraseSurfaceGate target list unchanged unless the surface stages something (it must not) | gate tests | `test_bin/crdt_test --gtest_filter='*EraseSurfaceGate*'` | ✅ |

### Sampling Rate
- **Per task commit:** targeted `--gtest_filter` on the new/extended suites + clang-format/clang-tidy on changed files (production files only — addtest targets disable tidy)
- **Per wave merge:** `ctest --test-dir build/OSX/Release --output-on-failure` (full suite — default-off/no-change guarantee; the gate target rides here per D-03)
- **Phase gate:** full suite green + the dedicated gate target green in Debug AND Release (SC-5's release-timing leg) before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `GlobalDB::CompactStorage` + `GetApproximateDiskUsageBytes()` — the one recipe home
- [ ] `PruningConfig::compact_threshold_mb` (parse, degenerate-0 reject, resolved-values INFO)
- [ ] `PruneStatus.bytes_reclaimed` (+ stats `compacted` flag if adopted — Open Q4) + RECL-03 wording update (retire the "not measured" clause at `TxPruner.cpp:285-287` and the Doxygen hooks at `TxPruner.hpp:113-119, :65`)
- [ ] TxPruner trigger members (cumulative watermark, cycle-start/last-compaction size snapshot, cancel atomic) + `MaybeCompactAfterCycle` tail + erased-bytes estimate assembly (evaluation-carried sizes)
- [ ] Shutdown wiring: stop path sets the cancel atomic (verify the existing io-drain-then-reset ordering needs no change — research says it does not)
- [ ] `test/src/account/reclamation_gate_test.cpp` (name TBD) + `addtest` registration (copy the `tx_lookup_tri_state_test` block, `CMakeLists.txt:449-465`)
- [ ] tx_pruner_test extensions: post-compaction identity suite, seam-held race suite, compaction-trigger unit suite
- [ ] SC-4 mesh leg (host decision per Open Q3)
- [ ] Framework install: none needed (GTest integrated)

## Security Domain

ASVS level 1 (config: `security_enforcement: true`, `security_asvs_level: 1`, `security_block_on: high`).

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|------------------|
| V2 Authentication | no | no new network/auth surface |
| V3 Session Management | no | — |
| V4 Access Control | yes | Compaction stays Light-only by construction (the `TxPruner::New` factory gate — full/archive never construct the service, so never compact); the surface itself is storage-internal (no RPC exposure); shutdown fail-closed via `ActiveDataStore()` null-check |
| V5 Input Validation | yes | The threshold knob is per-key type-checked in `FromJSON` with the WR-03 degenerate-zero rejection (a 0 threshold would compact after every cycle — write-amp DoS on self); absent keys keep safe defaults; no new network-derived input |
| V6 Cryptography | no | value bytes are rewritten by compaction but never transformed — bit-for-bit identical payload bytes (the GATE-02 proof); no crypto paths touched |
| V7 Error handling | yes | `outcome::result` on both surfaces; a failed compaction never corrupts state (RocksDB atomicity) and never blocks the cycle's completed-status; cancel-skip is a distinct, logged outcome — never silent |
| V8 Data protection | yes (invariant) | Compaction performs NO data transformation (no filter, no TTL, no blob GC policy override); the destructive surface remains the landed guarded erase; identity ctest proves non-tx classes byte-identical across the rewrite |

### Known Threat Patterns for this stack
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Compaction-induced availability loss (write stalls / io starvation on a live light node) | DoS | `allow_write_stall = false` (sleeps until load low — never forces the DB into stall); off-hot-path thread (io/timer, never DAG worker); threshold prevents tiny-cycle compaction churn; cancellable at shutdown; D-01's post-cycle point serializes against the next tick by construction |
| Config-driven compaction flood (threshold=0 → compact every cycle) | DoS | WR-03 degenerate-zero rejection at parse (the `max_erases_per_cycle`/`settle_seconds` precedent) — the knob cannot express "always compact" |
| Data transformation during compaction (silent value mutation) | Tampering | No compaction filter installed (v1.2-STACK rejected); GATE-02 post-compaction byte-identity ctest is the standing proof; the recipe sets only kForce + write-stall + canceled — nothing that touches payload logic |
| Measured-bytes reporting lies (fabricated reclamation) | Repudiation | bytes_reclaimed stamped only from real GetApproximateSizes deltas at compaction success (D-02); nullopt until then; watermark discipline unchanged (monotonic, completed-cycles-only); the CI gate pins the measurement itself |
| Shutdown race corrupting the DB | Tampering/DoS | Verified ordering: io drained before `tx_pruner_.reset()` before `tx_globaldb_` release; the cancel atomic aborts in-flight work promptly; `ActiveDataStore()` fail-closed post-shutdown |
| Gate test vacuity (proving nothing while green) | Repudiation (of the milestone claim) | Anti-vacuous before-pin (pre-size ≥ ~80% of erased logical bytes), D-C5 post-compaction snapshots, bounded ceiling, D-03's dedicated-target discipline |

## Sources

### Primary (HIGH confidence — verified this session)
- Vendored RocksDB 9.4 (`thirdparty/rocksdb`): `include/rocksdb/db.h` (CompactRange overloads `:1495-1500`, whole-DB nullptr doc `:1482`, GetApproximateSizes `:1429-1456`, SizeApproximationFlags `:1423-1427`, operators `:2121+`, DisableManualCompaction `:1589-1600`); `include/rocksdb/options.h` (CompactRangeOptions full struct — canceled atomic, kForce default kIfHaveCompactionFilter, allow_write_stall); `db/db_impl/db_impl_compaction_flush.cc` (`:965-990` entry cancel-check + Incomplete(kManualCompactionPaused); `:1062-1132` CompactRangeInternal — `flush_needed = true` unconditional for null bounds, memtable flush before compaction; `:2089-2090` exclusive+canceled interplay comment); `db/db_impl/db_impl.cc:4690+` (GetApproximateSizes impl — internal-key conversion, no unbounded branch); `db/db_test.cc:1609-1619` (files-only ≈ 0 with memtable-resident data)
- `src/storage/rocksdb/rocksdb.hpp:100-103` (`getDB()` — the only public raw-DB accessor in SuperGNUS); `src/storage/rocksdb/rocksdb.cpp:30-70` (wrapper create/bloom/prefix-transform); vendored `ipfs_lite/rocksdb/rocksdb.hpp:74` (twin getDB)
- `src/crdt/globaldb/globaldb.cpp` — `:115-149` ShutdownNow (handle move), `:151-159` ActiveDataStore fail-closed, `:194-203` DB options (32MB buffer/target, level0 trigger 1, sync writes), `:324` same-handle wiring, `:797-890` EraseEntry compose (guards + staging + seam); `globaldb.hpp:196-262` EraseEntry contract
- `src/transaction/TxPruner.hpp` (whole file — PruneCycleStats `:68-110`, PruneStatus + the bytes_reclaimed Doxygen hook `:112-133`, seam constants `:239-249`, members `:321-340`); `TxPruner.cpp` — RunCycle `:82-241`, RecordCompletedCycle + the "not measured until Phase 20" INFO `:243-297`, TickGateSettled `:299-315`, OutputsRemainGenuinelyConsumed (refuse paths) `:316-360`, seam impl `:375-395`
- `src/transaction/PruneEligibility.hpp:150-260` (BlockGateResult, PruneEvaluationResult, PruningConfig incl. WR-03 zero-rejection wording); `PruneEligibility.cpp` — prefix enumeration `:147`, block gate `:369-440` (GetLocalDeltaKeys fetch, BLOCK_ALREADY_ERASED), EvaluateCandidate `:442-540` (value Buffer in hand, genuine-consumption + spender ladder)
- `src/account/GeniusNode.cpp` — `:2449-2467` ReleaseRuntimeMembersAfterIoStopped (tx_pruner_.reset after drain), `:4281-4292` GetPruneStatus delegation, `:4892-4930` StartPruneTimerIfConfigured, `:5091-5169` RunPruneCycle (mode gate, snapshot, generation check, error split, reschedule tail; the "reset only after the io pool has been drained" comment `:5108`)
- `src/account/UTXOManager.cpp:909-927` (IsOutPointGenuinelyConsumed / GetOutPointSpender — state+amount semantics under shared_lock)
- `test/src/account/tx_pruner_test.cpp` — snapshot helpers `:1204-1318`, RealPrune identity test `:1331+`, seam usages `:2015-2018, :2097-2114, :2351-2360, :2461-2498`, ComposeLock_HeadGuard refusal+retry `:2265-2317`, fixture helpers (`kSmallSettleSeconds` `:167`, MakePruner `:684`, RunSettledCycle `:712`), test inventory `:1331-2904`
- `test/src/crdt/globaldb_erase_test.cpp:83-161` (EraseNodeCollection: addNode/makeNode/restartNodePendingStart/connectNodes one-direction dial)
- `test/src/account/CMakeLists.txt:401-465` (tx_pruner_test + tx_lookup_tri_state_test registration blocks); `cmake/functions.cmake` addtest (TIMEOUT 600 built in)
- `.github/workflows/cmake.yml:604, :674-676` (CI runs the full suite `-LE requires_foundry` — no include list, so new targets ride automatically)

### Secondary (MEDIUM-HIGH confidence)
- v1.2 milestone STACK research (recovered from git `fc228ef2f` — the live `.planning/research/STACK.md` is the v1.3 stack on this branch): the original compaction recipe, key map, DB options inventory — cross-checked this session against vendored source (all facts re-verified; one upgrade: 9.4's canceled option)
- `.planning/phases/18-*/18-{CONTEXT,RESEARCH}.md`, `18-04-SUMMARY.md` — the cycle orchestration, seam, bytes_reclaimed-by-design record
- `.planning/phases/19-*/19-06-SUMMARY.md`, `19-07-SUMMARY.md` — restart-matrix harness lessons (io_ hand-driven, responder leg, key-set vs byte-identity discipline), mesh fixture lessons (cache races, serving-topic determinism, per-instance observables)
- RocksDB wiki Manual-Compaction / DeleteRange blog / issue #8041 (via the recovered STACK research; mechanics re-verified in-tree)

### Tertiary (LOW confidence)
- None used for any load-bearing claim. A1/A2 estimates flagged in the Assumptions Log.

## Metadata

**Confidence breakdown:**
- RocksDB 9.4 API surface (overloads, options, cancellation, flush semantics, sizes API): HIGH — read directly from vendored headers AND implementation this session
- Architecture (surface placement, trigger point, shutdown ordering): HIGH — every cited location read; teardown ordering verified against the actual release path
- Measurement design (flags, sentinel range, memtable trap): HIGH — implementation + RocksDB's own test evidence
- Gate/CI shape: HIGH on mechanics (addtest/TIMEOUT/CI full-suite), MEDIUM on runtime bound (A1/A2 — order-of-magnitude, no standalone bench run)
- Race/identity/retention harness shapes: HIGH on mechanisms (seam, snapshot helpers, mesh recipes all read), MEDIUM on the SC-5 mutation-sequence wording (A4)

**Research date:** 2026-10-24
**Valid until:** 2026-11-23 (stable internal codebase + vendored RocksDB; re-verify if `src/transaction/TxPruner.*`, `src/crdt/globaldb/globaldb.*`, or the thirdparty RocksDB pin changes first)
