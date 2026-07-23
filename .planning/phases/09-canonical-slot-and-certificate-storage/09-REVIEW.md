---
status: issues_found
phase: "09-canonical-slot-and-certificate-storage"
depth: standard
files_reviewed: 31
counts:
  critical: 3
  warning: 4
  info: 1
  total: 8
---

# Phase 09 Code Review

## Scope

Reviewed the requested Phase 09 implementation and tests, including the nested
`evmrelay` receipt/event-filter changes, canonical transaction slot derivation,
bridge relaying and catch-up, certificate CRDT filtering and storage, startup
compatibility checks, typed certificate lookup consumers, and the Phase 09 test
targets. The implementation diff was reviewed from `c852bf9f^`.

## Findings

### CR-001 — Critical — External mint verification can be bypassed with an empty source reference

**File/line:** `src/account/PublicChainInputValidator.cpp:110-123`,
`src/account/PublicChainInputValidator.cpp:320-324`,
`src/account/PublicChainInputValidator.cpp:396-437`

**Evidence:** The validator derives the source reference from the sole input
hash, but falls back to `uncle_hash` when the input is the all-zero `Hash256`.
`VerifyPublicChainSmartContract` returns `true` immediately when that fallback
is empty, before checking the external chain or requiring a receipt. The public
chain UTXO validator only requires one input and one output, and the canonical
mint slot code accepts the 64-character all-zero hash. Consequently, a
numeric-chain `MintTransactionV2` can carry an all-zero burn hash and empty
uncle hash and skip all exact-log proof checks. Additionally, after requesting
a receipt for `tx_hash_parsed`, the verifier never checks that the parsed
receipt's `tx_hash` equals the requested hash before trusting the indexed log.

**Impact:** A signed external-chain mint can be accepted without evidence of
the claimed burn, or against a receipt belonging to a different transaction.
This violates the phase's fail-closed exact-receipt requirement and can create
unbacked tokens.

**Remediation:** For every external-chain mint, reject an all-zero input hash
and any empty/noncanonical source reference. Move the empty-reference bypass
behind an explicit local-chain branch, not ahead of chain classification.
Require the returned receipt transaction hash to equal the requested source
hash before inspecting its log. Add regression tests for an all-zero input with
empty uncle hash and for a syntactically valid receipt whose `transactionHash`
differs from the request.

### CR-002 — Critical — Catch-up advances past burns whose receipt lookup failed

**File/line:** `src/watcher/impl/bridge_catchup_watcher.cpp:261-285`,
`src/watcher/impl/bridge_catchup_watcher.cpp:288-310`,
`src/watcher/impl/bridge_catchup_watcher.cpp:361-362`,
`src/watcher/impl/bridge_catchup_watcher.cpp:479-510`

**Evidence:** A failed or unparseable receipt lookup is cached as
`std::nullopt`, and the corresponding burn log is skipped. An inconsistent
receipt or an unresolved block-wide log index is also skipped. Nevertheless,
`process_logs` returns `true`, which marks the chunk successful and advances
`from_block` beyond it. The retry guard only preserves the cursor when both log
queries fail or cannot be parsed; it does not preserve it when the log query
succeeds but its required receipt proof is temporarily unavailable.

**Impact:** A transient receipt RPC failure can permanently omit a historical
burn while the catch-up cursor records the containing range as complete. The
burn is not retried during that process lifetime, so users can be left without
the corresponding mint.

**Remediation:** Treat any matching log whose receipt-local ordinal cannot be
verified as a chunk failure/retry condition. Do not advance the cursor past
that chunk, and do not cache transient receipt failures across retries. Add a
test in which the log query succeeds, the first receipt lookup fails, the
second succeeds, and the cursor remains at the failed chunk until the burn is
processed.

### CR-003 — Critical — Replicated certificates are not required to use one canonical byte representation

**File/line:** `src/blockchain/Consensus.cpp:59-73`,
`src/blockchain/Consensus.cpp:1371-1390`,
`src/blockchain/Consensus.cpp:1453-1566`,
`src/blockchain/Consensus.cpp:1775-1818`,
`src/blockchain/Consensus.cpp:2390-2398`,
`src/blockchain/Consensus.cpp:2443-2453`,
`src/blockchain/Consensus.cpp:2659-2684`

**Evidence:** Locally submitted certificates are deterministically serialized
and byte equality defines replay versus conflict. The remote delta path,
however, only parses and semantically validates the incoming protobuf and then
stores/compares its original bytes. `ValidateCertificate` does not require the
wire bytes to equal deterministic serialization, does not validate or derive
the certificate timestamp, and `TallyVotes` silently ignores duplicate,
wrong-proposal, inactive-validator, and bad-signature votes when a valid quorum
remains. Protobuf unknown fields, field ordering, vote ordering, or appended
ignored votes can therefore produce distinct accepted bytes for the same
certificate semantics.

**Impact:** A peer can win the first-write race with a noncanonical but
semantically accepted representation. Honest deterministic replay then appears
as a conflict, and different nodes can retain different authoritative bytes
for the same slot. This undermines byte-identical replay and can be used for
persistent certificate-store divergence or denial of finality.

**Remediation:** Define and enforce a canonical certificate representation at
the replication boundary. Reject unknown fields and every invalid/duplicate
vote, sort votes by a canonical key, derive rather than trust redundant fields
such as timestamp and weights, deterministically serialize the normalized
certificate, and require the incoming bytes to match it before merge. Add
tests for unknown fields, changed timestamp, reordered votes, duplicate votes,
and an appended invalid vote.

### WR-001 — Warning — A temporarily unavailable registry causes permanent certificate loss

**File/line:** `src/blockchain/Consensus.cpp:2390-2397`,
`src/blockchain/Consensus.cpp:2476-2494`,
`src/blockchain/Consensus.cpp:2529-2544`,
`src/blockchain/Consensus.cpp:2618-2627`,
`src/crdt/impl/crdt_data_filter.cpp:136-159`,
`src/crdt/impl/crdt_datastore.cpp:917-964`

**Evidence:** `ValidateCertificate` returns `Check::Stalled` when the referenced
registry CID is not yet locally available. The element/callback path is
designed to preserve and journal stalled certificates, but the new atomic
delta filter requires `Check::Approve`; it returns `false` for `Stalled`.
`CRDTDataFilter` then removes both matching v2 elements before the delta is
merged, while the DAG node is still recorded as processed. The certificate
never reaches `CertificateReceived`, so `MarkStalled` and the recovery loop
cannot run.

**Impact:** Normal registry/certificate propagation reordering can make a
valid certificate disappear permanently on a lagging node, preventing that
node from converging or waking dependent transaction work.

**Remediation:** Separate terminal invalidity from missing dependencies in the
delta filter. Preserve an atomic, structurally valid pair when validation is
stalled and journal it for retry, or leave the source CID unprocessed so it is
retried after registry synchronization. Add a test where the registry is
initially unavailable, later becomes available, and the certificate is then
validated and delivered exactly once.

### WR-002 — Warning — Typed lookup maps every storage failure to `NotFound`

**File/line:** `src/blockchain/Consensus.cpp:3706-3711`,
`src/blockchain/Consensus.cpp:3748-3752`

**Evidence:** Both typed lookup entry points return
`CertificateStoreError::NotFound` for any `db_->Get` error. The underlying
database error model distinguishes `NOT_FOUND` from corruption, I/O failure,
shutdown, timeout, and other operational failures. Downstream consumers use
`NotFound` as the normal pending/missing-certificate condition.

**Impact:** Database corruption or an operational read failure is hidden as
ordinary certificate absence. Consumers can stall and retry instead of
surfacing an integrity/availability failure, contradicting the phase contract
that corruption must not be treated as absence.

**Remediation:** Map only the underlying `NOT_FOUND` condition to
`CertificateStoreError::NotFound`. Preserve non-absence errors with a dedicated
storage/availability error, or conservatively map corruption-class failures to
`IntegrityError`. Add injected-error tests that distinguish `NOT_FOUND`,
`CORRUPTION`, and `IO_ERROR`.

### WR-003 — Warning — The legacy namespace is gated only at startup, not during replication

**File/line:** `src/blockchain/Consensus.cpp:147-153`,
`src/blockchain/Consensus.cpp:2298-2335`,
`src/blockchain/Consensus.cpp:3656-3695`,
`src/blockchain/Consensus.hpp:664-669`,
`src/crdt/impl/crdt_data_filter.cpp:167-205`

**Evidence:** Startup scans `/cert/` and rejects any key outside the exact v2
slot/index schemas. At runtime, filters are registered only for
`/cert/v2/(slot|tx)/...`; unmatched elements are accepted by the CRDT filter's
default policy. A cleanly started node can therefore replicate a legacy
`/cert/<hash>` or another non-v2 certificate key without rejection. The key
remains silently mixed into live state and only causes startup refusal after a
restart.

**Impact:** A legacy or malicious peer can poison an otherwise compatible
node's datastore and turn the next restart into a persistent startup failure.

**Remediation:** Register a guard for the entire `/cert/` namespace and reject
or quarantine every key that is not part of an atomic, exact v2 pair. Add a
runtime replication test showing that a legacy certificate arriving after
startup never becomes visible and does not poison subsequent startup.

### WR-004 — Warning — The public `MintFunds` default creates a noncanonical, unfinalizable mint

**File/line:** `src/account/TransactionManager.cpp:584-599`,
`src/account/TransactionManager.cpp:668-705`,
`src/account/MintTransactionV2.cpp:231-247`

**Evidence:** `MintFunds` converts an empty chain ID to the literal `"public"`,
reserves the burn UTXO, constructs the transaction, and returns/enqueues it.
The new canonical slot preimage accepts only decimal chain IDs, so `"public"`
always fails slot derivation. The constructor path does not call
`GetSlotPreimage`, so the error is discovered only later by consensus, after
the API has reported success and the reservation has been made.

**Impact:** Existing callers that rely on the empty-chain default receive a
successful mint ID for a transaction that cannot enter a canonical slot, with
its burn UTXO left reserved until some separate cleanup path runs.

**Remediation:** Require and validate a canonical numeric source-chain ID at
the start of `MintFunds`, before any database or UTXO mutation. Remove the
`"public"` sentinel or translate it to an actual configured numeric chain ID.
Add an API-level test for empty and nonnumeric chain IDs that asserts failure
and no reservation/enqueue side effects.

### IN-001 — Info — Consumer tests do not exercise the consumers named by the phase

**File/line:** `test/src/blockchain/certificate_compatibility_test.cpp:345-363`,
`test/src/account/transaction_manager_pending_lifecycle_test.cpp:107-116`

**Evidence:** `PreviousNonceAndProducerHashConsumersResolveWinner` invokes
`GetCertificateBySubjectHash` twice directly; it does not execute the previous
nonce or producer-hash paths in `TransactionManager` or
`GeniusInputValidator`. Likewise,
`CertificateLookupErrorsSeparatePendingFromCorruption` compares enum values
and messages without injecting lookup outcomes into a consumer.

**Impact:** Regressions in the actual consumer branches, including the storage
error collapse above, can pass the Phase 09 suite.

**Remediation:** Replace or supplement these contract assertions with tests
that execute each real consumer against winning, missing, corrupt, and
operational-error certificate lookup results.

## Verification

- `git diff --check c852bf9f^..HEAD` passed.
- Built the eight focused targets:
  `consensus_slot_key_test`, `bridge_event_identity_test`,
  `bridge_relayer_test`, `public_chain_mint_validation_test`,
  `consensus_certificate_store_test`, `certificate_compatibility_test`,
  `transaction_manager_pending_lifecycle_test`, and
  `consensus_pending_lifecycle_test`.
- All eight focused test binaries passed.
- Existing unrelated worktree changes were not modified.

## Assessment

The phase should not be accepted as complete while CR-001 through CR-003
remain. The focused tests pass, but they do not cover the empty-source proof
bypass, receipt-fetch retry semantics, or canonical remote certificate wire
representation.
