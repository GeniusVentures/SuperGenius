---
phase: 12-multi-node-finality-fault-proof
reviewed: 2026-09-01T00:00:00Z
depth: standard
files_reviewed: 9
files_reviewed_list:
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
  - test/src/blockchain/multi_node_finality_fault_compatibility_smoke_test.cpp
  - test/src/blockchain/multi_node_finality_fault_test.cpp
findings:
  critical: 2
  warning: 7
  info: 8
  total: 17
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-09-01T00:00:00Z
**Depth:** standard
**Files Reviewed:** 9
**Status:** issues_found

## Summary

This round covers the full Phase 12 surface: the production TransactionManager/ConsensusManager/Blockchain consensus-finality code paths plus the three test targets and their CTest wiring. The 12-12 fixture repair (`PutConvergentImmutable` per direction in `consensus_pending_lifecycle_test.cpp:1280-1311`) was verified and is correct: each direction now writes the canonical slot key through the production convergent-immutable path on a dedicated node DB, so no direction depends on same-priority CRDT overwrite ordering.

The two prior review rounds' findings (process-ownership runner, launcher cleanup) remain fixed in the current source and are not re-reported.

The production code carries two Critical defects on the certificate/consensus ingress paths that Phase 12 exists to harden:

1. `ConsensusManager::CreateProposalState` mutates the shared `proposals_`/`slot_states_` maps without holding `proposals_mutex_` while being called from the pubsub receive thread — a genuine data race against the round-timer thread on every certificate received over GossipPubSub.
2. Two sites in `TransactionManager` dereference `tx_processed_m.find(...)` without checking for `end()`, and the lookup key can legitimately miss (multi-network tracked keys on DEV_NET), producing undefined behavior/crash on the conflicting-transaction path.

The remaining warnings are logic errors and disabled validation on these same paths (a status-promotion write to a loop copy, a dead error branch, the size gate running after the CRDT commit, the proof filter's short-circuited verification, and non-idempotent mint-confirm retry effects), plus an unbounded test-observation vector growing inside the production vote loop.

## Critical Issues

### CR-01: `CreateProposalState` mutates shared consensus maps without `proposals_mutex_` (data race)

**File:** `src/blockchain/Consensus.cpp:3017-3036` (called from `HandleCertificate`, `src/blockchain/Consensus.cpp:2993`)
**Issue:** `CreateProposalState` executes `proposals_.emplace(...)` and `slot_states_[new_state.slot_key]` with no lock. Its caller `HandleCertificate` runs on the pubsub receive thread (`OnConsensusMessage` -> `HandleCertificate`) and takes no lock beforehand; `FetchProposalState` immediately before it locks and unlocks correctly. Meanwhile the round-timer thread (`StartRoundTimer` loop -> `ProcessCertificates`, `ProcessDueVoteWork`, `ExpirePendingProposals`, `ClearProposalSlot`) and other pubsub callbacks (`HandleProposal`, `HandleVote`) mutate the same `std::unordered_map` containers under `proposals_mutex_`. Concurrent rehash/erase during an unlocked `emplace` is undefined behavior (heap corruption, crashes). Every node receiving any certificate over GossipPubSub while its timer runs hits this window.
**Fix:**
```cpp
ConsensusManager::ProposalState ConsensusManager::CreateProposalState( const Certificate &certificate )
{
    ProposalState new_state;
    new_state.proposal = certificate.proposal();
    new_state.slot_key = GetSlotKey( new_state.proposal );
    {
        std::lock_guard lock( proposals_mutex_ );
        proposals_.emplace( new_state.proposal.proposal_id(), new_state );

        auto &slot_state = slot_states_[new_state.slot_key];
        if ( slot_state.best_proposal_id.empty() )
        {
            slot_state.best_proposal_id = new_state.proposal.proposal_id();
            auto nonce_payload = DecodeNonceSubject( new_state.proposal.subject() );
            if ( nonce_payload.has_value() )
            {
                slot_state.best_tx_hash = nonce_payload.value().tx_hash();
            }
        }
    }
    return new_state;
}
```
Alternatively return the insert result and perform slot mutation in the already-locked `ValidateCertificateBestProposal` scope.

### CR-02: Unchecked `end()` dereference of `tx_processed_m.find()` on the conflicting-transaction paths

**File:** `src/account/TransactionManager.cpp:3430-3435` and `src/account/TransactionManager.cpp:3962-3965`
**Issue:** Both sites do:

```cpp
auto it = tx_processed_m.find( GetTransactionPath( conflicting_tx.value()->GetHash() ) );
// "No need to check if not found because we already found it on GetConflictingTransaction"
if ( it->second.status == TransactionStatus::CONFIRMED )
```

The assumption is false. `GetConflictingTransaction` -> `GetTransactionByNonceAndAddress` (`TransactionManager.cpp:3013-3026`) locates the entry by scanning values (nonce + source address), but `GetTransactionPath(tx_hash)` builds the key from the *current* network only (`GetBlockChainBase()` -> `/bc-<GetNetworkID()>/`). Entries are inserted under network-specific keys by `FetchAndProcessTransaction` (`TransactionManager.cpp:2586, 2615`), and on DEV_NET `GetMonitoredNetworkIDs()` also monitors TEST_NET (963) and MAIN_NET (369) (`TransactionManager.cpp:1361-1370`). A conflicting transaction tracked under `/bc-963/tx/...` while the local network is 144 makes `find()` return `end()`, and `it->second` is undefined behavior (typically a wild read/crash) inside CRDT-ingress certificate handling. The second site (line 3963) has not even the misleading comment.
**Fix:** Check the iterator at both sites and fall back to a value-scan (or skip the status check) when the key-form does not match:

```cpp
std::unique_lock tx_lock( tx_mutex_m );
auto it = tx_processed_m.find( GetTransactionPath( conflicting_tx.value()->GetHash() ) );
if ( it == tx_processed_m.end() )
{
    // Key namespace differs (multi-network tracked entry); resolve by value scan.
    it = std::find_if( tx_processed_m.begin(),
                       tx_processed_m.end(),
                       [&]( const auto &kv ) {
                           return kv.second.tx &&
                                  kv.second.tx->GetHash() == conflicting_tx.value()->GetHash();
                       } );
    if ( it == tx_processed_m.end() )
    {
        tx_lock.unlock();
        return ConsensusManager::Check::Approve; // nothing local to arbitrate
    }
}
```

## Warnings

### WR-01: `CheckTransactionValidity` promotes a copy, never the tracked entry

**File:** `src/account/TransactionManager.cpp:2902, 2949`
**Issue:** `for ( auto [key, tracked] : tx_processed_m )` copies each `TrackedTx`; `tracked.status = TransactionStatus::CONFIRMED;` (line 2949) mutates only the copy. The documented behavior (header, `TransactionManager.hpp:461-466`: "Valid ones are promoted to CONFIRMED") never happens — the map entry stays `VERIFYING`, so `GetOutgoingStatusByTxId`/`WaitForTransactionOutgoing` keep reporting a non-terminal state until timeout for transactions validated through this path (`SyncNonce` local-nonce-ahead recovery).
**Fix:** Iterate by reference: `for ( auto &[key, tracked] : tx_processed_m )` (the enclosing `std::unique_lock<std::shared_mutex> tx_lock( tx_mutex_m )` already permits mutation).

### WR-02: `QueryTransactions` tests the wrong variable — error branch is dead code

**File:** `src/account/TransactionManager.cpp:1812-1819`
**Issue:** After `if ( !transaction_key.has_value() ) { ...continue; }` (lines 1805-1811) the code calls `FetchAndProcessTransaction` and then checks `if ( !transaction_key.has_value() )` again instead of `process_result.has_error()`. The error-logging branch is unreachable and fetch/process failures are silently dropped.
**Fix:**
```cpp
auto process_result = FetchAndProcessTransaction( transaction_key.value(), value );
if ( process_result.has_error() )
{
    TransactionManagerLogger()->error( "... Unable to fetch and process transaction {} ... {}",
                                       ..., process_result.error().message() );
}
```

### WR-03: SIZE-01 pre-publish size gate runs after the CRDT commit and mid-proposal loop

**File:** `src/account/TransactionManager.cpp:1296-1313` (gate) vs `:1253` (commit)
**Issue:** The comment claims rejection "before they enter the consensus pipeline", but `crdt_transaction->Commit( topicSet )` (line 1253) has already durably published the transaction batch, and the gate sits inside the second loop that creates/submits proposals per transaction. For a batch where tx1 passes and tx2 exceeds `MAX_PUBSUB_TX_BYTES`, tx1's proposal is already submitted via `SubmitProposal` before the failure return; `TickOnce` (`TransactionManager.cpp:490-507`) then marks the whole item FAILED and pops it. Result: committed CRDT transaction data, an in-flight proposal for tx1, and local tracking FAILED — inconsistent local vs network state (partially self-healing only if a certificate later arrives).
**Fix:** Perform the size check (and UTXO commitment/witness construction) in the first loop before `Commit`, or pre-validate the whole batch (size + commitments) before any `Put`/`Commit`/`SubmitProposal` side effect.

### WR-04: `FilterProof` verification short-circuited — all incoming proofs accepted

**File:** `src/account/TransactionManager.cpp:3186-3207`
**Issue:** `valid_proof = true; break;` executes first, making the entire `IBasicProof::VerifyFullProof` block (lines 3190-3206) unreachable dead code. The CRDT proof filter therefore accepts every proof element, and the tombstoning branch (lines 3209-3221) can never run. The header documents this as short-circuited, but it disables a security check on the ingress path and leaves ~17 lines of dead code.
**Fix:** Remove the early `valid_proof = true; break;` so verification actually executes (and keep the TODO about reputation penalties), or delete the dead block and rename the filter until verification is enabled — do not ship both.

### WR-05: `active_vote_announcements_for_test_` grows unbounded, unlocked, in the production vote loop

**File:** `src/blockchain/Consensus.cpp:1566`
**Issue:** `active_vote_announcements_for_test_.push_back( bytes );` runs unconditionally on every vote announcement in `ProcessDueVoteWork` — including production nodes with no test attached. The vector is never cleared in production, so every replayed/retried active vote (retry cadence 500 ms per slot, `active_vote_retry_interval_`) accumulates serialized vote bytes forever: unbounded memory growth in long-running nodes. Additionally, the push happens without `fault_test_mutex_` (unlike the adjacent `fault_test_counters_` updates at lines 1570-1572) while the test accessor `ActiveVoteAnnouncements` (`consensus_pending_lifecycle_test.cpp:392-395`) reads it without synchronization — a latent data race with the round-timer thread.
**Fix:** Gate the capture on an armed test flag, or bound it (ring buffer) and guard it with `fault_test_mutex_`:

```cpp
if ( stop_timer_.load() ) return;
std::string bytes;
if ( !active_vote.vote.SerializeToString( &bytes ) ) continue;
{
    std::lock_guard lock( fault_test_mutex_ );
    if ( fault_test_counters_.vote_capture_enabled )   // armed only by friend test access
    {
        active_vote_announcements_for_test_.push_back( bytes );
    }
}
```

### WR-06: Migration branch of `ParseMintTransaction` ignores `PutUTXO`/`ConsumeUTXOs` results

**File:** `src/account/TransactionManager.cpp:1954, 1959`
**Issue:** In the `MigrationTransaction` branch the return values of `PutUTXO` and `ConsumeUTXOs` are silently dropped, while the `MintTransactionV2` branch directly below uses `BOOST_OUTCOME_TRY` for the identical calls (lines 1977, 1982-1985). Storage failures during migration application are swallowed, leaving partial UTXO state with no error propagated to `ChangeTransactionState`/consensus handling.
**Fix:** Wrap both calls with `BOOST_OUTCOME_TRY` to match the mint-v2 branch.

### WR-07: mint-v2 CONFIRMED retry re-applies parse effects; idempotency relies on an ignored bool and clobbers burn UTXO metadata

**File:** `src/account/TransactionManager.cpp:5426-5469` (with `src/account/UTXOManager.cpp:261-301`)
**Issue:** When `PersistBridgeExecutedMarker` fails, the entry is deliberately left `VERIFYING` ("Keep failed certificate work explicitly retryable", line 5437-5439) and the certificate handler returns an error, so `ProcessCommittedCertificate` marks the journal stalled and retries. On retry, `ChangeTransactionState(CONFIRMED)` sees status `VERIFYING` (not CONFIRMED) and re-runs `ParseTransaction` (line 5441). Second application effects: `mint_effects_for_test_` double-counts (breaking the Phase 12 "exactly one live mint effect" invariant within a process), `UpdateAccountUTXOState` version double-increments, and `ConsumeUTXOs` for the already-CONSUMED burn outpoint falls into the not-found branch (`UTXOManager.cpp:289-298`), overwriting the burn entry with an amount-0 `GeniusUTXO` and returning `consumed=false` — which `BOOST_OUTCOME_TRY` at `TransactionManager.cpp:1982` discards because it is not an error.
**Fix:** Record the parse boundary separately from the tracking status (e.g. keep a `mint_effects_applied` flag on the entry, or set the entry to an intermediate APPLIED state) so a marker-write retry skips re-parsing; and in `ParseMintTransaction` inspect the `ConsumeUTXOs` bool instead of discarding it.

## Info

### IN-01: `NONCE_REQUEST_TIMEOUT_MS` value contradicts its comment

**File:** `src/account/TransactionManager.hpp:50-51`
**Issue:** Constant is `5000` ms but the comment says "(10 seconds)".
**Fix:** Update the comment to "5 seconds" (or change the value if 10 s was intended).

### IN-02: Unreachable block after unconditional `return` in `InitTransactions`

**File:** `src/account/TransactionManager.cpp:2655-2697`
**Issue:** `return;` at line 2656 (guarded only by a TODO comment) makes the entire transaction-request/cooldown block below it dead code, including the `k_init_tx_request_cooldown_ms` logic that the header still documents.
**Fix:** Delete the dead block or gate the `return` behind the documented condition instead of an unconditional early exit.

### IN-03: `RollbackTransactions` implementation does not match its documentation

**File:** `src/account/TransactionManager.hpp:369-377` vs `src/account/TransactionManager.cpp:1329-1337`
**Issue:** Header documents nonce re-fetch, marking intermediate nonces VERIFYING, UTXO reversion, and nonce release; the implementation only marks the batch FAILED (delegating the rest to `ChangeTransactionState`).
**Fix:** Align the doc comment with the actual behavior or restore the described steps.

### IN-04: "process" and "escrow-release" transaction types can be deserialized but never pass well-formedness

**File:** `src/account/TransactionManager.cpp:1443-1459` vs `:4517-4525`
**Issue:** `DeSerializeEmbeddedTransaction` registers deserializers for `process` and `escrow-release`, but `transaction_parsers` (lines 97-107) omits both, so `CheckTransactionWellFormed` rejects any such embedded transaction during consensus validation — the types are dead at the nonce-consensus boundary.
**Fix:** Either add parser/reverter entries for these types or document that they must not travel through nonce subjects.

### IN-05: `consumed_root_result` computed, validated, then discarded

**File:** `src/account/TransactionManager.cpp:4874-4885, 4910`
**Issue:** The parsed consumed-outpoints root is only size/parse-checked and then `(void) consumed_root_result;` — the actual root equality is enforced elsewhere (BIND-01 in `HandleNonceConsensusSubject`). Dead computation that suggests a check that does not happen here.
**Fix:** Remove the parse or use the parsed root in the comparison.

### IN-06: CMake platform gate rejects Linux while claiming POSIX support

**File:** `test/src/blockchain/CMakeLists.txt:62-64`
**Issue:** `if(NOT APPLE OR WIN32)` fails the configure step on Linux, but the error message says "requires supported POSIX platform: fork/setsid/..." and the following `check_symbol_exists` probes are exactly the POSIX primitives Linux provides. Either the gate is intentionally macOS-only (then the message and the primitive checks are misleading) or Linux is being excluded unnecessarily.
**Fix:** Tighten the message to state the macOS-only constraint (e.g. `message(FATAL_ERROR "Phase 12 runner is currently supported on macOS only")`) or relax the gate to `if(WIN32)` if the primitives are sufficient.

### IN-07: Evidence-collector child performs `fcntl` on already-closed descriptors

**File:** `test/src/blockchain/multi_node_finality_fault_test.cpp:1534-1537`
**Issue:** In the forked child, `::close( data_pipe[0] ); ::close( control_pipe[0] );` is immediately followed by `::fcntl( data_pipe[0], F_SETFD, FD_CLOEXEC );` on the closed descriptors — no-op with EBADF, misleading to readers.
**Fix:** Delete the two `fcntl` lines (the read ends are already closed in the child).

### IN-08: Process-global `slot_key_handlers_` registry is registered per-instance but owned by no instance

**File:** `src/blockchain/Consensus.hpp:977-979`, `src/account/TransactionManager.cpp:170-185`, `src/blockchain/impl/Blockchain.cpp:1704-1712`
**Issue:** `slot_key_handlers_` is a `static inline` map; every `TransactionManager::New` (i.e. every peer in the in-process four-peer fixtures) overwrites the `NONCE_SUBJECT_TYPE` entry, and `Blockchain::UnregisterSlotKeyHandler` would erase it for all live instances. This is safe today only because the registered lambda is stateless and nothing in production unregisters. It is a fragile invariant for any future instance-capturing handler.
**Fix:** Document the stateless-handler requirement next to `RegisterSlotKeyHandler`, or key registrations by ConsensusManager instance.

---

_Reviewed: 2026-09-01T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
