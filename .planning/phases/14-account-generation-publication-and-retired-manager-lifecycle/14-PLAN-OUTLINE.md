# Phase 14 Plan Outline

**Phase:** Account-generation publication and retired-manager lifecycle safety

**Goal:** Publish only complete ready account generations, close old-manager admission at accepted selection, preserve already-admitted terminal work, and make failed or retired generations explicitly unavailable and permanently non-mutable.

## Serialized Plan Graph

| Plan | Wave | Depends on | Objective |
|------|------|------------|-----------|
| 14-01 | 1 | — | Manager admission, terminal ledger, retirement core, exact caller inventory. |
| 14-02 | 2 | 14-01 | Node ready/retiring/pending lifecycle core, events, status, node caller inventory. |
| 14-03 | 3 | 14-02 | Production TransactionManager callers. |
| 14-04 | 4 | 14-03 | Manager regression-fixture callers. |
| 14-05 | 5 | 14-04 | Final manager callers and manager shim removal. |
| 14-06 | 6 | 14-05 | Production/startup GeniusNode callers only. |
| 14-07 | 7 | 14-06 | Account/blockchain fixture GeniusNode callers only. |
| 14-08 | 8 | 14-07 | Processing/transaction-sync GeniusNode callers only. |
| 14-09 | 9 | 14-08 | Multi-account GeniusNode callers only. |
| 14-10 | 10 | 14-09 | Bridge common/E2E GeniusNode callers only. |
| 14-11 | 11 | 14-10 | Bridge-race translation units and shared fixture only. |
| 14-12 | 12 | 14-11 | Node-example and processing-multi GeniusNode callers only. |
| 14-13 | 13 | 14-12 | Final node shim removal and complete node caller-union build. |
| 14-14 | 14 | 14-13 | Deterministic timeout, cleanup-first failure, old terminal attribution, and one drain-gated explicit recovery. |
| 14-15 | 15 | 14-14 | Read-only exact final manager/node caller-union and lifecycle closure gate. |

## Split Rationale and Deterministic Barriers

The previous 14-06/07/08 migration plans exceeded the approved file/task budgets. The revised tail separates production/startup from account/blockchain fixtures; processing/transaction-sync from multi-account; bridge E2E from bridge-race; and the omitted node-example/processing-multi callers into `14-12`. `14-13` is the sole node-shim removal cut and depends on every caller group, including `node_example` and `processing_multi_test`. `14-14` follows that cut and implements the resolved timeout rule: an explicit recovery may be accepted, but starts no initialization until true old-generation drain. `14-15` is read-only and cannot repair callers.

All plans are strictly serialized because the shared manifest and API contract are mutable. Every plan has at most nine `files_modified`; every task has at most eight files. Same-wave file conflicts therefore do not occur.

## Mechanical Caller Partitions

### TransactionManager manifest

`14-manager-caller-inventory.tsv` remains complete and is consumed by 14-03/04/05; 14-05 removes every manager shim. Its target union enters the 14-15 read-only final gate.

### GeniusNode manifest

`14-02-CALLER-INVENTORY.tsv` assigns every account-bound API expression and its real CMake owner to exactly one tail plan:

- **14-06:** `GeniusNode.cpp`, `node_initialization_progress.cpp` → `genius_node`, `genius_node_test`, `node_initialization_progress`.
- **14-07:** account management/config/type plus node startup/genesis fixtures → five named targets.
- **14-08:** processing plus transaction-sync/migration-sync → seven named targets, including `migration_sync_test`.
- **14-09:** `multi_account_sync.cpp`, `policy_lifetime_multi_account_test.cpp` → `multi_account_test`, `policy_lifetime_multi_account_test`.
- **14-10:** five bridge E2E units → five named E2E targets.
- **14-11:** five bridge-race units plus `bridge_race_fixture.hpp`; shared-header rows expand to all five race targets.
- **14-12:** `example/node_test/NodeExample.cpp` → `node_example`; `test/src/processing_multi/processing_multi_test.cpp` → `processing_multi_test`.

## Multi-Source Coverage Audit

| SOURCE | ID | Requirement / constraint | Coverage | Status |
|--------|----|--------------------------|----------|--------|
| GOAL | Phase 14 | Complete publication, admission/drain, unavailable failure, permanent retirement | 14-01..14-15 | COVERED |
| REQ | D-01..D-04 | Accepted generation, events, switching errors, no overlap | 14-02, 14-06..14-15 | COVERED |
| REQ | D-05..D-08 | Admission, terminal drain, no overlap, bounded non-cancelling timeout | 14-01..14-05, 14-14, 14-15 | COVERED |
| REQ | D-09..D-12 | Cleanup-first failure, unavailable identity, explicit recovery | 14-02, 14-06..14-15 | COVERED |
| REQ | D-13..D-16 | Retired denial, immutable diagnostics, old attribution, processing status | 14-01..14-05, 14-06..14-15 | COVERED |
| RESEARCH | Caller breadth and final surfaces | Independently regenerated manager/node unions and builds, including node_example and processing_multi_test | 14-01..14-13, 14-15 | COVERED |
| RESEARCH | Timeout recovery | One explicit target-only recovery waits for true drain | 14-14, 14-15 | COVERED |
| CONTEXT | Deferred bridge/trust/repository authority work | No ownership redesign or universal cancellation | NONE | EXCLUDED |

No source item is unplanned.
