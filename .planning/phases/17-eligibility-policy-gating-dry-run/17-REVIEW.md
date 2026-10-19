---
phase: 17-eligibility-policy-gating-dry-run
reviewed: 2026-10-19T00:00:00Z
depth: standard
files_reviewed: 17
files_reviewed_list:
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/UTXOManager.cpp
  - src/account/UTXOManager.hpp
  - src/account/proto/SGTransaction.proto
  - src/crdt/crdt_datastore.hpp
  - src/crdt/globaldb/globaldb.cpp
  - src/crdt/globaldb/globaldb.hpp
  - src/crdt/impl/crdt_datastore.cpp
  - src/transaction/PruneEligibility.cpp
  - src/transaction/PruneEligibility.hpp
  - src/transaction/TransactionManager.cpp
  - src/transaction/TransactionManager.hpp
  - test/src/account/prune_eligibility_test.cpp
  - test/src/account/transaction_manager_certificate_fallback_test.cpp
  - test/src/account/utxo_manager_test.cpp
  - test/src/crdt/crdt_priority_delta_keys_test.cpp
findings:
  critical: 1
  warning: 5
  info: 4
  total: 10
status: issues_found
---

# Phase 17: Code Review Report

**Reviewed:** 2026-10-19
**Depth:** standard
**Files Reviewed:** 17
**Status:** issues_found

## Summary

Reviewed the Phase 17 diff (`2086039f7^..HEAD`): the `spent_by_txid` carrier +
`GetOutPointSpender`, the CRDT read exposures (`GetPriority`/`GetKeyPriority`,
`GetLocalDeltaKeys`), the cert-slot `previous_hash` relaxation, the
`PruneEligibility` evaluator (window arithmetic, block gate, pruned-marker
contract), and the GeniusNode config + dry-run timer wiring.

The core evaluator is well built: the window arithmetic is scale-consistent
(both `maxHeight` from heads and `spend_height` from `GetKeyPriority` are the
same `delta.priority()` scale — verified against `CrdtSet::SetPriority`
storing `P+1` and `GetPriority` returning `P`), every failure mode fails
closed with a structured reason, `GetLocalDeltaKeys` is verified strictly
local (`GetNodeWithoutRequest` → MerkleDAG/LRU only), the revert path clears
the spender carrier symmetrically, and the aggregate-init order of the
`UTXOEntry` brace initializers matches the struct. Test coverage is genuinely
adversarial (exact-set window discriminators, tombstone blocks, invariant
violation, cert-rung/CRDT-scan equivalence).

One concurrency defect in the new timer wiring is shipping-blocking, and
several fail-closed gaps deserve fixes before Phase 18 builds on this
evaluator verbatim.

## Critical Issues

### CR-01: Dry-run cycle reads replaceable members without the lifecycle lock (data race / UB)

**File:** `src/account/GeniusNode.cpp:4919-4930`
**Issue:** `RunPruneDryRunCycle` runs on an `io_context` **thread pool**
(`io_threads_`, `GeniusNode.cpp:380-384`) and reads `tx_globaldb_`,
`transaction_manager_`, and `account_` unlocked, then dereferences them and
retains `account_->GetUTXOManager()` by reference for the whole evaluation.
Those exact members are reassigned under `lifecycle_mutex_` elsewhere —
`transaction_manager_` at `GeniusNode.cpp:1138` (StateTransition),
`tx_globaldb_` at `:2164`, `account_` at `:3065/:3079` (SelectAccount). An
unsynchronized `shared_ptr` read concurrent with its assignment is a data
race (undefined behavior — torn control-block read can crash the node), not
merely "a member momentarily unset" as the comment assumes. The file already
has the correct tool: `SnapshotAccountServices()` (`GeniusNode.cpp`,
`GeniusNode.hpp:1022`) exists precisely to make these reads consistent, and
the state-change callback at `:1180-1196` uses it.
**Fix:**
```cpp
void GeniusNode::RunPruneDryRunCycle()
{
    const auto snapshot = SnapshotAccountServices();
    if ( !snapshot.account || !snapshot.manager || !tx_globaldb_ )
    {
        node_logger_->debug( "Prune dry-run cycle skipped: account-bound members unavailable" );
    }
    else
    {
        // Hold the snapshot shared_ptrs for the whole cycle so the account
        // (and its UTXOManager) outlives the evaluator's references.
        auto evaluator = PruneEligibility::New( *tx_globaldb_,
                                                *snapshot.manager,
                                                snapshot.account->GetUTXOManager(),
                                                node_type_,
                                                pruning_config_ );
        ...
```
(`tx_globaldb_` is node-scoped rather than per-generation; if it is also
swapped concurrently in your wiring, snapshot it under `lifecycle_mutex_` in
the same call.)

## Warnings

### WR-01: Unchecked `.value()` on `fromReadableString(GetHash())` at four new call sites

**File:** `src/transaction/TransactionManager.cpp:2644, 2667, 2715, 2886`
**Issue:** `base::Hash256::fromReadableString( tx->GetHash() ).value()` fails
whenever `GetHash()` (raw `dag_st.data_hash()`) is not exactly 64 hex chars —
e.g. the migration path seeds `data_hash("")`
(`src/migration/Migration3_4_0To3_5_0.cpp:366`), and `fromReadableString`
also throws `std::invalid_argument` from `std::stoul` on non-hex input
(`src/base/blob.hpp:103-118`). `.value()` on an errored result throws through
the CRDT/consensus processing stack instead of returning an outcome failure —
contrary to the project error contract. The phase followed the pre-existing
unsafe pattern (`:1293`, `:2679`, `:2748`, `:2952`) but added four more
copies; the safe pattern already exists in this file at `:947`.
**Fix:**
```cpp
auto txid = base::Hash256::fromReadableString( transfer_tx->GetHash() );
if ( txid.has_error() )
{
    m_logger->error( "Transfer {} has malformed data_hash", transfer_tx->GetHash() );
    return outcome::failure( txid.error() );
}
BOOST_OUTCOME_TRY( utxo_mgr.ConsumeUTXOs( inputs, TransferInputOwner( *transfer_tx ), UTXOType::UTXO_NORMAL, txid.value() ) );
```
(apply the same shape at 2667, 2715, 2886; consider a small
`HashFromTxHash(const std::string&)` helper to stop the duplication).

### WR-02: A transient evaluation error permanently kills the dry-run timer

**File:** `src/account/GeniusNode.cpp:4940-4948` (with `src/transaction/PruneEligibility.cpp:221-227`)
**Issue:** `Evaluate()` returns failure not only for the permanent config
error (missing window — unreachable here, since `enabled` requires a window at
parse) but also for transient datastore errors: `GetCRDTHeadList()` fails
with `operation_canceled` whenever `ActiveCRDTDataStore()` momentarily
returns null (e.g. during a GlobalDB restart/generation switch — the
member-null guard at `:4919` does not cover this, since `tx_globaldb_` itself
is non-null). The cycle then `return`s without rescheduling, permanently
stopping dry-run reporting for the node's remaining uptime. That contradicts
the evaluator's own "query failures are logged and the scan continues (never
aborts)" discipline applied one level down.
**Fix:** Only stop for the permanent error; skip-and-reschedule otherwise:
```cpp
if ( evaluation.has_error() )
{
    if ( evaluation.error() == std::errc::invalid_argument )
    {
        node_logger_->error( "... broken policy state — dry-run cycle stopped" );
        return;
    }
    node_logger_->warn( "... transient evaluation failure — retrying next cycle" );
}
```

### WR-03: `finality_window: 0` accepted while `enabled: true`

**File:** `src/transaction/PruneEligibility.cpp:81-92`
**Issue:** `IsUint64()` + `GetUint64()` accepts `0`. With N=0, the window
predicate `max_height − spend_height ≥ 0` is vacuously true for every
recorded spend, so every genuinely-spent transaction becomes eligible
immediately. The fail-closed contract ("N is required when enabled; there is
no default window") is defeated by the degenerate value, and Phase 18 will
reuse this evaluator verbatim for real erasure.
**Fix:**
```cpp
if ( pruning_object.HasMember( "finality_window" ) && pruning_object["finality_window"].IsUint64()
     && pruning_object["finality_window"].GetUint64() > 0 )
{
    config.finality_window = pruning_object["finality_window"].GetUint64();
}
// existing enabled-without-window fail-close then also covers explicit 0
```
(and extend the `disabled_reason` message to name `finality_window > 0`).

### WR-04: Pruned-marker lookup trusts `record.tx_path()` with no consistency check

**File:** `src/transaction/PruneEligibility.cpp:165-194`
**Issue:** `LookupPrunedEntry` uses the marker's stored `tx_path()` for the
value-presence query without validating it against the requested `tx_hash`.
Consequences: (a) a corrupt marker whose `tx_path` points at any absent path
yields `PRUNED` — and in `EvaluateCandidate` grants
`SPENDER_PRUNED_WINDOW_SATISFIED` (window satisfied) — silently, violating
the module's own "state bug is reported loudly, never guessed away" contract
(the invariant-violation branch catches marker∧value, not marker∧wrong-path);
(b) an empty payload parses as a valid proto with `tx_path == ""`, and
`QueryKeyValues("")` then prefix-scans the entire store.
**Fix:** Validate before querying:
```cpp
if ( record.tx_path().empty()
     || record.tx_path().size() < tx_hash.size()
     || record.tx_path().compare( record.tx_path().size() - tx_hash.size(), tx_hash.size(), tx_hash ) != 0 )
{
    MarkerLogger()->error( "Pruned marker for tx {} carries mismatched path {}", tx_hash, record.tx_path() );
    return outcome::failure( std::errc::bad_message );
}
```

### WR-05: Eligible candidate with unresolved CID silently skips the block gate

**File:** `src/transaction/PruneEligibility.cpp:270-274`
**Issue:** In `Evaluate()`, an eligible candidate whose `cid` is `nullopt`
(`GetTransactionCID` failed — possible in exactly the asymmetric
partial-erase state the codebase guards against elsewhere) is `continue`d out
of the block-gate loop: no `BlockGateResult`, no reason anywhere in the
report. That contradicts the stated repudiability discipline ("nothing is
silently dropped") and leaves the Phase-18 consumer an eligible work-list
entry with no block verdict to consult before deleting.
**Fix:** Emit a fail-closed block report (or a candidate-level reason):
```cpp
if ( !candidate_cid.has_value() )
{
    BlockGateResult ungated;
    ungated.cid    = "<unresolved for " + candidate.tx_path + ">";
    ungated.erasable = false;
    RecordReason( ungated, PruneReason::SPENDER_UNRESOLVED );
    result.block_reports.push_back( ungated );
    continue;
}
```

## Info

### IN-01: Config log prints a misleading `finality_window=0` when none is set

**File:** `src/account/GeniusNode.cpp:484-488`
**Issue:** `pruning_config_.finality_window.value_or( 0 )` logs
`finality_window=0` for a config that has no window (disabled), which reads
as a configured zero window to an operator scanning logs (and interacts
confusingly with WR-03).
**Fix:** Log presence, not a fabricated number (e.g. a separate line or
`"not set"` when `!has_value()`).

### IN-02: Duplicate per-cycle INFO summaries

**File:** `src/transaction/PruneEligibility.cpp:293-297` and `src/account/GeniusNode.cpp:4956-4963`
**Issue:** `Evaluate()` already logs "Prune evaluation: X candidates, Y
eligible, Z/W blocks erasable" every cycle; `RunPruneDryRunCycle` then logs a
second INFO with the same numbers in a different format. Two homes for one
report (DRY) and noisy logs.
**Fix:** Keep exactly one — preferably the node-side dry-run summary, and
drop the evaluator's (or gate the evaluator's behind `debug`).

### IN-03: Whole-store evaluation runs synchronously on an io-pool thread

**File:** `src/account/GeniusNode.cpp:4926-4963` (timer callback)
**Issue:** `Evaluate()` deserializes every stored transaction of every
monitored network on one `io_context` pool thread every 10 minutes. On a
large store this stalls other io work on that thread for the duration. It
matches the `RunResultGC` precedent, so this is a scaling note for the
Phase-18 reuse (consider posting to a worker/strand when the store grows).

### IN-04: Sibling GlobalDB additions return different errors for the same failure

**File:** `src/crdt/globaldb/globaldb.cpp:902-907` vs `:1015-1023`
**Issue:** `GetKeyPriority` returns `std::errc::operation_canceled` when no
active CRDT datastore, while `GetLocalDeltaKeys` (added in the same phase,
same file) returns `Error::CRDT_DATASTORE_NOT_CREATED` for the identical
condition — each copied a different pre-existing sibling. Callers cannot
pattern-match one way.
**Fix:** Pick one code for "no active CRDT datastore" in GlobalDB facades
(new code should prefer the typed `Error::CRDT_DATASTORE_NOT_CREATED`).

---

_Reviewed: 2026-10-19_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
