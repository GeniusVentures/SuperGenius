# Phase 16: Erasure Primitives & Bookkeeping Hardening - Context

**Gathered:** 2026-10-15
**Status:** Ready for planning

<domain>
## Phase Boundary

The local two-level erasure primitives — CRDT set entries + raw CID block + tx→CID map entry erased in one atomic RocksDB `WriteBatch` — exposed behind owner-module APIs with the safety invariants baked into their shape (seal markers unaddressable, in-use CIDs guarded, no tombstone path reachable). Fresh-DB test-fixture rules established from the first erasure test onward. **Zero production behavior change**: no production caller exists at phase end; `removeNode`'s first consumer is the test suite.

Covers requirements PRUNE-02 and GATE-03 only. Eligibility policy, dry-run evaluator, and the pruner service are Phase 17/18 scope.

</domain>

<decisions>
## Implementation Decisions

### Locked carry-forwards from milestone v1.2 definition (do not re-litigate)
- **D-C1:** One shared RocksDB behind two facades (CRDT datastore + ipfs-lite block store, `src/crdt/globaldb/globaldb.cpp:322-329`); atomicity = one `WriteBatch`
- **D-C2:** Physical local RocksDB delete only — the CRDT tombstone path (`DeleteKey`/`DeleteTransaction`) is forbidden for pruning (owner directive: "RocksDB deletes should be physical deletion, not writing tombstones")
- **D-C3:** Seal markers (`<cid>sealed`) are never erased — they are the traversal terminator (GATE-03); reuse the dormant `removeNode` chain; zero vendored ipfs-lite-cpp changes
- **D-C4:** The existing tx→CID map (`StoreTransactionCID`/`GetTransactionCID`, `src/transaction/TransactionManager.cpp:4322-4345, 4433-4458`) is extended with a deletion lifecycle, not replaced
- **D-C5:** In-use guards enumerated: head CID, root-CID pending/active, LRU-cached CIDs; `isResolved` asserted before and after erase
- **D-C6:** Test rules: fresh temp DB, three-part assertions (CRDT entries absent, raw block absent, seal marker still present) plus a negative control (untouched sibling key byte-identical)

### Erase-API composition (discussed)
- **D-01:** The atomic two-level erase surfaces as ONE narrow, generic API on the GlobalDB facade — key + CID in, single `WriteBatch` internally, guards inside. The API stays generic (no transaction semantics leak into the CRDT layer). Rationale: wrong deletion becomes impossible by API shape, and Phase 18's `TxPruner` stays a thin caller. Owner-module key-layout knowledge (`CrdtSet` owns `/s /t /k/v /k/p`; `GraphsyncDAGSyncer` owns block storage; `TransactionManager` owns the map) stays inside those modules — GlobalDB composes them into one batch.

### Map deletion lifecycle (discussed)
- **D-02:** Phase 16 builds PLAIN map-entry deletion only. The pruned-marker record (backing "history pruned" answers) is designed in Phase 17 together with the PRUNED-vs-NOT_FOUND error surface — research indicates the marker likely belongs under the pruner's own node-local `/prune/` namespace (following the `CRDTWorkJournal` pattern), not in the map's keyspace. Building it now risks placing it in the wrong layer before that design settles.

### Crash-consistency proof depth (discussed)
- **D-03:** Phase 16's crash proof is unit-level: `WriteBatch`-atomicity tests on fresh temp DBs plus failure-injection points at primitive boundaries (crash *between* steps simulated). Real process-kill → fixed-point convergence tests belong to Phase 18's service criterion, where the four-step erase flow actually exists.

### Claude's Discretion
- Test-file placement and fixture naming (repo conventions: gtest, PascalCase fixtures, `*_test.cpp`)
- Exact method signatures/parameter shapes for the GlobalDB erase API (planner/refactor stage)
- `maxHeight` exposure placement (needed by Phase 17's evaluator; may land here or in 17 if unused in 16)

### Folded Todos
None — the single SDK todo match (`bridge-race-not-all-11-mint-within-window`, score 0.6) was stopword noise, not a scope match.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone research (file:line-verified against this repo)
- `.planning/research/SUMMARY.md` — converged architecture story, erase-flow ordering, resolved researcher disagreements, phase implications
- `.planning/research/ARCHITECTURE.md` — integration points with citations: one-DB topology, map location, seal-marker mechanics, erase primitives, restart hazards
- `.planning/research/STACK.md` — RocksDB 9.4 / vendored ipfs-lite-cpp API surface, WriteBatch/compaction semantics, `/prune/` prefix precedent
- `.planning/research/PITFALLS.md` — full pitfall catalog (fetch-resurrect, multi-element blocks, fixture rules) with prevention strategies

### Requirements & roadmap
- `.planning/REQUIREMENTS.md` — PRUNE-02 (atomic two-level physical erasure), GATE-03 (seal markers never erased)
- `.planning/ROADMAP.md` — Phase 16 entry: goal, dependencies, 5 success criteria

### Codebase conventions
- `.planning/codebase/ARCHITECTURE.md` — layering (account/tx → CRDT/GlobalDB → storage), component responsibilities
- `.planning/codebase/CONVENTIONS.md` — naming, `outcome::result<T>` error handling, test conventions, include order

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `DAGSyncer::removeNode(CID)` chain (`src/crdt/impl/graphsync_dagsyncer.cpp:461-464` → `MerkleDagServiceImpl` → vendored `RocksdbDatastore::remove` → `db->Delete`) — the raw-block erasure primitive, zero production callers today
- `StoreTransactionCID` / `GetTransactionCID` (`src/transaction/TransactionManager.cpp:4322-4345, 4433-4458`) — the existing forward tx→CID map to extend with deletion
- `CRDTWorkJournal` (`/crdt/work/`) — precedent for node-local direct-to-shared-DB writes outside the broadcast path
- `GetLocalDeltaKeyValues` (`src/crdt/impl/crdt_datastore.cpp:2280-2302`) — block-parse pattern (`GetNodeWithoutRequest` + `GetDeltaFromNode`) Phase 17's eligibility gate will reuse
- Both storage wrappers expose `getDB()` → raw RocksDB `DB*`, so `WriteBatch` composition needs zero wrapper changes

### Established Patterns
- `outcome::result<T>` with `enum class` errors — the new erase API returns results, no exceptions
- Static `New()` factories returning `std::shared_ptr`; interface/impl separation (`impl/` subdirs)
- gtest with PascalCase fixtures; test targets excluded from clang-tidy via `disable_clang_tidy()`; fresh temp RocksDB dirs per fixture (v1.1-era rule)
- Key layouts owned by their modules: `CrdtSet` owns `/crdt/s|t/<key>/<cid>`, `/crdt/k/<key>/v|p` (`src/crdt/impl/crdt_set.cpp:462-496`); no other module constructs these keys

### Integration Points
- `GlobalDB` facade (`src/crdt/globaldb/globaldb.hpp`) — where the new single-entry erase API surfaces (D-01)
- `CrdtSet` local-erase primitive — the CRDT-entry half of the batch
- `GraphsyncDAGSyncer` guarded block-erase exposure — the block half (guards: `IsHead`, `IsRootCIDPendingOrActive`, LRU cache membership)
- Vendored seal-marker key format (`<cid>sealed`, `thirdparty/ipfs-lite-cpp` `datastore_rocksdb.cpp`) — must remain unaddressable by construction

</code_context>

<specifics>
## Specific Ideas

- Owner's exact words on erasure semantics: "RocksDB deletes should be physical deletion and not writing tombstones. That is what CRDT does." — the CRDT path is what we are explicitly NOT using
- API-shape principle adopted from the roadmap rationale: "wrong deletion impossible by API shape"

</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)
- `bridge-race-not-all-11-mint-within-window` (P1) — bridge-mint-race scope; SDK matched on stopword overlap only; remains in pending for Track A bridge work

None otherwise — discussion stayed within phase scope.

</deferred>

---

*Phase: 16-Erasure Primitives & Bookkeeping Hardening*
*Context gathered: 2026-10-15*
