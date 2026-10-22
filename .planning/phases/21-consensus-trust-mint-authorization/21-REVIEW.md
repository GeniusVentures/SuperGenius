---
phase: 21-consensus-trust-mint-authorization
reviewed: 2026-10-21T00:00:00Z
depth: standard
files_reviewed: 43
files_reviewed_list:
  - example/node_test/sgns_config.json
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/MintTransactionV2.cpp
  - src/account/MintTransactionV2.hpp
  - src/account/PublicChainInputValidator.cpp
  - src/account/PublicChainInputValidator.hpp
  - src/base/CMakeLists.txt
  - src/base/bounded_defer.cpp
  - src/base/bounded_defer.hpp
  - src/blockchain/Blockchain.hpp
  - src/blockchain/Consensus.cpp
  - src/blockchain/Consensus.hpp
  - src/blockchain/ValidatorRegistry.cpp
  - src/blockchain/ValidatorRegistry.hpp
  - src/blockchain/impl/CMakeLists.txt
  - src/transaction/TransactionConsensusHandler.cpp
  - src/transaction/TransactionConsensusHandler.hpp
  - src/transaction/TransactionManager.cpp
  - src/transaction/TransactionManager.hpp
  - test/src/account/CMakeLists.txt
  - test/src/account/burnconfig_policy_e2e_test.cpp
  - test/src/account/mint_evidence_binding_test.cpp
  - test/src/account/network_config_precedence_test.cpp
  - test/src/base/CMakeLists.txt
  - test/src/base/bounded_defer_test.cpp
  - test/src/blockchain/CMakeLists.txt
  - test/src/blockchain/consensus_pending_lifecycle_test.cpp
  - test/src/blockchain/multi_node_finality_fault_test.cpp
  - test/src/blockchain/validator_admission_equivocation_test.cpp
  - test/src/blockchain/validator_registry_verified_load_test.cpp
  - test/src/bridge_race/bridge_race_fault_rpc_test.cpp
  - test/src/mock/mock_rpc_transport.cpp
  - test/src/mock/mock_rpc_transport.hpp
  - test/src/multiaccount/multi_account_sync.cpp
  - test/src/transaction_sync/CMakeLists.txt
  - test/src/transaction_sync/legacy_mint_admission_test.cpp
  - test/src/transaction_sync/legacy_mint_sync_admission_test.cpp
  - test/src/transaction_sync/migration_sync_test.cpp
  - test/testutil/genius_node_test_access.hpp
  - test/testutil/local_trust_setup.hpp
findings:
  critical: 3
  warning: 5
  info: 4
  total: 12
status: issues_found
---

# Phase 21: Code Review Report

**Reviewed:** 2026-10-21
**Depth:** standard
**Files Reviewed:** 43 (diff base `bbd64e814`; ~3,045 insertions across src)
**Status:** issues_found

## Summary

Security-remediation phase covering the verified registry chain-walk (`LoadVerifiedRegistryByCid` / `VerifyRegistryChainClassified`), the strict registry-authorization path in `ConsensusManager::ValidateCertificate` with its D-01 grandfather probe and D-03 defer bound, the certificate-conditional v1-mint sync stall (MINT-03), the cross-certificate equivocation guard (TRUST-02), and the `BoundedDeferTracker` mechanism.

The chain-walk itself is well built: cycle rejection, memo-only-after-full-walk, first-seen deadline anchoring, single-oldest eviction, and the raw-loader recursion guard are all correct and well tested (`validator_registry_verified_load_test.cpp` covers forged roots, cycles, non-quorum mid-chain, retryable-miss classification, and memo-cap eviction). `BoundedDeferTracker` is correct per its contract.

However, the review found three blocker-grade defects at the new authorization boundaries: (1) the MINT-04 burn-fact binding reads only the FIRST output of a mint-v2 while deserialization accepts arbitrarily many outputs — a multi-output mint inflates minted value beyond the attested burn; (2) all receipt-log defense (including MINT-04) is conditional on the endpoint having a configured bridge contract, so an endpoint without one confirms a mint on receipt existence alone; (3) the grandfathered certificate path re-opens the exact unbounded-stall DoS the D-03 defer tracker was added to close, because its registry-load failure returns `Stalled` forever with no bound. The MINT-03 sync gate, the equivocation guard core, and the config wiring are sound apart from listed warnings.

## Critical Issues

### CR-01: MINT-04 burn-fact binding reads only `front()` — a multi-output mint-v2 mints value beyond the attested burn

**File:** `src/account/MintTransactionV2.cpp:145-168`, `src/account/PublicChainInputValidator.cpp:248-266`
**Issue:** `MintTransactionV2::GetAmount()`, `GetTokenID()`, and the new `GetPrimaryDestinationAddress()` all return `utxo_params_.second.front()` only. `FromNew` (MintTransactionV2.cpp:118-135) accepts an arbitrary number of outputs — it only rejects `outputs.empty()`. `DecodeAndBindBurnFacts` compares the receipt's decoded burn facts (token, amount, recipient) against these front()-only accessors, and no ingress constrains a mint-v2 to a single output (the UTXO commitment checks in `GeniusInputValidator.cpp:220` only verify commitment/output count consistency). A submitter crafts a mint-v2 with output[0] = exactly the attested burn (token/amount/recipient) plus output[1..n] carrying arbitrary additional value: every MINT-04 check passes on output[0], and the effects path (`GetProducedUTXOs` over all outputs) mints the extra value. The evidence boundary binds one output while the mint creates N.
**Fix:** For evidence-required mint types, require exactly one output at deserialization/admission, or bind the full output set:
```cpp
// In DecodeAndBindBurnFacts (or the admission predicate):
if ( mint.GetUTXOOutputs().size() != 1 )
{
    logger->error( "receipt/mint fact binding failed: multi-output mint tx={}", PreviewValue( mint.GetHash() ) );
    return std::nullopt;
}
```
Alternatively compare `sum(outputs.amount) == facts.amount` and require every destination equals `facts.destination`.

### CR-02: Receipt-log defense (incl. MINT-04) is skipped when an endpoint has no `bridge_contract_address` — endpoint confirms on receipt existence alone

**File:** `src/account/PublicChainInputValidator.cpp:580-640`
**Issue:** The entire log-matching block — bridge-contract address check, accepted-topic0 check, and the new MINT-04 burn-fact binding — is nested under `if ( !ep.bridge_contract_address.empty() )`. An endpoint configured with an empty bridge contract address skips all of it and, on a receipt that merely exists with `status == 0x1` (line 567), is credited full `consensus_weight` at line 640. With ≥75 weight from such endpoints, a mint-v2 citing any successful public-chain transaction hash (e.g., an unrelated transfer) gathers valid evidence. The MINT-02 fixes close the empty-source/local-chain identities but keep this per-endpoint fail-open. This is reachable through `GeniusNode::ConfigureRpcEndpoint` (GeniusNode.cpp:4410-4423), which accepts arbitrary `WeightedRpcEndpoint` vectors from callers, and through `AddRpcEndpoints` merges where the fetched bridge address can remain empty. The new tests (`mint_evidence_binding_test.cpp:185-205`) always set a bridge contract, so the gap is untested.
**Fix:** For evidence-required mint types, an endpoint without a configured bridge contract (or with an empty accepted-topic0 set) must not confirm:
```cpp
if ( RequiresPublicChainEvidence( tx ) && ( ep.bridge_contract_address.empty() || ep.accepted_topic0_hashes.empty() ) )
{
    logger->warn( "GatherVerificationEvidence: endpoint lacks bridge binding for chain_id={} url={} — not confirming",
                  chain_id, ep.url );
    continue;
}
```

### CR-03: Grandfathered certificate path has no defer bound — crafted certificate at any finalized slot stalls its CRDT delta forever

**File:** `src/blockchain/Consensus.cpp:3673-3688` (contrast the bounded strict path at 3714-3728)
**Issue:** The strict path bounds a not-synced registry stall with `authorization_defer_tracker_` (30 min → deterministic `Check::Reject`). The grandfathered path returns `Check::Stalled` on ANY registry-load error with no bound ("any load error stays retryable exactly as before"). The D-01 probe at 3650 grants grandfathering to ANY certificate whose `GetExpectedCertificateSlotKey()` matches a slot that already has a durable record — the record's content is never compared to the candidate, and the slot key (`account:nonce`) for an attacker's own already-finalized slot is fully attacker-controlled. Any gossip peer can therefore craft a structurally valid certificate (self-signed proposal, correctly computed `proposal_id`) for its own finalized slot referencing a nonexistent registry CID: the probe hits, `LoadUnverifiedRegistryByCid` fails forever, `FilterCertificate` (3472-3485) returns `Stall` forever, and the whole CRDT delta containing that element — including honest elements merged into it — retries indefinitely. This is precisely the attack the FilterCertificate comment describes ("an attacker needs only a self-signed proposal referencing a nonexistent registry CID"); the strict path closes it, the grandfather path re-opens it.
**Fix:** Route the grandfathered load failure through the same defer bound:
```cpp
registry_ret = registry_->LoadUnverifiedRegistryByCid( certificate.registry_cid() );
if ( registry_ret.has_error() )
{
    if ( authorization_defer_tracker_.Defer( certificate.registry_cid() ) )
    {
        logger_->error( "{}: grandfathered registry defer window elapsed for cid {} proposal_id={}", ... );
        return Check::Reject;
    }
    return Check::Stalled;
}
```

## Warnings

### WR-01: Grandfather probe binds by slot-key existence only — no content binding between the durable record and the candidate

**File:** `src/blockchain/Consensus.cpp:3648-3670, 3679`
**Issue:** `grandfathered_final` is set whenever ANY record exists at the candidate's derived slot key; the stored bytes are never parsed or compared. Every future certificate claiming a finalized slot skips TRUST-01 chain verification and tallies against an unverified registry (`TallyVotes` checks only self-consistency of the certificate's own claimed CID/epoch — there is no currency check, so a stale registry in which the signer set still holds quorum validates). If the conflicting certificate's signer set is disjoint from the stored one, the equivocation guard does not fire and control falls through to the lower-hash overwrite rule (3550-3554), so the conflicting certificate can replace the stored winner when its serialized hash is lower. Marginal exposure is narrowed by the fact that locally stored registries passed the element filter (so fabricated registries are hard to plant), but the probe converts "a record exists at this slot" into "this different certificate is authorized", which is a stronger claim than the evidence supports.
**Fix:** Parse the durable record and require it to BE this certificate (byte/hash equality) before skipping strict authorization; at minimum require `GetExpectedCertificateSlotKey` record content to match the candidate's serialization, or run the strict verified load even on the grandfather path and use the unverified loader only as the tally registry.

### WR-02: `SetLegacyMintDeferWindow` accepts negative windows — immediate reject-on-first-sight (asymmetric with its consensus twin)

**File:** `src/transaction/TransactionManager.cpp:4636-4639`; config read at `src/account/GeniusNode.cpp:1892`
**Issue:** `read_seconds` reads `authorization_defer_window_sec` as a plain int and passes negatives through. `ConsensusManager::SetAuthorizationDeferWindow` (Consensus.cpp:688-697) warns and ignores negatives; `TransactionManager::SetLegacyMintDeferWindow` does not — a negative window makes every `Defer()` compute a past deadline and return expired on first sight, so value-carrying v1 mints are rejected and tombstoned immediately instead of stalling. Legitimate historical v1 mints whose certificates legitimately arrive later (unordered deltas) would be permanently dropped on that node (data loss via misconfiguration). The ill-typed-value test (`network_config_precedence_test.cpp`, `AuthorizationTrustKnobsIgnoreIllTypedValues`) covers a string and a negative CAP but not a negative window integer.
**Fix:** Mirror the guard: `if ( window.count() < 0 ) { warn; return; }` in `SetLegacyMintDeferWindow`, or clamp negatives in `read_seconds`.

### WR-03: `SendTransactionItem` MINT-03 preflight aborts the entire submission batch on the first legacy mint

**File:** `src/transaction/TransactionManager.cpp:2025-2032`
**Issue:** The loop `return outcome::failure( std::errc::operation_not_permitted )` on the first value-carrying v1 mint, so no other transaction in the batch — including fully valid ones — is submitted. One poisoned entry blocks all (contrast `DistributeTaskResults`'s explicit "one malformed entry must never block the payout" discipline at 1145-1147).
**Fix:** Skip and report the offending item per-element (continue the loop, aggregate a per-item error) rather than aborting the whole call.

### WR-04: Corrupt/unparseable registry deltas are classified as retryable-forever — no bound, no distinct error class

**File:** `src/blockchain/ValidatorRegistry.cpp:806-818, 916-921`
**Issue:** In `LoadUnverifiedRegistryUpdateByCid`, an element that fails `DeserializeRegistryUpdate` is `continue`d and the function falls through to `no_such_file_or_directory`; `VerifyRegistryChainClassified` then maps ANY fetch error to `resource_unavailable_try_again`. A delta that exists locally but is permanently corrupt (partial write, bit rot) is indistinguishable from "not synced yet": `EvaluateBatchSubject` pends forever, `HandleBatchCertificate` stalls forever, with no terminal bound and no diagnostic distinguishing corruption from ordering. This is the error-classification coarseness the phase brief asks about — the retryable class must not absorb permanent local corruption.
**Fix:** Return a distinct error (e.g., `std::errc::bad_message` / `invalid_argument`) on parse failure of an element that WAS found under the registry key, and map it to `permission_denied`-style permanent failure (or at least a dedicated log + bound) at the walk level.

### WR-05: Equivocation marker is local-only and carries no protocol consequence

**File:** `src/blockchain/Consensus.cpp:1384-1448`
**Issue:** `RecordEquivocationMarker` writes via a raw `datastore->put` (private local RocksDB prefix, not CRDT-replicated). The marker never leaves the node, nothing aggregates markers across nodes, and no consumer exists — the equivocator's registry penalty/weight is untouched, so the signer pays nothing on any node that did not personally observe both certificates. The guard's Reject also only protects nodes whose filter sees the conflicting element. Acceptable as 21-07 scope ("cheap guards"), but the header's "durable marker for the audit trail" oversells a single-node log entry.
**Fix:** Track as a deferred item: either replicate the marker (CRDT key under the consensus topic) or wire it into `ApplyVoteEffects`-style penalty bookkeeping; at minimum, state the local-only scope in the header contract.

## Info

### IN-01: Expired legacy-mint defer entries are never `Clear()`ed

**File:** `src/transaction/TransactionManager.cpp:3818-3828`
**Issue:** The deadline-reject path `break`s without `legacy_mint_defer_tracker_.Clear( slot_id )`; dead keys occupy the 4096-entry table until FIFO-evicted, eroding effective capacity (and each eviction re-arms that key's deadline if re-deferred). Semantics remain correct (re-defer of an untracked expired key re-anchors), this is capacity hygiene only.
**Fix:** `legacy_mint_defer_tracker_.Clear( slot_id );` before the `break` in the expired branch.

### IN-02: Mint type taxonomy duplicated as raw strings across modules

**File:** `src/transaction/TransactionManager.cpp:4626-4634`, `src/account/PublicChainInputValidator.cpp:164-168`
**Issue:** `"mint"` / `"mint-v2"` literals appear in both `IsValueCarryingLegacyMint` and `RequiresPublicChainEvidence` (and in `MintTransactionV2::GetSlotID`'s `"mint-v2:"` prefix). A rename or new type silently diverges the two admission predicates — the one-home rule for the type taxonomy.
**Fix:** Shared constants (e.g., `GeniusTransaction::kTypeMintV2`) used by both predicates.

### IN-03: `SubmitCertificate`'s overwrite path lacks the equivocation probe `FilterCertificate` applies

**File:** `src/blockchain/Consensus.cpp:2852-2866` (contrast 3517-3547)
**Issue:** The local write path compares only serialized hashes against the existing slot record; the cross-certificate equivocation check exists only in the gossip filter. The local path produces this node's own legitimately-certified output so exposure is low, but the asymmetry means the durable record can still be overwritten lower-hash-first without the marker on the write path.
**Fix:** Unify: extract the existing-vs-candidate arbitration (hash ordering + equivocation probe) into one helper called from both paths.

### IN-04: Verified-registry load failure drops inbound proposals outright while votes for the same proposal pend

**File:** `src/blockchain/Consensus.cpp:2957+` (`HandleProposal`)
**Issue:** `LoadVerifiedRegistryByCid` failure in `HandleProposal` logs a warning and returns — the proposal is not queued for retry, while `HandleVote` for the same proposal pends in `pending_votes_` (TTL-bounded). A not-yet-synced registry CID makes the proposal vanish unless re-proposed. Pre-existing shape (the old `LoadRegistryByCid` behaved the same), retained here; the defer-bound discipline applied to `ValidateCertificate` could be applied symmetrically.

---

## Notes on Verified Areas (no findings)

- `BoundedDeferTracker` (src/base/bounded_defer.cpp): first-seen anchoring, single-oldest eviction, mutex discipline, and `Clear`'s map/deque sync are all correct; `bounded_defer_test.cpp` covers each adversarial case including eviction-redeferral and concurrent access.
- `VerifyRegistryChainClassified`: cycle detection via `visited_cids`, memo-only-after-termination, ancestor-first memoization, and the Contains/Lookup eviction race handling are correct; `LoadUnverifiedRegistryByCid`'s use inside `VerifyUpdateClassified` correctly avoids the documented recursion.
- MINT-03 sync admission (`FilterTransaction`): the certificate probe is soundly bound — `GetCertificateBySlot` re-runs full `ValidateCertificate` (slot-key binding + quorum) and `CertificateMatchesTransaction` binds account/nonce/tx-hash/embedded-transaction/CheckHash, so a value-carrying v1 mint cannot pass without a fully valid certificate at its exact slot; the stall/reject/certified-pass triage and observability counters are correct.
- `ProveCrossCertificateEquivocation`: scoped to different verified mint hashes at one canonical slot (same-subject view-change contention cannot fire it); dual signature verification with distinct signing bytes is correct; `topics.front()` in the RPC path is guarded by the `topics.empty()` check.
- Verified-only `LoadRegistryByCidCached` with single-oldest eviction keeps map/deque consistent; the genesis-manifest fingerprint pin is genuinely enforced (FATAL_TRUST_MISMATCH on mismatch, value never logged).

_Reviewed: 2026-10-21_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
