# Phase 11: Convergent Certificate Consumption & Mint Recovery - Context

**Gathered:** 2026-08-21
**Status:** Ready for planning

<domain>
## Phase Boundary

Make every durable, exact slot-certificate delivery converge through the existing transaction lifecycle, and ensure the certified winning Mint's UTXO effects and existing bridge-executed marker recover safely across duplicates and restart. This phase must not add a second certificate-authority record or a new finality journal.

</domain>

<decisions>
## Implementation Decisions

### Certificate consumption

- **D-01:** `/cert/<canonical-slot>` remains the only certificate authority. There is no new local certificate-acceptance journal: the authoritative CRDT certificate plus existing transaction and UTXO persistence are the durable source of truth.
- **D-02:** Local publication, PubSub receipt, CRDT synchronization, and restart recovery must converge on the existing transaction confirmation/application lifecycle. Duplicate processing of the same exact winning transaction is a no-op at the Mint-effect boundary.
- **D-03:** Preserve Phase 10 exact winner binding at consumption: an occupied slot certificate may process only the transaction embedded in and exactly certified by that certificate; it can never apply a same-slot losing Mint.

### Certificate-first and restart recovery

- **D-04:** On certificate-first delivery, obtain the winning transaction from CRDT first. If it is not yet locally available, use only the exact validated transaction embedded in that accepted certificate, then enter the same transaction lifecycle.
- **D-05:** A later transaction delivery or startup CRDT scan must find the accepted canonical-slot certificate and converge to the same confirmed/application result. Certificate-first and transaction-first ordering must not create separate completion paths.
- **D-06:** Temporary local failures during certified Mint application remain retryable through recovery. They do not turn a valid certified Mint into a terminal failure or require an operator-only path.

### Mint completion ordering

- **D-07:** Apply Mint UTXO effects idempotently before persisting the existing bridge-executed marker. The marker is the durable completion barrier, not pre-application permission.
- **D-08:** If the executed-marker write fails after idempotent UTXO application, retain the UTXO effects and retry completion. Missing marker state is recovery evidence; do not silently declare complete or roll back applied UTXOs.

### the agent's Discretion

- Choose the smallest recovery trigger, persistence check, and test seams that preserve the existing transaction lifecycle and the locked ordering above.
- Reuse existing certificate-work recovery and transaction/UTXO idempotence where they satisfy the contract; add state only if code research proves an unavoidable atomicity gap.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone contract

- `.planning/PROJECT.md` — core value: one external burn yields one authority and one Mint effect; no new bridge-special finality record.
- `.planning/REQUIREMENTS.md` — Phase 11 requirements `CERT-05`, `MINT-01`, and `MINT-02` plus out-of-scope boundaries.
- `.planning/ROADMAP.md` §Phase 11 — locked goal and four success criteria.

### Prior-phase protocol decisions

- `.planning/phases/08-canonical-slot-certificate-binding/08-CONTEXT.md` — canonical Mint slot and exact certificate/proposal binding.
- `.planning/phases/09-durable-one-vote-finality/09-CONTEXT.md` — durable vote-lock release only after accepted same-slot certificate finality.
- `.planning/phases/10-authoritative-slot-certificate-publication/10-CONTEXT.md` — slot-only certificate authority, selected publisher, and CRDT recovery decisions.
- `.planning/phases/10-authoritative-slot-certificate-publication/10-VERIFICATION.md` — verified Phase 10 authority and exact winner-binding evidence.

### Existing consumption and recovery code

- `src/blockchain/Consensus.cpp` — certificate work journal, durable readback, `ProcessCommittedCertificate`, and recovery dispatch.
- `src/account/TransactionManager.cpp` — transaction-first verification, certificate-first `OnConsensusCertificate`, `ChangeTransactionState`, Mint parsing, and bridge-executed persistence.
- `src/account/UTXOManager.cpp` — transaction-output keyed idempotent UTXO insertion and durable UTXO storage.
- `test/src/account/transaction_manager_certificate_fallback_test.cpp` — current canonical-slot transaction-consumer fixtures and competing-Mint binding coverage.
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — durable certificate callback/recovery fixture and `MemorySecureStorage` setup.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets

- `ConsensusManager::RecoverPendingCertificateWork` and its certificate work journal provide the existing durable-readback/retry boundary after CRDT callbacks.
- `TransactionManager::FetchAndProcessTransaction` already models transaction-first `VERIFYING` versus exact-certificate `CONFIRMED` state.
- `TransactionManager::OnConsensusCertificate` already bridges certificate-first delivery into transaction confirmation and can deserialize a certificate-embedded transaction.
- `UTXOManager::PutUTXO` is idempotent by transaction output outpoint, allowing incomplete Mint output application to be replayed safely.

### Established Patterns

- CRDT callbacks are pre-commit notifications; durable readback/recovery—not callback provenance—drives final processing.
- Certificate authority is slot-keyed, while the exact transaction/proposal binding is checked at every consumption boundary.
- `outcome::result` and existing recovery journals express retryable failures without a new dependency.

### Integration Points

- `ConsensusManager::ProcessCommittedCertificate` dispatches accepted certificates to the nonce-subject transaction handler and determines whether certificate work is done or retryable.
- `TransactionManager::ChangeTransactionState(CONFIRMED)` currently persists the bridge-executed marker before `ParseTransaction`; Phase 11 must make the locked idempotent-apply-then-marker ordering recoverable.
- Startup transaction scanning and CRDT transaction callbacks must converge with certificate-first handling rather than bypass it.

</code_context>

<specifics>
## Specific Ideas

- “The mint is already there”: use the existing transaction and UTXO state as the recovery truth rather than duplicating certificate acceptance state.
- A later transaction already searches for its certificate; preserve that behavior and prove both arrival orders converge.

</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)

- `bridge-startup-wiring-mock-rpc.md` — weak keyword-only match (0.2); bridge startup/RPC wiring is unrelated to certificate consumption and Mint recovery.

</deferred>

---

*Phase: 11-convergent-certificate-consumption-mint-recovery*
*Context gathered: 2026-08-21*
