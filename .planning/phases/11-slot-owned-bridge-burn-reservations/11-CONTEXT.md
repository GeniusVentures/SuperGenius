# Phase 11: Slot-Owned Bridge Burn Reservations - Context

**Gathered:** 2026-07-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace proposal-owned bridge-burn reservations with one durable node-local reservation lifecycle owned by the canonical mint slot and exact burn outpoint. Proposal validation remains side-effect-free; successful consensus admission durably reserves the burn, all valid contenders share that reservation, certificate observation makes it irrevocably unavailable before application or cleanup, and an uncertified reservation releases only after deterministic proof that the complete slot is safely abandoned. The complete 11-node race proof remains Phase 12, and bridge startup/mock-RPC infrastructure is outside this phase.

</domain>

<decisions>
## Implementation Decisions

### Reservation durability
- **D-01:** Slot-owned burn reservations are persisted directly and restored across node restart. There must be no restart interval in which a protected burn appears ready.
- **D-02:** Reservations are durable node-local safety state. They are never replicated through CRDT; every validator independently establishes its own reservation after admitting a valid proposal, while the certificate remains the shared consensus authority.
- **D-03:** Startup reconciles each reservation against the authoritative slot certificate and durable vote state before admitting new proposal work. Missing ephemeral candidate state is normal and must not itself fail startup or release the burn.
- **D-04:** Malformed reservation records or durable contradictions between reservation, certificate, burn outpoint, and vote state fail closed. Ordinary absence of ephemeral candidates is not a contradiction.
- **D-05:** Consensus admission cannot complete until the slot reservation is durable. Reservation persistence failure rejects or retries admission without adding an active candidate or publishing a vote.

### Competing proposal ownership
- **D-06:** The canonical slot is the sole reservation owner. Every semantically valid contender for the same canonical burn joins the existing reservation; proposer, account, nonce, candidate hash, and current-best status never own or transfer it.
- **D-07:** Proposal rejection and cleanup remove only proposal-local state. They never directly release, replace, or reacquire the burn reservation.
- **D-08:** A reservation binds the canonical slot ID to the exact canonical burn outpoint: source chain, burn transaction hash, and receipt-local log position. Candidate-controlled fields are excluded from ownership.
- **D-09:** Reservation uniqueness is node-wide across all local accounts and transaction-manager paths. Different local representations of the same burn cannot create independent reservation lifecycles.

### Finality and failed consumption
- **D-10:** First observation of an authoritative certificate durably transitions the reservation to `FinalizedPendingApplication`, making the burn irrevocably unavailable before winning-mint application or proposal cleanup.
- **D-11:** Physical bridge-UTXO consumption remains atomic with applying the certified mint outputs and its canonical application record. Certificate observation must not remove the input in a separate partially applied operation.
- **D-12:** A transient winning-mint application failure keeps the exact certified winner pending and automatically retryable across restart. The burn is never released and no alternate candidate may be selected or applied.
- **D-13:** An irreconcilable contradiction between the certified winner and durable UTXO/application state enters a durable safety-error state. Preserve certificate finality, never release or remint the burn, stop retries that cannot succeed, and emit critical diagnostics.
- **D-14:** If certificate authority is established but the finalized reservation transition cannot be persisted, transaction application and cleanup must wait. Recovery must first make the burn durably unavailable.

### Slot abandonment
- **D-15:** An uncertified reservation releases automatically at a deterministic safe-release horizon, not a generic reservation TTL and not by operator-only cleanup.
- **D-16:** Safe release requires no authoritative certificate and expiration of every candidate and durable-vote certificate-acceptance horizon. Candidate deadlines and vote horizons provide bounded proof; missing ephemeral objects are not awaited forever.
- **D-17:** After safe abandonment, a later valid proposal for the same burn may establish a fresh reservation generation because previous signatures are already unusable and no certificate exists.
- **D-18:** Release is an atomic conditional transition over the current reservation identity. Concurrent valid candidate admission preserves or renews the reservation, certificate finality wins over cleanup, and stale cleanup cannot affect a newer generation.
- **D-19:** Safe release deletes the durable reservation record completely. A later reservation uses a fresh unique generation identity rather than retaining abandoned history.

### the agent's Discretion
- Choose the durable record schema, key namespace, encoding, and component boundaries, provided reservation uniqueness is node-wide and storage remains node-local rather than CRDT-replicated.
- Choose the atomic batch/CAS mechanism and fresh generation-token representation that enforce D-05, D-10, D-14, and D-18 without retaining released history.
- Choose recovery scheduling, retry backoff, error types, logs, and metrics consistent with fail-closed safety and bounded automatic reconciliation.
- Choose how consensus admission, proposal cleanup, finalization, and `UTXOManager` communicate reservation lifecycle transitions without making validation stateful.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone scope and normative requirements
- `.planning/ROADMAP.md` — Phase 11 goal, dependency, success criteria, and the Phase 12 verification boundary.
- `.planning/REQUIREMENTS.md` — Normative BURN-01 through BURN-05 lifecycle requirements.
- `.planning/PROJECT.md` — Core safety value, observed double-certificate failure, bridge identity constraints, and milestone boundaries.

### Prior phase contracts
- `.planning/phases/09-canonical-slot-and-certificate-storage/09-CONTEXT.md` — Canonical burn outpoint and slot identity, authoritative slot certificate, transaction-hash compatibility index, and atomic mint application decisions.
- `.planning/phases/10-durable-vote-lock-and-finalization-state-machine/10-CONTEXT.md` — Durable vote horizon, candidate window, shared finalization path, finality-before-cleanup, and exact-winner retry decisions.
- `.planning/phases/10-durable-vote-lock-and-finalization-state-machine/10-VERIFICATION.md` — Verified Phase 10 lifecycle and explicit boundary delegated to Phase 11.

### Reservation and mint application code
- `src/account/UTXOManager.hpp` — Current transient `local_reservations_` model, reservation APIs, burn outpoint access, and atomic mint-effect interface.
- `src/account/UTXOManager.cpp` — Current reserve/release semantics and `ApplyMintEffectsAtomically`, including atomic burn consumption and application-record persistence.
- `src/account/TransactionManager.hpp` — Bridge burn state and consensus validation seams.
- `src/account/TransactionManager.cpp` — Current pre-consensus `MintFunds` reservation, proposal cleanup release, certificate handler, validation path, and certified mint application.
- `src/account/proto/SGTransaction.proto` — Bridge input wire identity and receipt-local output index used by the canonical burn outpoint.

### Consensus lifecycle code
- `src/blockchain/Consensus.hpp` — Subject, certificate, proposal-cleanup, and slot-key handler APIs plus finalized-slot lifecycle state.
- `src/blockchain/Consensus.cpp` — Proposal admission, durable vote horizon, `FinalizeSlot`, `ClearProposalSlot`, recovery, and all certificate ingress paths.
- `src/blockchain/ConsensusStateStore.hpp` — Existing node-local durable consensus record patterns and typed recovery contract.
- `src/blockchain/ConsensusStateStore.cpp` — Direct RocksDB persistence, strict reads, batching, and restart scans available as design precedent.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `MintTransactionV2` canonical slot derivation already maps every proposal for one chain/transaction/receipt-log burn to one fixed slot ID.
- `UTXOManager::ApplyMintEffectsAtomically` already commits the winning outputs, canonical application record, and bridge-input consumption as one durable operation; Phase 11 should preserve this boundary.
- Phase 10's `ConsensusStateStore` provides strict direct-RocksDB record, scan, and batch patterns for validator-private state that must not replicate through CRDT.
- Phase 10's durable vote horizon and finalization processing marker provide the evidence needed for bounded abandonment and exact-winner recovery.

### Established Patterns
- Current `UTXOManager::local_reservations_` is explicitly transient and maps an outpoint to a transaction-style reservation ID; it is not sufficient as Phase 11 authority.
- Current `TransactionManager::MintFunds` inserts/reserves the synthetic bridge UTXO before consensus admission and rolls it back by transaction hash on failure; Phase 11 must move ownership to the post-validation admission boundary.
- Proposal cleanup is registered separately from certificate application, enabling proposal-local cleanup to stop touching slot-owned reservation state.
- Certificate authority is established before application and cleanup; application failure leaves exact-winner work pending rather than rolling back finality.

### Integration Points
- Consensus proposal admission needs a subject-specific reservation transition after validation succeeds but before the candidate becomes active or vote publication becomes possible.
- `TransactionManager` registration must expose slot-level reserve/finalize/abandon operations without adding side effects to `ValidateUTXOParametersForConsensus`.
- `FinalizeSlot` must persist `FinalizedPendingApplication` before invoking the certified transaction handler or clearing proposals.
- `ClearProposalSlot` and proposal cleanup handlers must be unable to release a reservation directly.
- Startup recovery must load and reconcile reservation records before consensus subscriptions, proposal admission, or vote replay.
- The retirement/recovery path must conditionally delete an uncertified reservation only after all candidate and vote horizons expire and a final certificate lookup remains absent.

</code_context>

<specifics>
## Specific Ideas

- A burn reservation is a local safety lock, not consensus finality and not replicated network state.
- `Ready -> Reserved -> FinalizedPendingApplication -> Consumed` is the successful lifecycle; `Reserved -> deleted` is permitted only for proven whole-slot abandonment.
- A finalized safety error is intentionally permanent for that burn: consensus is trusted over local application state, but local contradictions must never create a second mint.
- Released records leave no durable history, so every new reservation must carry a fresh unique identity that defeats stale cleanup without relying on an old generation record.

</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)
- `bridge-race-not-all-11-mint-within-window.md` — the complete multi-node race and timing proof belongs to Phase 12 TEST-01 through TEST-06; Phase 11 supplies the reservation semantics it will exercise.
- `bridge-startup-wiring-mock-rpc.md` — bridge startup and mock-RPC infrastructure are not part of slot-owned reservation semantics and remain deferred.

</deferred>

---

*Phase: 11-Slot-Owned Bridge Burn Reservations*
*Context gathered: 2026-07-28*
