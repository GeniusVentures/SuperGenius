# Phase 20: Reclamation Proof & E2E Hardening - Pattern Map

**Mapped:** 2026-10-24
**Files analyzed:** 11 (8 modified, 1 created, 2 conditionally modified)
**Analogs found:** 9 / 11 with in-repo matches (2 ride vendored-RocksDB references — see No Analog Found)

> Every line citation below was read this session against the working tree at `gsd/phase-19-pruned-node-survival-recovery-hardening` (HEAD `e69c3f65d`). Vendored RocksDB lives at `/Users/henriqueklein/gnus/thirdparty/rocksdb` (workspace checkout, 9.4) — NOT under `SuperGNUS/thirdparty` (that path does not exist; resolves via the workspace root).

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/crdt/globaldb/globaldb.hpp` | service (storage facade contract) | request-response | `globaldb.hpp` EraseEntry contract `:204-266` + GetRaw/QueryRaw `:183-202` | exact (same file, newest owner-vended surface) |
| `src/crdt/globaldb/globaldb.cpp` | service (storage facade impl) | request-response | `QueryRaw` impl `globaldb.cpp:780-788` + `EraseEntry` impl `:797-892` | exact |
| `src/transaction/TxPruner.hpp` | service (scheduling policy + status) | batch (cycle tail) | itself — `PruneCycleStats`/`PruneStatus` `:58-133`, members `:321-340` | exact (in-place extension) |
| `src/transaction/TxPruner.cpp` | service (cycle orchestration) | batch | `RecordCompletedCycle` `:243-297` + `RunCycle` tail `:233-241` | exact (in-place extension) |
| `src/transaction/PruneEligibility.hpp` | config (parse/validate contract) | transform (JSON→policy) | `PruningConfig` struct + FromJSON doc `:216-273` | exact |
| `src/transaction/PruneEligibility.cpp` | config (parse impl) | transform | `FromJSON` `settle_seconds` block `:103-109` | exact (the copy-me knob block) |
| `src/account/GeniusNode.cpp` | wiring (config INFO + stop path) | event-driven (timer/shutdown) | resolved-values INFO `:478-492` + `ReleaseRuntimeMembersAfterIoStopped` `:2449-2467` | exact |
| `test/src/account/reclamation_gate_test.cpp` (name TBD — CREATED) | test (dedicated measured-bytes gate) | batch (seed→prune→compact→measure) | `tx_lookup_tri_state_test.cpp` fixture family `:1-80` + tx_pruner seeding helpers | role-match (fixture exact; the byte-measurement assertion itself has NO analog) |
| `test/src/account/tx_pruner_test.cpp` | test (identity/race/trigger suites) | batch + seam-held interleaving | own suites: RealPrune `:1331-1443`, ConcurrentRead `:2044-2122`, ComposeLock `:2265-2317`, crash hooks `:2461-2498` | exact (extend, don't fork) |
| `test/src/account/CMakeLists.txt` | config (build) | — | `tx_lookup_tri_state_test` block `:448-470` | exact (copy-block) |
| `test/src/crdt/globaldb_erase_test.cpp` | test (SC-4 mesh host — CONDITIONAL per Open Q3) | event-driven (real pubsub) | `EraseNodeCollection` `:83-163` | exact (if this host wins; otherwise the recipe moves to tx_pruner_test) |

---

## Pattern Assignments

### 1. `src/crdt/globaldb/globaldb.{hpp,cpp}` (service, request-response)

**Analog:** the same file's newest owner-vended surface — `EraseEntry` (contract) and `QueryRaw` (impl). Both were read in full; the new `CompactStorage(const std::atomic<bool>*)` + `GetApproximateDiskUsageBytes()` copy their shape exactly.

**Contract placement + doc pattern** — declare beside `EraseEntry` (`globaldb.hpp:264-266` shows the signature style; Doxygen @brief/@details/@param/@return blocks like `:204-263`):

```cpp
// globaldb.hpp:264-266 — the newest public surface's signature shape
outcome::result<void> EraseEntry( const std::string                              &key,
                                  const CID                                      &cid,
                                  const std::optional<std::pair<Buffer, Buffer>> &extra_entry = std::nullopt );
```

**Fail-closed impl pattern** (`globaldb.cpp:780-788` — the raw-surface convention; note it uses `std::errc::operation_canceled`, while the heavier `EraseEntry` uses `Error::GLOBALDB_NOT_STARTED` at `:803-807`. Research recommends `GLOBALDB_NOT_STARTED` for the compaction surface; pick ONE and match whichever neighbor the planner considers the family — both precedents are in this file):

```cpp
// globaldb.cpp:780-788
outcome::result<GlobalDB::QueryResult> GlobalDB::QueryRaw( const Buffer &key_prefix ) const
{
    auto datastore = ActiveDataStore();
    if ( !datastore )
    {
        return outcome::failure( std::errc::operation_canceled );
    }
    return datastore->query( key_prefix );
}
```

`ActiveDataStore()` (`globaldb.cpp:151-159`) is the shutdown fail-close the surface inherits for free — after `ShutdownNow` moves `m_datastore` (`:130-135`) it returns nullptr under `lifecycle_mutex_`.

**The raw-DB accessor the surface wraps** (`src/storage/rocksdb/rocksdb.hpp:100-103`):

```cpp
[[nodiscard]] const std::shared_ptr<DB> &getDB() const
{
    return db_;
}
```

**Vendored reference — CompactRangeOptions** (`thirdparty/rocksdb/include/rocksdb/options.h:2053-2107`; verified this session). The load-bearing members:

```cpp
// options.h:2071-2072 — the default compacts NOTHING without a filter: kForce is mandatory (D-C1)
BottommostLevelCompaction bottommost_level_compaction =
    BottommostLevelCompaction::kIfHaveCompactionFilter;
// options.h:2075 — false = sleeps until load is low (never force a stall)
bool allow_write_stall = false;
// options.h:2082-2086 — 9.4-native first-class cancellation
// Allows cancellation of an in-progress manual compaction.
std::atomic<bool>* canceled = nullptr;
```

`options.h:2087-2093` NOTE: `DisableManualCompaction()` overwrites the user-provided canceled variable — the reason the TxPruner-owned atomic (not Disable/Enable) is the chosen belt.

**Vendored reference — whole-DB CompactRange + the sizes API** (`thirdparty/rocksdb/include/rocksdb/db.h:1470-1501` and `:1421-1456`):

```cpp
// db.h:1479-1482 — ONLY CompactRange has nullptr/unbounded semantics
//    db->CompactRange(options, nullptr, nullptr);   // compacts the entire database
// db.h:1495-1501 — the two overloads
virtual Status CompactRange(const CompactRangeOptions& options,
                            ColumnFamilyHandle* column_family,
                            const Slice* begin, const Slice* end) = 0;
virtual Status CompactRange(const CompactRangeOptions& options,
                            const Slice* begin, const Slice* end) { ... }

// db.h:1423-1427 — flags enum (NONE/INCLUDE_MEMTABLES/INCLUDE_FILES)
// db.h:1444-1448 — simpler overload; DEFAULT IS FILES-ONLY (the Pitfall-1 trap:
// the 32MB write buffer at globaldb.cpp:200 keeps small fixtures in the memtable)
virtual Status GetApproximateSizes(ColumnFamilyHandle* column_family,
                                   const Range* ranges, int n, uint64_t* sizes,
                                   SizeApproximationFlags include_flags =
                                       SizeApproximationFlags::INCLUDE_FILES);
```

**GetApproximateSizes has NO unbounded range** (unlike CompactRange): `Range(Slice(), Slice())` is the EMPTY range `["", "")` — the surface must build an explicit `0xFF…` sentinel high bound (research Pitfall 2, verified against `db_impl.cc:4690+`). The sentinel + `INCLUDE_FILES | INCLUDE_MEMTABLES` knowledge lives in exactly this one method.

**Error mapping:** reuse `Error::ROCKSDB_IO` (`globaldb.hpp:88`) for non-OK statuses; WARN-log the status string per the existing `m_logger->error("{}: ...", __func__)` style (`globaldb.cpp:805`).

---

### 2. `src/transaction/TxPruner.{hpp,cpp}` (service, batch — in-place extension)

**Analog:** itself. Read both files in full (344 + 401 lines). Four extension points:

**(a) The D-01 hook point** — `RunCycle` tail (`TxPruner.cpp:233-241`). `MaybeCompactAfterCycle(globaldb, stats)` goes between `RecordCompletedCycle` and `return stats` (post-status-update, pre-reschedule — the wiring's reschedule at `GeniusNode.cpp:5151-5168` fires only after RunCycle returns):

```cpp
// TxPruner.cpp:233-241
if ( InterruptRequested( kInterruptBeforeStatus ) )
{
    stats.duration_ms = ElapsedMs( cycle_start );
    return stats;
}

RecordCompletedCycle( globaldb, stats, cycle_start );
return stats;   // ← the compaction gate goes between these two lines
```

**(b) The RECL-03 clause retiring** (`TxPruner.cpp:282-296` — the comment at 282-284 and the trailing log clause at 285-287):

```cpp
// TxPruner.cpp:285-296 (excerpt)
logger_->info( "Prune cycle [generation {}]: examined={}, eligible={}, erased={}, held_blocks={}, "
               "skipped={}, errors={}, duration_ms={}, watermark={} — bytes reclaimed: not measured "
               "until Phase 20",     // ← DROP this clause; keep the line counts-only (D-02)
               stats.generation, ... DescribeWatermark( watermark_before, watermark_after ) );
```

**(c) `PruneStatus` gains the field; the Doxygen hooks retire** (`TxPruner.hpp:112-133`; also the PruneCycleStats note at `:62-66` "bytes_reclaimed is deliberately absent" and the hpp:65 "before Phase 20's CompactRange proof" wording):

```cpp
// TxPruner.hpp:121-133 (current — the @details block at :114-119 is the retiring hook)
struct PruneStatus
{
    std::optional<uint64_t> watermark;
    PruneCycleStats         last_cycle;
    // ADD: std::optional<uint64_t> bytes_reclaimed;  — stamped ONLY at
    // compaction success under status_mutex_ (D-02); nullopt = no compaction
    // since restart (the honest state)
};
```

The status-mutex copy discipline to extend: `RecordCompletedCycle`'s `const std::scoped_lock lock( status_mutex_ );` block (`TxPruner.cpp:262-275`) and `GetPruneStatus` (`:365-369`).

**(d) New members** — append beside `config_m`/`status_m` (`TxPruner.hpp:321-340`): `cumulative_erased_bytes_` (uint64_t, in-memory only, D-01), `compact_pending_size_snapshot_` (per Open Q2), `std::atomic<bool> compaction_canceled_`. The existing member-atomic precedents in this codebase are `GlobalDB::shutdown_started_` / `stop_backup_thread_` (`globaldb.hpp:452-453`, `std::atomic_bool`) — member scope is what makes the `canceled` pointer outlive the in-flight CompactRange (research Pitfall 4).

**Supporting helpers already in file:** `ElapsedMs` (`TxPruner.cpp:41-46`) for the compaction INFO duration; the anonymous-namespace layout (`:28-66`) for any new private helper.

**Erased-bytes estimate extension point (evaluation-carried sizes, research A5):** `PruneEligibility::EvaluateCandidate` already receives the value Buffer (`PruneEligibility.cpp:442-446`) and the block gate fetches the block via `GetLocalDeltaKeys` (`:379`) — a size field on `PruneCandidateResult` (`PruneEligibility.hpp:128-155`) or equivalent assembly rides these existing reads; no second fetch path. Optional `bool compacted` (+ `compaction_duration_ms`) on `PruneCycleStats` (`TxPruner.hpp:68-110`) per Open Q4.

---

### 3. `src/transaction/PruneEligibility.{hpp,cpp}` (config, transform)

**Analog:** the `settle_seconds` knob — the exact copy-me block for `compact_threshold_mb` (WR-03 degenerate-0 rejection convention).

**Struct member + doc** (`PruneEligibility.hpp:243-247`):

```cpp
/// Settle window in seconds (D-C2): ... Default 60. A degenerate 0 is rejected
/// at read time and the member keeps its safe default.
uint64_t settle_seconds = 60;
```

**Parse block** (`PruneEligibility.cpp:103-109` — insert the twin after it):

```cpp
// D-C2 settle knob: a degenerate 0 would let a cycle fire during
// initial sync; rejected at read time, default retained.
if ( pruning_object.HasMember( "settle_seconds" ) && pruning_object["settle_seconds"].IsUint64() &&
     pruning_object["settle_seconds"].GetUint64() > 0 )
{
    config.settle_seconds = pruning_object["settle_seconds"].GetUint64();
}
```

Extend the `FromJSON` @details doc (`PruneEligibility.hpp:253-271`) with the new key's rule (a 0 threshold would compact after every cycle — write-amp DoS on self; absent keeps default ~16MB). Also update the FromJSON tests in the prune_eligibility suite (`ctest -R prune_eligibility`) per the Validation map.

---

### 4. `src/account/GeniusNode.cpp` (wiring, event-driven — MINIMAL)

**Analog A — resolved-values INFO to extend** (`GeniusNode.cpp:478-492`):

```cpp
// GeniusNode.cpp:485-491 — add compact_threshold_mb to the key/value list
node_logger_->info( "sgns_config.json: pruning enabled={}, finality_window={}, dry_run={}, "
                    "max_erases_per_cycle={}, settle_seconds={}",
                    pruning_config_.enabled,
                    pruning_config_.finality_window.value_or( 0 ),
                    pruning_config_.dry_run,
                    pruning_config_.max_erases_per_cycle,
                    pruning_config_.settle_seconds );
```

**Analog B — shutdown ordering (VERIFIED: no structural change needed).** `ReleaseRuntimeMembersAfterIoStopped` (`GeniusNode.cpp:2449-2467`) cancels the prune timer and resets the pruner only after the io pool has drained (the stop sequence's own comment at `:2572`: "until the io_context has been stopped and all handlers have drained"; the RunPruneCycle comment at `:5106-5108`: "reset only after the io pool has been drained"):

```cpp
// GeniusNode.cpp:2459-2467
if ( prune_dry_run_timer_ )
{
    boost::system::error_code ignored;
    prune_dry_run_timer_->cancel( ignored );
    prune_dry_run_timer_.reset();
}
// The TxPruner holds only status/config (no runtime resources) —
// release it beside its timer once the io pool is drained (18-04).
tx_pruner_.reset();
```

Because the compaction runs synchronously inside `RunCycle` on the io thread (`GeniusNode.cpp:5130-5133` is the only call site), the drain IS the join — an in-flight CompactRange returns before `tx_pruner_.reset()` and before `tx_globaldb_` release (`:2487`). The only candidate change: set the cancel atomic early in the stop path for promptness (a one-liner beside the timer cancel, e.g. a `TxPruner::RequestCompactionCancel()` on the `tx_pruner_` object before reset). Timer construction/reschedule patterns for reference: `StartPruneTimerIfConfigured` `:4892-4951`, reschedule tail `:5151-5168`.

---

### 5. `test/src/account/reclamation_gate_test.cpp` (CREATED — test, batch)

**Fixture analog:** `tx_lookup_tri_state_test.cpp` — the newest member of the family; "this file owns its own copy and includes no sibling test" (`:7-10`). Copy its skeleton verbatim: Doxygen file header, includes block (`:26-42`), `CRDTFixture` derivation with the per-file name string (`:64-69`), `MemorySecureStorage` factory + `GeniusAccount::New` + `LoadUTXOs` SetUp (`:71-80`).

```cpp
// tx_lookup_tri_state_test.cpp:64-69
class TxLookupTriStateTest : public ::test::CRDTFixture
{
public:
    TxLookupTriStateTest() : ::test::CRDTFixture( "tx_lookup_tri_state_test" )
    {
    }
```

**Seeding/驱动 helpers to copy from `tx_pruner_test.cpp`:** `MakeTransfer` (`:285-303` — build the ~10KB payloads here), `SeedTransaction` (`:306-314` — the REAL publish path, never raw puts), `TrackOutputs` (`:389-399`), `ConsumeOutput` (`:402-412`), `MakePruner` (`:684-695` — set the new `compact_threshold_mb` member here; NOTE it takes k_budget/settle args, add the threshold), `RunSettledCycle` (`:712-736`, bounded `ASSERT_WAIT_FOR_CONDITION` with `kSmallSettleSeconds * 1000 + 8000` headroom).

**Measurement path:** exercise the PRODUCTION surface `GlobalDB::CompactStorage` / `GetApproximateDiskUsageBytes` (research Pitfall 7 — never a test-side raw CompactRange). The assertion core (≥60% bound + sanity ceiling + anti-vacuous before-pin) has NO in-repo analog — use RESEARCH.md "Code Examples → The gate assertion core" verbatim as the shape. The `du` belt walks `db_path_` (`test/testutil/storage/base_crdt_test.hpp:42`, a plain `std::string` member) with `boost::filesystem` — loose sanity bound only (WAL/OPTIONS noise, research Pitfall 6).

**CMake registration** (see entry 8 below).

---

### 6. `test/src/account/tx_pruner_test.cpp` (test — EXTENDED: GATE-02, SC-5, trigger units, SC-4 mesh option)

**Analog A — GATE-02 post-compaction identity (D-C5):** the RealPrune suite (`:1331-1443`) IS the pattern; the extension inserts `db_->CompactStorage(...)` between the before-snapshots (`:1367-1377`) and the after-assertions (`:1428-1443`). The six snapshot helpers (`:1204-1315`) are reused as-is: `SnapshotPrefix`, `SnapshotCrdtEntriesContaining` (note its `/crdt/work/` transient-family exclusion at `:1238-1244`), `SnapshotHeads`, `SnapshotSeals`, `SnapshotBlocks`, `SnapshotMapEntry`.

**Analog B — SC-5 seam-held hold (the blocking-lambda pattern):** `PruneStatus_ConcurrentReadDuringCycleIsCoherent` (`:2044-2122`) is the exact mechanism — atomics + deadline-bounded spin inside the hook + `return false` to continue:

```cpp
// tx_pruner_test.cpp:2097-2111 — the hold at kInterruptAfterEvaluation; SC-5 uses
// kInterruptBeforeErase (:198's seam) with the spend mutation done before release
sgns::TxPruner::SetCycleInterruptHookForTest(
    [&]( int step )
    {
        if ( step == sgns::TxPruner::kInterruptAfterEvaluation )
        {
            hook_entered.store( true );
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
            while ( !release_cycle.load() && std::chrono::steady_clock::now() < deadline )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            return false; // continue the cycle (the hold was the point)
        }
        return false;
    } );
```

The deadline (`:2103-2107`) is load-bearing: a hung interleaving fails the test rather than hanging it. Fixture discipline: always the clear-on-every-exit pair (`:2114`, `:2498-2499`) and the destructor-first clear documented in the file header (`:26-29`).

**Analog C — SC-5 leg assertions (skip-this-cycle → next-cycle-erases):** `ComposeLock_HeadGuardRefusesAtEraseTimeNeverTorn` (`:2265-2317`) is the assertion shape — stats-based (`skipped`/`errors` this cycle, `erased==1` next cycle at `:2309-2316`), state-based (`LookupPrunedEntry` NOT_FOUND→PRUNED at `:2297-2299`, `:2314-2316`), never timing-based. The spend mutation on the held outpoint uses the `ConsumeOutput`/UTXO surface (`:2274`, `UTXOManager.cpp:909-915` semantics).

**Analog D — counting hooks (visit-selective fire):** the crash matrix's visit counters (`:2351-2360` `kCrashKillPoints`, `:2461-2474` `++visits < invocation` guard) if the race needs mid-sequence selectivity.

**Do not duplicate:** `TickGate_NonLightRolesRefusedAtConstruction` (`:1786-1804`) already pins the Full/Archive factory refusal — SC-4's "full node = never constructs a pruner" is covered by it (research Open Q3).

**SC-4 mesh host (Open Q3 — planner picks):** recommendation is THIS file + EraseNodeCollection-recipe GlobalDB second nodes (the 19-06 responder precedent — a second real pubsub host already lives on this fixture: `nonce_responder_pubsub_`/`nonce_responder_account_` members `:266-275` with the messenger-stop-before-pubsub-stop teardown `:247-259`).

---

### 7. `test/src/crdt/globaldb_erase_test.cpp` (test — CONDITIONAL SC-4 host)

**Analog:** `EraseNodeCollection` (`:83-163`) — the 19-07 real-pubsub N-node topology. Its three load-bearing lessons, read in place:

```cpp
// :127-142 restartNodePendingStart — db FIRST, then pubsub; recreate WITHOUT
// db->Start() so the caller reconnects peers then Start()s into a live connection
// :148-163 connectNodes — ONE-direction dial only (two dials inside one gossip
// heartbeat collide on the shared source tuple — the multi_node lesson)
```

`addNode` (`:103-116`) builds per-test fresh paths via `removeAllWithRetry`; `makeNode` (`:172+`) carries the loopback-advertise requirement (a wildcard-only listener silently drops every Broadcast). Only chosen if the planner hosts SC-4 here instead of tx_pruner_test (Open Q3) — one host, not both (DRY).

---

### 8. `test/src/account/CMakeLists.txt` (config, build)

**Analog:** the `tx_lookup_tri_state_test` block (`:448-470`) — copy the whole block, rename the target:

```cmake
# test/src/account/CMakeLists.txt:448-470
# Phase 19 (19-05): D-03 tri-state tx-lookup suites — same link set as
# tx_pruner_test (the fixture family it copies).
addtest(tx_lookup_tri_state_test
    tx_lookup_tri_state_test.cpp
)

target_link_libraries(tx_lookup_tri_state_test
    genius_node_test
    json_secure_storage
    base_crdt_test
)

if(MSVC)
    target_link_options(tx_lookup_tri_state_test PUBLIC /WHOLEARCHIVE:$<TARGET_FILE:genius_node_test>)
elseif(APPLE)
    target_link_options(tx_lookup_tri_state_test PUBLIC -force_load "$<TARGET_FILE:genius_node_test>")
else()
    target_link_options(tx_lookup_tri_state_test PUBLIC
        "-Wl,--whole-archive"
        "$<TARGET_FILE:genius_node_test>"
        "-Wl,--no-whole-archive"
    )
endif()
```

`addtest` (`cmake/functions.cmake:8-38`) already provides: ctest registration (`:17-20`), `TIMEOUT 600` (`:21` — D-03's generous timeout is free), `test_bin` output dir, and `disable_clang_tidy` (`:27`). CI registers new targets automatically (full suite `-LE requires_foundry`, no include list — research Sources). The `RESOURCE_LOCK genius_node_pool` tail (`CMakeLists.txt:472-478`) is for multi-node-booting suites — the gate target boots ONE node; add it to the lock list only if the chosen fixture proves heavyweight.

---

## Shared Patterns

### Fail-closed shutdown guard (all new GlobalDB surface methods)
**Source:** `globaldb.cpp:151-159` (`ActiveDataStore`) — first act of every method; nullptr after `ShutdownNow` moved `m_datastore` (`:130-135`). Apply to `CompactStorage` and `GetApproximateDiskUsageBytes`.

### outcome::result error contract
**Source:** `globaldb.hpp:86-96` (`Error` enum — `ROCKSDB_IO`, `GLOBALDB_NOT_STARTED` both exist) + `TxPruner.cpp:118-128` (permanent vs transient split via `IsPermanentStop`, `:371-378`). Apply to: the surface (failure = WARN + watermark kept → retry next cycle, never silent, never fails the completed cycle); the config parse (reject-at-read keeps member default). Note the two in-file precedents for the post-shutdown code: `std::errc::operation_canceled` (raw surfaces, `globaldb.cpp:785`) vs `Error::GLOBALDB_NOT_STARTED` (EraseEntry, `:806`) — pick one, document it.

### Status-mutex copy discipline
**Source:** `TxPruner.cpp:262-275` (`const std::scoped_lock lock( status_mutex_ )` around every `status_m` touch) + `:365-369`. Apply to the `bytes_reclaimed` stamp at compaction time.

### Test-seam fixture discipline
**Source:** `tx_pruner_test.cpp:26-29` (header contract), `:2114`/`:2498-2499` (clear on every exit path), `globaldb.hpp:268-276` (the GlobalDB twin seam). Apply to every new hook-armed test; blocking hooks always carry the 5s deadline (`:2103-2107`).

### Real-publish seeding (never raw local puts)
**Source:** `SeedTransaction` `tx_pruner_test.cpp:305-314` — value goes through `db_->Put(..., { kSeedTopic })` so blocks/heads/seals/priorities all exist; only the tx→CID map entry is a raw put (`:380-386`). Apply to the gate fixture's 200×~10KB seeding (low-entropy payloads for stable compression, research A2).

### Doxygen file header + documented public functions
**Source:** every file read this session (e.g. `TxPruner.hpp:1-29`, `globaldb_erase_test.cpp:1-14`, `tx_lookup_tri_state_test.cpp:1-24`). Required by project CLAUDE.md for the new gate test file and any new public methods.

---

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `reclamation_gate_test.cpp` — the GetApproximateSizes before/after assertion | test | batch-measurement | ZERO `CompactRange`/`GetApproximateSizes` callers exist in `src/` or `test/` (verified by grep; only hits are the Phase-20 Doxygen hooks). Use RESEARCH.md "Code Examples → gate assertion core" as the shape; fixture skeleton still copies `tx_lookup_tri_state_test.cpp`. |
| `GlobalDB::CompactStorage` recipe body | service | request-response | No in-repo CompactRange call exists; the vendored headers ARE the reference (`options.h:2053-2107`, `db.h:1470-1501`) — excerpted above so the planner never needs to open them. |

## Metadata

**Analog search scope:** `src/crdt/globaldb/`, `src/transaction/`, `src/account/`, `src/storage/rocksdb/`, `test/src/account/`, `test/src/crdt/`, `test/testutil/storage/`, `cmake/`, vendored `/Users/henriqueklein/gnus/thirdparty/rocksdb/include/rocksdb/`
**Files read:** 14 (all citations above read this session; TxPruner.hpp/.cpp, globaldb.hpp, base_crdt_test.hpp, functions.cmake read in full)
**Pattern extraction date:** 2026-10-24
**Verification note for planner:** research line citations drift-checked — all anchors confirmed at the cited lines on this branch (A6 applies going forward).
