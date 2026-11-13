---
phase: 19-pruned-node-survival-recovery-hardening
plan: 05
subsystem: transaction
tags: [surv-02, d-03, tri-state-lookup, pruned-lookup, value-first-ordering, geniusnode-passthrough, cli, outcome-result]

# Dependency graph
requires:
  - phase: 19-pruned-node-survival-recovery-hardening (17-05 design / 18 production)
    provides: PruneEligibility::PrunedLookup + LookupPrunedEntry — the one verdict vocabulary and marker lookup this plan composes verbatim
  - phase: 19-pruned-node-survival-recovery-hardening (19-04)
    provides: the InitializeUTXOs marker-consult precedent (loud three-way shape) and the tx_pruner_test fixture family this suite copies
provides:
  - SURV-02's local half closed: TransactionManager::LookupTransaction answers found / not-found / pruned distinctly, with the PrunedTransactionRecord payload on PRUNED and a loud, observable invariant-violation verdict on value-plus-marker
  - GeniusNode::LookupTransaction passthrough + the example CLI `tx <hash>` command ("history pruned" is a distinct string)
  - The TxLookupTriStateTest suite (5 cases, new tx_lookup_tri_state_test target, double-run green)
affects: [SURV-02 (complete — peer half closed by 19-07), RPC/transport mapping (deferred per D-03)]

# Tech tracking
tech-stack:
  added: [] # zero new dependencies
  patterns: [value-first-then-marker lookup composition (tracked map -> exact-hash CRDT probe -> marker consult; a found value outranks a marker and the violation is carried on the result, not just logged), opaque-enum forward declaration to keep a result struct dependency-free (no rapidjson leak into every TM.hpp consumer)]
key-files:
  created:
    - test/src/account/tx_lookup_tri_state_test.cpp
  modified:
    - src/transaction/TransactionManager.hpp
    - src/transaction/TransactionManager.cpp
    - src/account/GeniusNode.hpp
    - src/account/GeniusNode.cpp
    - example/node_test/NodeExample.cpp
    - test/src/account/CMakeLists.txt

key-decisions:
  - "The invariant-violation encoding: on a found value with a surviving marker the result carries verdict VALUE_PRESENT_INVARIANT_VIOLATION *and* the found tx (tx-first reading; a clean found is verdict NOT_FOUND + tx). The plan's 'report loudly while still returning the found tx' is thereby observable on the result itself — deterministic to assert, no log scraping — and PRUNED is never reported when a value exists"
  - "TransactionLookupResult declares PrunedLookup via an opaque enum forward declaration instead of including PruneEligibility.hpp: the include drags rapidjson into every TM.hpp consumer and broke the migration library build (no rapidjson include path). Consequence: no default member initializer for verdict — the single construction site (LookupTransaction) always sets it, documented on the member"
  - "The tracked-map test observable is GetTransactionStatusByTxId (public, scans exactly tx_processed_m; INVALID == not tracked) because GetTransactionByHash is private — the same per-binary seam-free shape the family prefers when a public discriminator exists"
  - "The example CLI maps verdicts in tx-first order (found line, then verdict switch) so the {VIOLATION, tx} case prints both the found confirmation and the loud error line — verdict-to-text only, zero domain logic (invariant 3)"

patterns-established:
  - "Tri-state lookup composition: probe the VALUE through every found-source before ANY marker read (Pitfall 8; the Phase-16 one-batch value-XOR-marker invariant makes the order safe), and let the marker consult refine — never overwrite — a found verdict"

requirements-completed: [SURV-02] # peer half (refusal/failover) closed by 19-07; the local half here completes it

# Metrics
duration: 105min
completed: 2026-10-23
---

# Phase 19 Plan 05: Tri-State Tx-Lookup (D-03) Summary

**TransactionManager now answers a local tx lookup tri-state — FOUND from either found-source (tracked map, then exact-hash CRDT probe), PRUNED with the durable PrunedTransactionRecord naming the erased path, NOT_FOUND for unknown hashes — probed value-first-then-marker so a present value always outranks a marker, with the value-plus-marker invariant violation carried loudly on the result while the found tx is still returned; surfaced through a one-line GeniusNode passthrough and a `tx <hash>` CLI command whose "history pruned" answer is distinct from not-found, proven by a five-case suite on a real CRDT stack.**

## Performance

- **Duration:** 105 min (22:17-00:02 UTC)
- **Tasks:** 3
- **Files:** 7 (1 created, 6 modified)

## Accomplishments
- `TransactionLookupResult` (TM.hpp, namespace level) + `LookupTransaction` (public): reuses `PrunedLookup` verbatim — no second enum, no hand-built marker keys (T-19-0504); ordering in source order is value-probes-then-marker (Pitfall 8 / T-19-0502); hard errors from either found-source or the marker lookup propagate via `outcome::failure` — never downgraded to NOT_FOUND (T-19-0501)
- Found path consults the marker ONLY to detect the invariant violation (ERROR log); the violation is also carried on the verdict so callers see it deterministically
- `GeniusNode::LookupTransaction`: exact `GetTransactionStatus` shape (not-ready gate mirrors the sibling, `std::errc::not_connected` failure; one-line delegation; Doxygen documents the tri-state)
- `cmd_tx` in NodeExample: `check_arg_count(args, 2, "tx <tx_hash>")` + `args[1]` idiom (cmd_ds shape), COMMANDS registration, help listing; "history pruned" string present (2 occurrences); zero RPC/transport changes (git diff confirms 3 files only)
- TxLookupTriStateTest (new target, tx_pruner_test registration block copied verbatim with renames): FoundTracked / FoundCrdt (fresh manager over the live DB with the tracked-empty negative proof) / Pruned (record payload names the erased path) / NotFound / InvariantViolationLoud (SweepReprunes resurrect recipe; VIOLATION verdict + found tx asserted)
- Verification: suite 5/5 twice + ctest green; tx_pruner_test 27/27; transaction_manager_pending_lifecycle_test 22/22; node_example builds; format gate clean on all changed files (git-clang-format changed-lines); clang-tidy on new lines shows only the file-family accepted idiom classes

## Task Commits

Each task was committed atomically:

1. **Task 1: TransactionManager tri-state lookup API** - `5c7d4dbf8` (feat)
2. **Task 2: GeniusNode passthrough + NodeExample `tx` command** - `b93b3accc` (feat)
3. **Task 3: TriState suite (new target)** - `c5d531183` (test)

## Files Created/Modified
- `src/transaction/TransactionManager.hpp` — PrunedLookup opaque forward decl, TransactionLookupResult struct, public LookupTransaction declaration (all Doxygen'd)
- `src/transaction/TransactionManager.cpp` — LookupTransaction implementation after FetchExactTransactionFromCRDT (both found-sources defined above it)
- `src/account/GeniusNode.hpp` / `.cpp` — passthrough declaration + impl (GetTransactionStatus neighbor)
- `example/node_test/NodeExample.cpp` — cmd_tx + COMMANDS entry + help line
- `test/src/account/tx_lookup_tri_state_test.cpp` — new, 326 lines: fixture stack + seeding helpers (real-topic Put + map entry + GATE-03 head-retirement driver put) + 5 cases
- `test/src/account/CMakeLists.txt` — tx_lookup_tri_state_test registration (tx_pruner_test block copy; same genius_node_test/json_secure_storage/base_crdt_test link set + platform whole-archive options)

## Decisions Made
- See key-decisions: result-carried violation encoding; opaque-enum header hygiene; public tracked-map discriminator; CLI tx-first mapping
- PRUNED path logs INFO naming the hash ("pruned locally - history pruned") — the distinct answer is a normal state, not an anomaly; violations log at ERROR
- Marker-consult failure on a FOUND value stays FOUND (WARN): the value already authoritatively decided the verdict; only the violation report is degraded, never the payload

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Including PruneEligibility.hpp in TransactionManager.hpp breaks unrelated consumers**
- **Found during:** Task 1 first build (migration library failed: 'rapidjson/document.h' file not found)
- **Issue:** the plan's struct definition needs the PrunedLookup definition, but PruneEligibility.hpp's rapidjson include is not on every TM.hpp consumer's include path — the header would silently widen TM's dependency surface
- **Fix:** opaque `enum class PrunedLookup : uint8_t;` forward declaration in TM.hpp; verdict has no default initializer (the single construction site always sets it, documented on the member)
- **Files modified:** src/transaction/TransactionManager.hpp
- **Verification:** full genius_transaction_objs + dependents build clean
- **Committed in:** `5c7d4dbf8`

**2. [Rule 3 - Blocking] The plan's verify command names a nonexistent build target**
- **Found during:** Task 2 verify
- **Issue:** `--target node_test` — no rule exists; the example's target is `node_example` (directory node_test, target node_example)
- **Fix:** built `node_example` (the same artifact the plan means); all Task 2 criteria verified against it
- **Files modified:** none (command drift only)
- **Committed in:** n/a

**3. [Rule 3 - Blocking] The pruned arm's first erase refused: the seeded delta was still the topic head (GATE-03)**
- **Found during:** Task 3 first run (Pruned + InvariantViolation cases failed on EraseTransactionEntryPruned)
- **Issue:** a fresh tx Put makes its delta the topic HEAD, and EraseEntry's all-topic head scan refuses head deltas — the 19-04 suite hit the same guard and retired heads with plain driver puts
- **Fix:** SeedTransaction follows every seed with a plain driver put on the same topic (unique keys, double-run safe); the raw map entry still names the TX's own delta CID
- **Files modified:** test/src/account/tx_lookup_tri_state_test.cpp
- **Verification:** both erase-driven cases green from the next run
- **Committed in:** `c5d531183`

**4. [Rule 1 - Bug] Wrong record-payload expectation: the production marker never sets cid**
- **Found during:** Task 3 second run (record->cid() empty)
- **Issue:** the PRUNED assertion expected a non-empty cid, but BuildPruneMarkerEntry (the one record home) sets tx_path ONLY — the 17-designed production shape
- **Fix:** assert the actual durable payload (record->tx_path() == the erased tx path), with a comment naming the contract
- **Files modified:** test/src/account/tx_lookup_tri_state_test.cpp
- **Verification:** Pruned case green; no assertion weakened beyond the contract's actual payload
- **Committed in:** `c5d531183`

**5. [Rule 3 - Blocking] The plan's tracked-map test observable is private**
- **Found during:** Task 3 first build ('GetTransactionByHash' is a private member)
- **Issue:** the plan's seeding recipe implied asserting tracked-map state via GetTransactionByHash, which is private (no seam in this binary)
- **Fix:** GetTransactionStatusByTxId — public, scans exactly tx_processed_m, INVALID == not tracked (verified in source) — as both the tracked-present and tracked-empty proofs; no friend seam needed
- **Files modified:** test/src/account/tx_lookup_tri_state_test.cpp
- **Verification:** clean build; both found-source cases keep their discriminating proofs
- **Committed in:** `c5d531183`

---

**Total deviations:** 5 auto-fixed (3 Rule 3 blocking, 1 Rule 3 command drift, 1 Rule 1 wrong expectation)
**Impact on plan:** All must_haves truths proven. Deviations 1/5 changed which observable/declaration shape the plan sketched; 3/4 are the family's known harness contracts; none changed what the production code proves.

## Issues Encountered
- clang-tidy on new lines: file-family accepted classes only (k-constant naming, constexpr char[] topics, array decay on TokenID::FromBytes — verbatim from tx_pruner_test, guarded-optional false positive on the ASSERT-covered record access, `txId` param naming mirroring the GetTransactionStatus sibling). `misc-const-correctness` on the new shared_lock was cleared (declared const). addtest targets disable clang-tidy by design; findings listed for the record
- The example CLI's switch handles the {VIOLATION, tx} encoding explicitly (found line + loud error line) — documented in cmd_tx's comment so future editors keep the mapping total

## Threat Mitigations (per plan threat_model)
- **T-19-0501** (pruned conflated with never-existed): distinct PRUNED verdict with record payload, asserted by the Pruned case; never guessed on violation — DONE
- **T-19-0502** (misorder race): value-probes precede the marker consult in source order; the InvariantViolationLoud case proves the found value outranks the marker — DONE
- **T-19-0503** (logic at the boundary): GeniusNode is one-line delegation + the sibling's gate; cmd_tx maps verdict-to-text only; git diff shows zero RPC/transport surface — DONE
- **T-19-0504** (duplicated vocabulary): PrunedLookup reused verbatim (opaque forward decl; no second enum; LookupPrunedEntry is the only marker read) — DONE
- **T-19-05SC** (supply chain): zero registry installs — DONE

## Known Stubs
None — every path is wired to real stores; no placeholder text or unwired data sources.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- SURV-02 is complete (local half here, peer half 19-07); the phase's remaining plan (19-06 restart matrix) can also use LookupTransaction for operator-facing pruned answers in diagnostics
- RPC/transport PRUNED mapping remains deferred per D-03's deferred list — the verdict vocabulary is ready at the boundary when that lands

## Self-Check: PASSED

All 7 files exist (6 modified + 1 created); all 3 task commits (5c7d4dbf8, b93b3accc, c5d531183) present in git log; `TransactionLookupResult` in TM.hpp/.cpp (grep 3/3); "history pruned" present in NodeExample.cpp (grep 2); tx_lookup_tri_state_test 5/5 twice + ctest green; tx_pruner_test 27/27; lifecycle binary 22/22; no RPC files in any task diff.

---
*Phase: 19-pruned-node-survival-recovery-hardening*
*Completed: 2026-10-23*
