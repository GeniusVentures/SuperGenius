---
phase: 09-canonical-slot-and-certificate-storage
verified: 2026-07-24T12:41:40Z
status: gaps_found
score: "38/43 must-have truths verified; 8/10 requirements satisfied"
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: "10/16 must-have truths verified; 7/10 requirements satisfied"
  gaps_closed:
    - "Noncanonical/zero external mint identities now fail before mutation, local bypass is explicit, and receipt hashes are bound to the request."
    - "Receipt-resolution and enabled-query failures now preserve the catch-up cursor and retry with a fresh receipt cache."
    - "Certificates now use strict recursive unknown-field rejection, complete vote validation, derived fields, canonical ordering, and deterministic bytes."
    - "Missing registry state now has a bounded source-CID dependency retry path."
    - "Certificate lookup now distinguishes NotFound, IntegrityError, and StorageError."
    - "The live CRDT filter now guards the complete /cert/ namespace, including tombstones."
    - "Real previous-nonce and producer-UTXO consumers now execute winner, absence, corruption, and operational-error branches."
  gaps_remaining: []
  regressions:
    - "Catch-up still advances after BurnProcessor false/exception outcomes that are not durable duplicate proof."
    - "Certificate write-once preflight treats every raw datastore read error as absence on local and replicated paths."
    - "A mixed Reject plus RetryDependency delta discards the retry decision and can merge the retained stalled namespace."
gaps:
  - truth: "A catch-up burn is delivered durably before its chunk is deduplicated and its cursor advances."
    status: failed
    reason: "The boolean BurnProcessor contract collapses durable duplicates, transient unavailability, reservation, submission failure, and exceptions into false; the watcher still commits seen_burns and advances the cursor."
    artifacts:
      - path: "src/watcher/impl/bridge_catchup_watcher.cpp"
        issue: "False and exception outcomes are counted as skipped, followed by dedup and cursor commit."
      - path: "src/account/GeniusNode.cpp"
        issue: "The production callback returns false for unavailable account/node, reserved outpoints, failed MintTokens, and exceptions."
    missing:
      - "Use explicit Processed/AlreadyHandled/Retry outcomes."
      - "Advance only after every staged burn is processed or proven durably handled."
      - "Add false, exception, transient reservation, and failed-submission cursor regressions."
  - truth: "An occupied canonical certificate slot cannot be bypassed when storage reads fail."
    status: failed
    reason: "SubmitCertificate and FilterCertificateDelta inspect only has_value(); NOT_FOUND, corruption, I/O, timeout, and shutdown errors all look absent and permit write/merge preflight to continue."
    artifacts:
      - path: "src/blockchain/Consensus.cpp"
        issue: "Local and replicated write-once checks use raw db_->Get without typed error classification."
      - path: "test/src/blockchain/consensus_certificate_store_test.cpp"
        issue: "No injected slot/index preflight read-error matrix covers local submission or remote filtering."
    missing:
      - "Map both slot and index preflight reads through the typed certificate-store error mapper."
      - "Treat only exact NOT_FOUND as absence; fail closed on integrity and operational errors."
      - "Test symmetric and asymmetric slot/index failures on both paths."
  - truth: "Retry-dependent certificate data cannot merge when another namespace in the same delta is rejected."
    status: failed
    reason: "CRDTDataFilter gives Reject aggregate precedence but sanitizes only rejecting namespaces, discards the dependency, then returns the retained stalled certificate data for normal merge."
    artifacts:
      - path: "src/crdt/impl/crdt_data_filter.cpp"
        issue: "Mixed Reject/RetryDependency loses dependency_cid and retains retry-dependent elements."
      - path: "src/crdt/impl/crdt_datastore.cpp"
        issue: "Only RetryDependency parks a root; returned Reject deltas are merged."
      - path: "test/src/crdt/crdt_datastore_test.cpp"
        issue: "No two-filter mixed-decision regression proves stalled keys remain invisible."
    missing:
      - "Preserve RetryDependency for retained stalled namespaces after rejected namespaces are sanitized, or remove both classes terminally."
      - "Add a mixed two-filter regression through the datastore merge path."
---

# Phase 09: Canonical Slot and Certificate Storage Verification Report

**Phase goal:** Establish one canonical finality identity for every proposal and make slot-keyed certificates compatible with transaction-hash certificate consumers.

**Status:** `gaps_found`

**Re-verification:** Yes — after plans 09-05 through 09-09 closed the historical gaps.

## Verdict

Phase 09 is substantially implemented, and every historical verification gap is closed. Canonical nonce and burn slots, strict external burn proof, deterministic certificate bytes, atomic slot/index pairs, typed lookup, legacy-state rejection, missing-registry retry, and real compatibility consumers are present and covered by passing integration targets.

The phase still does not meet its complete storage/durability contract. Two fresh critical findings are confirmed directly in current source: catch-up can permanently skip a valid burn after transient mint-submission failure, and certificate write-once preflight can be bypassed by datastore read errors. A third review warning is goal-blocking because a mixed CRDT filter decision can merge a registry-stalled certificate before its dependency is validated.

No human verification is needed to establish this verdict.

## Roadmap Success Criteria

| # | Success criterion | Status | Evidence |
|---|---|---|---|
| 1 | Same nonce shares one normal slot; same burn shares one bridge slot regardless of candidate fields | ✓ VERIFIED | `MakeNonceSlotPreimage`, `MintTransactionV2::GetSlotPreimage`, result-returning `GetSlotKey`, and `consensus_slot_key_test` establish fixed 64-lowercase-hex slot IDs. |
| 2 | Exact-proposal certificate is authoritative by slot with verified transaction-hash index | ✗ FAILED | The representation, atomic pair, and index are implemented, but raw preflight read errors can bypass occupied-slot checks, and mixed Reject/RetryDependency can merge an incompletely validated pair. |
| 3 | Previous-nonce and producer-UTXO consumers retrieve through `GetCertificateBySubjectHash()` | ✓ VERIFIED | Both production consumers use the Blockchain wrapper; real winner/absent/corrupt/I/O matrices pass in `transaction_manager_pending_lifecycle_test`. |
| 4 | Legacy transaction-keyed certificate state fails startup before background side effects | ✓ VERIFIED | `HasCompatibleCertificateState()` precedes subscription/timer/filter/recovery; runtime `/cert/` filtering and restart tests also pass. |

## Observable Truths

### Plan 09-01 — Canonical slot identity

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | Every accepted subject resolves to one lowercase 64-hex SHA-256 slot | ✓ VERIFIED | Domain preimages are validated, SHA-256 hashed, and revalidated by `ConsensusManager::GetSlotKey`. |
| 2 | Same canonical source and nonce share one normal slot | ✓ VERIFIED | `MakeNonceSlotPreimage(source, nonce)` is the sole normal-domain preimage. |
| 3 | Same chain, burn hash, and receipt-local index share one mint slot despite candidate fields | ✓ VERIFIED | Mint preimage contains only those three fields; adversarial equality tests vary proposer/output fields. |
| 4 | Invalid/noncanonical slot inputs reject without proposal fallback | ✓ VERIFIED | Registered handler errors propagate; zero/malformed mint sources and noncanonical nonce addresses fail. |

### Plan 09-02 — Canonical bridge event identity

| # | Truth | Status | Evidence |
|---|---|---|---|
| 5 | Live and catch-up burns carry the absolute receipt-local position | ✓ VERIFIED | Live receipt iteration stores the ordinal; catch-up resolves block-wide index against the complete receipt and checked-narrows it. |
| 6 | Multiple same-transaction burns remain independent | ✓ VERIFIED | Discovery/dedup/persistence use `(tx_hash, receipt_log_index)`. |
| 7 | Outpoint, reservation, rollback, and persistence identity use the index | ✓ VERIFIED | `MintFunds` uses the required index at every identity/state seam. |
| 8 | Only the exact indexed log matching chain/token/amount/destination is accepted | ✓ VERIFIED | Validator bounds-checks and decodes only `receipt.logs[input.output_idx_]`. |

### Plan 09-03 — Slot-keyed certificate store

| # | Truth | Status | Evidence |
|---|---|---|---|
| 9 | Successful publication exposes slot record and winner index in one CRDT delta | ✓ VERIFIED | One batch `GlobalDB::Put(vector<DataPair>)` publishes both siblings. |
| 10 | Replay is idempotent and an occupied-slot conflict is rejected | ✗ FAILED | Correct when reads succeed, but raw preflight read errors are treated as absence on local and replicated paths. |
| 11 | Malformed, partial, or internally mismatched replicated pairs reject before merge | ✓ VERIFIED | Complete namespace filter enforces exact pair shape, keys, value, bytes, signatures, and derivations. |
| 12 | Legacy transaction-keyed state blocks startup before side effects | ✓ VERIFIED | Compatibility scan runs before background setup and reports the offending key. |

### Plan 09-04 — Verified lookup compatibility

| # | Truth | Status | Evidence |
|---|---|---|---|
| 13 | Slot lookup returns only a certificate deriving to the requested slot | ✓ VERIFIED | Input, bytes, normalization, complete certificate, and derived slot are verified. |
| 14 | Hash lookup verifies index-to-slot and certificate-to-winner links | ✓ VERIFIED | Index delegates to authoritative slot lookup, then checks exact embedded winner. |
| 15 | Losing hash is NotFound while full losing subject observes finalized slot | ✓ VERIFIED | Separate hash and subject-slot APIs plus compatibility fixture. |
| 16 | Existing nonce-chain and producer consumers retrieve winners without storage-layout knowledge | ✓ VERIFIED | Both consume Blockchain hash lookup; neither reads `/cert/` directly. |

### Plan 09-05 — External mint proof closure

| # | Truth | Status | Evidence |
|---|---|---|---|
| 17 | Empty/zero/malformed/noncanonical burn hashes cannot mutate state or form a slot | ✓ VERIFIED | `MintFunds` validates before reads/mutation; `MintTransactionV2` rejects zero input hashes. |
| 18 | Noncanonical source-chain IDs reject before mutation | ✓ VERIFIED | Canonical unsigned-decimal validation occurs before duplicate/state operations. |
| 19 | Only explicit Genius chain IDs bypass receipt proof | ✓ VERIFIED | Empty/unknown chain IDs reject; only `supergenius` and `supergenius_chain` bypass. |
| 20 | Endpoint receipt hash must equal the requested burn before contributing weight | ✓ VERIFIED | Hash equality precedes status/log inspection. |

### Plan 09-06 — Transactional catch-up cursor

| # | Truth | Status | Evidence |
|---|---|---|---|
| 21 | Chunk completion requires all enabled queries and receipt identities | ✓ VERIFIED | v1/v2 results gate publication and cursor advancement. |
| 22 | BurnProcessor sees no candidate before all query/receipt validation succeeds | ✓ VERIFIED | `process_logs` only stages; one later publication sweep invokes the callback. |
| 23 | Missing/malformed/mismatched/ambiguous/overflowing receipt identity preserves cursor | ✓ VERIFIED | Every such branch fails the chunk; scripted regressions pass. |
| 24 | Wide receipt ordinal is checked at the uint32 boundary | ✓ VERIFIED | Shared production helper is tested at 0, UINT32_MAX, and UINT32_MAX+1. |
| 25 | Retry performs fresh receipt requests | ✓ VERIFIED | Cache is local to one `poll_once`; two-attempt tests verify refetch. |
| 26 | Successful dependency resolution delivers once before advancing to `chunk_end + 1` | ✗ FAILED | False/exception publication outcomes still commit dedup and cursor even when no durable submission exists. |

### Plan 09-07 — Canonical certificate bytes and namespace

| # | Truth | Status | Evidence |
|---|---|---|---|
| 27 | Accepted certificates have one canonical protobuf representation | ✓ VERIFIED | One normalizer and deterministic serializer serve creation, submission, replication, callback, and lookup. |
| 28 | Unknown/duplicate/invalid/nonordered/redundant variants reject remotely | ✓ VERIFIED | Recursive reflection checks and adversarial pair tests cover each class. |
| 29 | Votes are bytewise ordered, registry-valid, and redundant fields derived | ✓ VERIFIED | Strict tally plus normalization derives timestamp/weights and sorts unique votes. |
| 30 | Runtime `/cert/` accepts only exact v2 pairs; legacy/malformed keys cannot poison restart | ✓ VERIFIED | Complete namespace route strips rejected records; restart regressions pass. |
| 31 | Certificate tombstones are terminally rejected and removed before merge | ✓ VERIFIED | Delta filters match elements and tombstones; exact-ID attack tests retain both authoritative records. |

### Plan 09-08 — Missing-registry dependency retry

| # | Truth | Status | Evidence |
|---|---|---|---|
| 32 | Registry-stalled pair is neither merged, deleted, nor marked processed | ✗ FAILED | True in isolation, false when another matching namespace returns Reject in the same delta; the cert pair is retained but the retry decision is lost. |
| 33 | Filtering preserves terminal rejection versus retryable stall | ✗ FAILED | Aggregate Reject discards a retained namespace's dependency and permits that namespace to merge. |
| 34 | Namespace Reject removes matching elements/tombstones while preserving unrelated namespaces | ✓ VERIFIED | Generic rejection/tombstone tests exercise the sanitation behavior. |
| 35 | Stalled root parks with backoff and releases active ownership | ✓ VERIFIED | Tagged job result routes to bounded parked-root state. |
| 36 | Parked roots are bounded and cleaned at capacity/TTL/attempt limits with fairness | ✓ VERIFIED | Production caps, deadlines, counters, cleanup, and deterministic tests exist. |
| 37 | Registry arrival causes one fully validated atomic merge and one delivery | ✗ FAILED | Holds for an isolated RetryDependency delta, but mixed Reject/Retry can merge early and mark the source processed instead of parking it. |
| 38 | Due retry cannot be starved by later ordinary arrivals or TTL | ✓ VERIFIED | Snapshot-gated scheduler and sustained-arrival/TTL tests implement the contract. |

### Plan 09-09 — Typed reads and real compatibility consumers

| # | Truth | Status | Evidence |
|---|---|---|---|
| 39 | Only database NOT_FOUND becomes certificate NotFound | ✓ VERIFIED | Shared lookup mapper performs the exact mapping. |
| 40 | Corruption maps to IntegrityError and operational failures to StorageError | ✓ VERIFIED | Injected slot/index read tests cover corruption and I/O. |
| 41 | Dangling index is IntegrityError; operational slot failure remains StorageError | ✓ VERIFIED | Relationship handling is separate from raw read mapping. |
| 42 | Real previous-nonce and producer consumers execute all four outcome classes | ✓ VERIFIED | Production private paths are invoked through narrow friend access and real Blockchain fixtures. |
| 43 | Only previous-nonce absence becomes Pending; all other consumer failures reject | ✓ VERIFIED | Assertions verify dependency type/value and fail-closed producer behavior. |

**Must-have score:** **38/43**

## Requirements Coverage

| Requirement | Status | Implementation/test evidence |
|---|---|---|
| SLOT-01 | ✓ SATISFIED | Result-returning canonical slot is used at proposal admission/arbitration, certificate derivation, and subject finality lookup. |
| SLOT-02 | ✓ SATISFIED | Normal preimage is canonical source address plus nonce; nonce collision/separation tests pass. |
| SLOT-03 | ✓ SATISFIED | Mint slot is chain + nonzero burn hash + receipt-local index, independent of proposer/nonce. |
| SLOT-04 | ✓ SATISFIED | Candidate fields are excluded; adversarial same-burn proposals share a slot. |
| CERT-01 | ✗ BLOCKED | A registry-stalled pair can merge through the mixed Reject/Retry aggregation bug before complete registry/quorum validation. |
| CERT-02 | ✗ BLOCKED | Authoritative slot storage is canonical when reads succeed, but corruption/I/O preflight errors can bypass occupied-slot conflict checks. |
| CERT-03 | ✓ SATISFIED | Winner index contains only slot ID and is emitted with the slot record in one delta. |
| CERT-04 | ✓ SATISFIED | Hash lookup verifies canonical index, authoritative payload/slot, exact winner, and typed storage errors. |
| COMP-01 | ✓ SATISFIED | Real previous-nonce and producer-witness paths work through the verified index. |
| COMP-02 | ✓ SATISFIED | Startup rejects legacy state before side effects; runtime namespace/tombstone guards prevent persistent poisoning. |

No orphaned Phase 09 requirements were found. All ten roadmap IDs appear in plan frontmatter.

## Required Artifacts

| Artifact group | Status | Details |
|---|---|---|
| `GeniusTransaction.hpp`, `MintTransactionV2.cpp`, `Consensus.hpp/.cpp` | ⚠ PARTIAL | Substantive and wired for canonical slots/certificates; certificate write preflight in `Consensus.cpp` fails closed only when raw reads return values. |
| `BridgeRelayer.cpp`, `TransactionManager.cpp`, `PublicChainInputValidator.cpp` | ⚠ PARTIAL | Receipt identity and exact-log proof are wired; replay-read and weighted-status warnings remain. |
| `bridge_catchup_watcher.cpp`, `eth_receipt_source.hpp` | ✗ PARTIAL | Receipt staging/retry is substantive, but BurnProcessor outcome lacks durable retry semantics. |
| `crdt_data_filter.hpp/.cpp`, `crdt_datastore.cpp` | ✗ PARTIAL | Tri-state filtering/retry exists, but mixed decisions can merge stalled data; shutdown also has a synchronization/completion warning. |
| `Blockchain.hpp` and compatibility wrappers | ✓ VERIFIED | Separate authoritative slot and exact winner-hash APIs are wired to consumers. |
| `consensus_slot_key_test.cpp`, `bridge_event_identity_test.cpp`, `public_chain_mint_validation_test.cpp` | ⚠ PARTIAL | Strong focused coverage, but no false/exception publication or failed-status endpoint disagreement case. |
| `consensus_certificate_store_test.cpp`, `certificate_compatibility_test.cpp`, `transaction_manager_pending_lifecycle_test.cpp` | ⚠ PARTIAL | Canonical bytes, lookup mapping, consumers, tombstones, and isolated retry are covered; write-preflight read failures and mixed-filter decisions are not. |

All 19 unique PLAN frontmatter artifacts exist and are substantive. No artifact is missing or a stub.

## Key Link Verification

| Link | Status | Details |
|---|---|---|
| Transaction/nonce subject → canonical slot API → consensus maps | ✓ WIRED | One result-returning path; recognized failures do not fall back. |
| Receipt observation → relayer/catch-up → GeniusNode → MintFunds → `output_idx_` | ✓ WIRED | Mandatory receipt-local index is preserved end to end. |
| Mint input index → exact receipt log validator | ✓ WIRED | Bounds, address/topic, ABI fields, chain/token/amount/destination are checked. |
| Certificate → canonical normalization → slot/index batch | ⚠ PARTIAL | Canonical bytes and batch are correct; preflight raw read errors are not classified. |
| Replicated pair → complete namespace filter → CRDT merge | ✗ NOT_WIRED | Isolated RetryDependency works, but mixed Reject/Retry loses the retained dependency barrier. |
| Slot lookup → hash index lookup → Blockchain consumers | ✓ WIRED | Relationships and typed errors are verified through production consumers. |
| Catch-up staging → BurnProcessor → cursor commit | ✗ NOT_WIRED | Callback failure is not distinguished from durable already-handled proof. |

## Fresh Code Review Findings

| Finding | Independent disposition | Goal impact |
|---|---|---|
| CR-01 catch-up commits after recoverable submission failure | **Confirmed blocker** | Fails truth 26 and can permanently omit a valid historical burn. |
| CR-02 certificate preflight treats read errors as absence | **Confirmed blocker** | Fails truth 10 and CERT-02 write-once authority. |
| WR-01 bridge replay persistence read fails open | Confirmed warning | Duplicate mint work can be created during storage failure; does not change slot derivation itself. |
| WR-02 mixed Reject/Retry loses dependency | **Confirmed blocker (elevated)** | Fails truths 32, 33, 37 and CERT-01 validation-before-authority. |
| WR-03 failed-status endpoint vetoes weighted policy | Confirmed warning | Fail-closed but violates endpoint-disagreement liveness/policy; missing regression. |
| WR-04 shutdown races/returns early on worker path | Confirmed warning | Undefined concurrent reads and no synchronous completion guarantee; current shutdown test covers only an external caller. |

These gaps are not explicitly covered by later Phase 10-12 goals and therefore are not deferred.

## Automated Verification

| Check | Result |
|---|---|
| Build nine deterministic phase targets from current HEAD | PASS |
| Build `bridge_anvil_catchup_e2e_test` | PASS |
| CTest focused regex, parallel 2 | PASS — 9/9 targets, 0 failures, 49.99s |
| Full CMake build after all gap plans | PASS — orchestration evidence |
| Anvil catch-up runtime | ENVIRONMENT SKIP — no port available in 18545-18644 before code assertions; not classified as code failure |
| Schema drift | PASS — false |
| Prior current-milestone verification suites | None |
| `git diff --check` before report write | PASS |

The green targets validate implemented success paths and historical gap closures. They do not cover the three blocking paths above.

## Anti-Pattern and Disconfirmation Pass

- No `TBD`, `FIXME`, or `XXX` markers were found in the 40 plan-modified files.
- Existing `TODO` comments were reviewed as warning-level/pre-existing cleanup; none is the implementation basis for this verdict.
- Partial requirement found: CERT-02 has canonical slot storage but an error-collapsing write-once preflight.
- Misleading green-test boundary found: catch-up retry tests inject a BurnProcessor that always returns `true`.
- Uncovered error paths found: local/remote certificate preflight corruption/I/O, mixed Reject/Retry delta aggregation, failed-status endpoint disagreement, and worker-initiated shutdown.

## Human Verification

None required for the verdict. The Anvil E2E should be rerun in an environment with an available port after gap closure, but its current environment skip neither causes nor resolves these source-level blockers.

## Gaps Summary

Three root concerns block Phase 09:

1. Catch-up publication lacks a durable outcome contract, so cursor advancement can outlive transient submission failure.
2. Certificate write-once enforcement fails open on datastore read errors.
3. Multi-filter CRDT aggregation can merge dependency-stalled certificate data.

The other three fresh warnings should be fixed or explicitly accepted, but they do not independently change the `gaps_found` verdict.

---

_Verified: 2026-07-24T12:41:40Z_
_Verifier: Codex (gsd-verifier)_
