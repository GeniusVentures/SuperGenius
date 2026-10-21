---
phase: 18-txpruner-service-first-real-prune
reviewed: 2026-10-21T23:09:00Z
depth: standard
files_reviewed: 28
files_reviewed_list:
  - src/account/DevMintInputValidator.cpp
  - src/account/DevMintInputValidator.hpp
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/PublicChainInputValidator.cpp
  - src/account/UTXOManager.cpp
  - src/account/UTXOManager.hpp
  - src/base/DevMintPolicy.hpp
  - src/crdt/crdt_datastore.hpp
  - src/crdt/crdt_set.hpp
  - src/crdt/globaldb/globaldb.cpp
  - src/crdt/globaldb/globaldb.hpp
  - src/crdt/graphsync_dagsyncer.hpp
  - src/crdt/impl/crdt_datastore.cpp
  - src/crdt/impl/crdt_set.cpp
  - src/crdt/impl/graphsync_dagsyncer.cpp
  - src/transaction/PruneEligibility.cpp
  - src/transaction/PruneEligibility.hpp
  - src/transaction/TransactionManager.cpp
  - src/transaction/TransactionManager.hpp
  - src/transaction/TxPruner.cpp
  - src/transaction/TxPruner.hpp
  - test/src/account/dev_mint_policy_test.cpp
  - test/src/account/prune_eligibility_test.cpp
  - test/src/account/tx_pruner_test.cpp
  - test/src/crdt/crdt_erase_test.cpp
  - test/src/crdt/globaldb_erase_test.cpp
findings:
  critical: 1
  warning: 3
  info: 5
  total: 9
status: issues_found
---

# Phase 18: Code Review Report

**Reviewed:** 2026-10-21T23:09:00Z
**Depth:** standard (diff base `706a142ad^`, cross-referenced into surrounding full-file context and callee implementations)
**Files Reviewed:** 28
**Status:** issues_found

## Summary

The phase diff (TxPruner service + real prune cycle, seal-based re-delivery guard, net-scoped dev-mint policy, owner-vended compose lock, marker batch-join, BLOCK_ALREADY_ERASED evidence, config keys, MintFunds burn-UTXO leak fix, test suites) is largely well-constructed: the DevMintPolicy predicate reads only `version::GetNetworkID()` (verified — no transaction/proposer field reaches it), both `GatherVerificationEvidence` bypass legs return default-invalid evidence (`RpcVerificationEvidence{}`, `valid=false`) on non-designated nets, the compose lock is the same recursive `CrdtSet::mutex_` that `PutElems` holds across build+commit (verified at crdt_set.cpp:770-807) with no AB-BA lock ordering found against heads/dag mutexes, and the BLOCK_ALREADY_ERASED soundness premise holds (production block-byte deletion is staged only through `StageBlockRemoval`; `DeleteCIDBlock` evicts only the in-memory LRU).

One BLOCKER was found in the MintFunds burn-UTXO leak fix: the rollback in the catch handler uses `transaction_hash` after it was moved from, so the cleanup silently no-ops on exactly the failure paths it was written to fix — and it is strictly worse there than the code it replaced (the old `RollbackUTXOs` treated an empty reservation id as "release any"; `RemoveBurnUTXOs` treats it as "held by another" and skips the entry entirely).

Verification notes: no build, clang-format, or clang-tidy run was performed in this review (read-only review environment); all findings are from source tracing.

## Critical Issues

### CR-01: Use-after-move of `transaction_hash` makes the MintFunds catch-path rollback a silent no-op (and blocks re-delivery permanently)

**File:** `src/transaction/TransactionManager.cpp:1045` (the move) and `:1058` (the moved-from use)
**Issue:** Inside the `try`, the hash is moved out:

```cpp
MintTransactionV2::New( amount,
                        std::move( chainid ),
                        tokenid,
                        FillDAGStruct( std::move( transaction_hash ) ),  // <-- parameter is std::optional<std::string> BY VALUE: the string is moved here
                        std::move( mint_inputs ),
                        destination )
```

`FillDAGStruct`'s parameter is `std::optional<std::string>` (TransactionManager.hpp:534), so the argument conversion move-constructs the string; a 64-hex-char hash is heap-backed, so `transaction_hash` is left empty. Every exception reaching the catch (from `MintTransactionV2::New`, `MakeSignature`, or `EnqueueTransaction` — the throw sites the handler exists for) therefore sees an empty string, and the handler does:

```cpp
if ( auto removal = account_m->GetUTXOManager().RemoveBurnUTXOs( rollback_inputs, transaction_hash ); ...
```

The burn outpoint is `UTXO_RESERVED` under the real burn hash (claimed at TransactionManager.cpp:1019-1023 before the try). In `RemoveBurnUTXOs` (UTXOManager.cpp:700-706), a `RESERVED` entry whose recorded holder differs from the passed id is `held_by_other` and skipped. With the moved-from empty id, the mismatch is guaranteed, so the burn `UTXO_BRIDGE` entry survives in memory and on disk. Consequences: (a) the 2026-10-20 dangling-entry incident this change was made to fix recurs on every post-move throw; (b) it is worse than the old code — `RollbackUTXOs` treated `reservation_id.empty()` as "release any" (UTXOManager.cpp:669), releasing the entry to READY (re-mintable), whereas the skipped RESERVED entry permanently answers `IsOutPointReserved == true`, so re-delivery of the burn is rejected with `already_connected` forever while no mint ever enqueued; (c) the error/log lines print an empty `tx_hash`, and the trailing "rolled back reservation" log is emitted even when nothing was rolled back.

**Fix:** capture the reservation id before the try block and use the copy in the handler:

```cpp
// before the try (beside rollback_inputs):
const std::string burn_reservation_id = transaction_hash;
...
catch ( const std::exception &e )
{
    if ( auto removal = account_m->GetUTXOManager().RemoveBurnUTXOs( rollback_inputs, burn_reservation_id );
         removal.has_error() )
    {
        m_logger->error( "{}: MintFunds rollback failed to remove burn UTXO for tx_hash={}: {}",
                         __func__, burn_reservation_id, removal.error().message() );
    }
    ...
}
```

(The FAILED-transition call site at TransactionManager.cpp:5095 is correct — `tx->dag_st.uncle_hash()` carries the pre-move copy; only the catch path is broken.)

## Warnings

### WR-01: `RemoveBurnUTXOs` rollback swallows repair-store failures silently — disk/memory divergence with no log

**File:** `src/account/UTXOManager.cpp:745-748`
**Issue:** When a per-owner `StoreUTXOsLocked` fails mid-loop, memory is restored from the entry-time copies and already-stored owners are "repaired":

```cpp
for ( const auto &repaired : stored_so_far )
{
    (void) StoreUTXOsLocked( repaired );
}
```

A failing repair leaves that owner's persisted snapshot in the post-removal state while memory is pre-removal, with no log line — the divergence is invisible until restart, when the "removed" burn entry reappears from disk (re-introducing the incident this function exists to close). This violates the project rule "Never silently swallow failures". (The same pattern exists at UTXOManager.cpp:357-359 in the pre-existing put path; the new code copies the defect into a cleanup path where divergence is costlier.)
**Fix:** log each failed repair explicitly, e.g.

```cpp
for ( const auto &repaired : stored_so_far )
{
    if ( auto re_store = StoreUTXOsLocked( repaired ); re_store.has_error() )
    {
        logger_->error( "RemoveBurnUTXOs rollback: owner {} snapshot repair failed: {} — persisted state may diverge",
                        repaired,
                        re_store.error().message() );
    }
}
```

### WR-02: Prune marker record names the local-network path even when the erase resolves under a different monitored network

**File:** `src/transaction/PruneEligibility.cpp:243` (`BuildPruneMarkerEntry`), consumed at `src/transaction/TransactionManager.cpp:4337` (`EraseTransactionEntryPruned`)
**Issue:** `EraseTransactionEntryWithMarker` deliberately resolves the entry under ANY monitored network — its own comment (TransactionManager.cpp:4298-4305) stresses that "the map spans every monitored network, so the entry can live under a non-local network's path", and the erase correctly uses `path_and_cid.first`. But the marker is built beforehand from `tx_hash` alone: `BuildPruneMarkerEntry` calls `TransactionManager::GetTransactionPath( tx_hash )`, whose single-arg overload uses `GetBlockChainBase()` = `GetBlockChainBase( version::GetNetworkID() )` (TransactionManager.cpp:2258-2261) — the node's own network. For a cross-network entry, the persisted `PrunedTransactionRecord.tx_path` names a key that never existed, and `LookupPrunedEntry`'s value-presence probe (PruneEligibility.cpp:205-217) then queries that wrong path: it is vacuously absent, so (a) the `VALUE_PRESENT_INVARIANT_VIOLATION` detector can never fire for such markers, and (b) the D-04 forensic record ("the tx path named by the hash") is inaccurate. The PRUNED verdict itself remains correct today only because the wrong-path value is always absent.
**Fix:** build the marker from the resolved path — e.g. an overload `BuildPruneMarkerEntry( const std::string &tx_hash, const std::string &resolved_tx_path )` with `record.set_tx_path( resolved_tx_path )`, called from `EraseTransactionEntryPruned` after `ResolveTransactionPathAndCID` (i.e., move marker construction into `EraseTransactionEntryWithMarker` using `path`).

### WR-03: The "first real cycle in 1 min" operator log is wrong — settle-window semantics defer the first erase to ~11 minutes

**File:** `src/account/GeniusNode.cpp:4956-4961` (log), interacting with `src/transaction/TxPruner.cpp:299-319` (`TickGateSettled`)
**Issue:** The first timer tick (t=1 min) reaches `TickGateSettled` with no prior `gate_held_since_`, which stamps the clock and refuses the cycle (TxPruner.cpp:312-315); the first cycle that can actually run is the second tick at t≈11 min (10-minute cadence). The INFO line states "first real cycle in 1 min, then every 10 min", so an operator verifying the prune feature 5 minutes after start sees no cycle and no status — a false-negative operational signal on the exact surface (RECL-03/RECL-04) this phase builds for observability.
**Fix:** either state the effective schedule in the log ("first real cycle after the first tick + {settle}s settle window (≈11 min at the default cadence)"), or have `RunPruneCycle`'s skip branch log at INFO once (the service already debug-logs the skip at TxPruner.cpp:95-97, which is below default visibility).

## Info

### IN-01: `DevMintInputValidator::Register()` doc contradicts the registry contract

**File:** `src/account/DevMintInputValidator.hpp:26-28`
**Issue:** "@return Always true; registration is insert-only..." — `IInputValidator::Register` (InputValidators.hpp:107-117) returns `false` when the chain id is already claimed or the validator is null. The function's return value is ignored at the call site (TransactionManager.cpp:76), so only the doc is wrong.
**Fix:** Change the doc to "True when 'dev' was unclaimed and this validator took it; false when another validator already owns the chain id."

### IN-02: `PruneCycleStats::generation_superseded` is never set by any production caller

**File:** `src/transaction/TxPruner.hpp:103-109`; wiring at `src/account/GeniusNode.cpp:5090-5120`
**Issue:** The field's contract says the wiring "checks before invoking the cycle and tags its own reporting", but `GeniusNode::RunPruneCycle` only performs the pre-cycle supersession check; on success it stores nothing and the returned stats always carry `false` (the test at tx_pruner_test.cpp:1251 pins this dead state). The dry-run arm tags its INFO line separately. The field is dead weight in the production status surface until a caller is added.
**Fix:** Either have the wiring log the returned stats with a superseded tag when a post-cycle check warrants it, or drop the field until a consumer exists (YAGNI).

### IN-03: `BlockGateResult::reasons` doc "empty iff erasable" is stale after BLOCK_ALREADY_ERASED

**File:** `src/transaction/PruneEligibility.hpp:182-183`
**Issue:** An erasable BLOCK_ALREADY_ERASED verdict now carries a non-empty `reasons` vector (the evidence code), contradicting the field comment. The `erasable` field's own doc was updated; `reasons`' was not.
**Fix:** Update to "Every failed per-key check (deduplicated); empty iff erasable — except the affirmative BLOCK_ALREADY_ERASED evidence, which is carried on an erasable verdict."

### IN-04: `GeniusNode::GetPruneStatus()` has no callers

**File:** `src/account/GeniusNode.hpp:931-941`, `src/account/GeniusNode.cpp:4281-4292`
**Issue:** The new RECL-04 query surface is wired to nothing in `src/`, `gRPCForSuperGenius/`, or `evmrelay/` (only tests read `TxPruner::GetPruneStatus` directly). As shipped, the "queryable" requirement is met by the service but unreachable through the node facade.
**Fix:** Acceptable as a forward surface if an RPC wiring is imminent; otherwise defer adding the facade method until its consumer lands.

### IN-05: Two evaluation INFO summaries per dry-run/real cycle tick (pre-existing shape extended to the real arm)

**File:** `src/transaction/PruneEligibility.cpp:357-361` plus `src/transaction/TxPruner.cpp:285-296`
**Issue:** A real cycle emits the evaluator's "Prune evaluation: N candidates..." INFO line and then the TxPruner "Prune cycle [generation N]: ..." INFO line, each with overlapping counts (eligible vs erased). Not a bug (the two lines have distinct scopes), but operators reading RECL-03 output must know two lines constitute one cycle.
**Fix:** Consider demoting the evaluator's internal summary to debug when driven by the TxPruner, or noting in the TxPruner line that it pairs with the evaluation line.

---

_Reviewed: 2026-10-21T23:09:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
