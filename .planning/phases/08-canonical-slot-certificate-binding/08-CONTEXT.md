# Phase 8: Canonical Slot & Certificate Binding - Context

**Gathered:** 2026-08-20
**Status:** Ready for planning

<domain>
## Phase Boundary

Make competing verified `MintTransactionV2` proposals resolve to their existing common canonical slot, and define certificate acceptance so a stored key, slot, certificate payload, and exact winning proposal must agree before finality can proceed.

This phase establishes identity and binding only. Bounded winner timing and the durable vote lock belong to Phase 9; authoritative certificate publication and hash-only consumer migration belong to Phase 10; certificate convergence and exactly-once mint recovery belong to Phase 11.

</domain>

<decisions>
## Implementation Decisions

### Canonical mint slot
- **D-01:** Keep `MintTransactionV2::GetSlotID()` as the canonical slot calculation. Its verified chain, token, source transaction, amount, and destination are intentional identity facts; do not remove amount or destination.
- **D-02:** Proposer account, proposal nonce, and other proposal-envelope differences must not change the slot. Competing mint proposals for the same verified burn must therefore contend in the same finality domain.

### Certificate-to-proposal binding
- **D-03:** The certificate remains bound to its exact winning proposal. A matching slot does not permit a certificate to stand in for a different proposal.
- **D-04:** Certificate acceptance must verify agreement among canonical slot, eventual slot-keyed storage key, certificate payload, and embedded winning proposal before it can finalize or mint.
- **D-05:** Any slot/key/payload/proposal mismatch fails closed: it must not finalize, mint, overwrite a certificate, or unlock local slot state. Do not use a local delivery-source flag as evidence of authority.

### Phase boundaries
- **D-06:** Do not change the Mint slot formula merely to solve publication or vote-lifecycle behavior. Those behaviors are intentionally sequenced into Phases 9-11.

### the agent's Discretion
- Choose the smallest validation helpers and test seams that fit the existing C++17 consensus patterns, provided the locked identity and failure behavior above are preserved.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone contract
- `.planning/PROJECT.md` — v3.0 core value, protocol constraints, and the decision to rebuild from `develop` without a bridge-specific record.
- `.planning/REQUIREMENTS.md` — authoritative Phase 8 requirements `SLOT-01`, `SLOT-02`, and `SLOT-03`.
- `.planning/ROADMAP.md` § Phase 8 — fixed phase goal, dependencies, and success criteria.
- `.planning/research/SUMMARY.md` § Decision Update — supersedes the earlier bridge-specific research proposal; confirms generic slot-keyed authority and the existing Mint slot semantics.
- `.planning/research/ARCHITECTURE.md` § Decision Update — binding and publication direction that later phases must follow.

### Existing implementation
- `src/account/MintTransactionV2.hpp` and `src/account/MintTransactionV2.cpp` — current canonical Mint slot contract and its field encoding.
- `src/blockchain/Consensus.hpp` and `src/blockchain/Consensus.cpp` — proposal slot bookkeeping, certificate construction/acceptance, and the current subject-hash certificate storage path being constrained by this phase.
- `src/account/TransactionManager.cpp` — embedded transaction slot dispatch and downstream certificate lookup context.
- `src/account/GeniusInputValidator.cpp` — existing certificate-dependent witness validation; hash-only lookup migration is deferred to Phase 10.

### Test patterns
- `test/src/blockchain/consensus_pending_lifecycle_test.cpp` — existing `ConsensusManager` test-access seam and CRDT fixture pattern for focused consensus lifecycle tests.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `MintTransactionV2::GetSlotID()` in `src/account/MintTransactionV2.cpp`: already encodes the verified Mint facts needed for same-burn proposal competition.
- `ConsensusManager::GetSlotKey`, `ContinueProposalAfterSubject`, and `ClearProposalSlot` in `src/blockchain/Consensus.cpp`: existing slot arbitration hooks that Phase 8 must constrain without introducing vote durability yet.
- `ConsensusManager` certificate serialization and verification path: the natural seam for rejecting slot/key/payload/proposal disagreement before downstream finality effects.

### Established Patterns
- C++17 with `outcome::result<T>` and fail-closed error propagation in consensus code.
- CRDT fixture plus narrowly scoped `ConsensusManager` test-access classes for internal-state contract tests.

### Integration Points
- `ConsensusManager::SubmitCertificate`, `HandleCertificate`, and certificate lookup helpers currently use `/cert/<subject-hash>`; Phase 8 defines the validation invariants that Phase 10 will apply when moving authority to `/cert/<slot-id>`.
- `TransactionManager` dispatches embedded Mint transactions through `GetSlotID()`, linking bridge mint construction to consensus slot competition.

</code_context>

<specifics>
## Specific Ideas

The user explicitly confirmed that amount and destination are immutable, independently verified burn facts in this flow. The defect is not the Mint slot formula; it is the later lifecycle that allowed a late proposal to recreate a cleared slot and obtain another vote.

</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)
- `Bridge Startup Wiring + Mock RPC Endpoints` (`.planning/todos/pending/bridge-startup-wiring-mock-rpc.md`) — low-confidence keyword overlap only; startup wiring and mock RPC endpoints are outside Phase 8's canonical-slot and binding boundary.

</deferred>

---

*Phase: 8-Canonical Slot & Certificate Binding*
*Context gathered: 2026-08-20*
