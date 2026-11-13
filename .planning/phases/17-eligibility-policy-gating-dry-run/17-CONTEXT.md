# Phase 17: Eligibility, Policy & Gating (Dry-Run) - Context

**Gathered:** 2026-10-16
**Status:** Ready for planning

<domain>
## Phase Boundary

The pruning policy evaluator decides erasability on live data **without erasing anything** — genuinely-spent ∧ spend-recency-window eligibility, the multi-element block-parse gate, the own-frontier survival fix, Light-only fail-closed gating with the tx-prefix allowlist, the pruning config surface (default off), and the per-tx pruned-marker design backing future "history pruned" answers. The two correctness dependencies of the first real prune (block gate, own-frontier fix) land and are proven HERE, before any production erasure exists (Phase 18).

Covers requirements PRUNE-01, PRUNE-03, GATE-01, GATE-04, GATE-06. The TxPruner service, first real erase, and marker *writing* are Phase 18; PRUNED answer surfacing to queries is Phase 19 (SURV-02).

</domain>

<decisions>
## Implementation Decisions

### Locked carry-forwards from milestone + Phase 16 (do not re-litigate)
- **D-C1:** Physical local RocksDB delete only — never the CRDT tombstone path; one atomic WriteBatch per erase (Phase 16 substrate: `CrdtSet::StageEraseElement`, `GraphsyncDAGSyncer::StageBlockRemoval`, `GlobalDB::EraseEntry`, `TransactionManager::EraseTransactionEntry` — all landed, zero production callers)
- **D-C2:** Genuine spend = `IsOutPointGenuinelyConsumed` (`UTXOManager.hpp:397`) — synthesized zero-amount CONSUMED tombstones are not spend evidence
- **D-C3:** Block gate parses the delta block itself at evaluation time (`GetNodeWithoutRequest` + `GetDeltaFromNode`, `GetLocalDeltaKeyValues` pattern, `crdt_datastore.cpp:2280-2302`) — never "what this node applied"
- **D-C4:** Seal markers never erased; `maxHeight` comes from the existing `GetCRDTHeadList()` — no new API
- **D-C5:** Config knobs in `sgns_config.json` beside `node_type` (read in `LoadSgnsConfig`), default off, resolved values INFO-logged; absent keys behave exactly as today
- **D-C6:** The evaluator runs on committed state only — never from CRDT element callbacks (they fire pre-batch-commit; proven repo invariant)
- **D-C7:** Allowlist prefix from the verified constant: `TRANSACTION_BASE_FORMAT = "/bc-%hu/"` (`TransactionManager.hpp:433`) — `/chain/<net>/tx` naming is stale; do not use it

### Own-frontier survival mechanism (discussed — owner decided)
- **D-01:** Cert-slot previous_hash generalization. Resolve the outgoing `previous_hash`/nonce chain from certificate data (certificates are never pruned — GATE-02 — and embed the winning tx hash), generalizing the existing `GetRegisteredOutgoingPreviousHash` precedent (`TransactionManager.cpp:1688-1726`). Own-nonce exemption (last-K anchor retention) is a FALLBACK only if planning proves cert-slot too invasive — K-magic-number fragility and burst re-exposure are the reasons it is not primary.

### Finality window definition (discussed — owner challenged the research default; constraint locked)
- **D-02:** **The window measures the recency of the SPEND, never the age of the transaction's creation, and never wall-clock.** Rationale (owner's counterexample, verified against source): a transaction's own priority/depth is set at its creation; spending an ancient UTXO yesterday leaves the ancient tx's depth huge, so a creation-age window would instantly expose a just-spent tx — exactly backwards. Additionally, `spent_by_txid` is written `std::nullopt`/`reset()` today (`UTXOManager.cpp:179, :390`) — the consuming tx identity is not recorded.
  - **Candidate A (preferred — researcher verifies):** the CONSUMED UTXO entry is itself a GlobalDB put at spend time, so it carries its own CRDT priority (`/crdt/k/<utxo-key>/p`). `spend-recency = maxHeight − priority(consumed entry) ≥ N`. Zero schema change; the signal is exactly the spend event. Research MUST verify consumed entries receive per-key priority and that the consumed entry's key is enumerable for the evaluator.
  - **Candidate B (fallback):** populate `spent_by_txid` at `ConsumeUTXOs` and require the consuming transaction's depth ≥ N. Explicit and auditable; adopting B amends the REQUIREMENTS.md out-of-scope row "populating spent_epoch/spent_by_txid" — amend that row ONLY if A fails verification.
  - `N` is a config knob, required when pruning is enabled; no default value (default-off makes it unnecessary).

### Dry-run reporting surface (discussed — owner decided)
- **D-03:** The evaluator is a pure structured result over candidate enumeration — eligible/ineligible sets with per-item reasons — unit-testable against fixtures (the SC-1 exact-set assertion targets this result). Production dry-run mode emits a per-cycle INFO summary derived from it. NO queryable report surface this phase; operator introspection is Phase 18's RECL-04.

### Pruned-marker design (discussed — owner decided)
- **D-04:** Per-tx marker records under a node-local `/prune/` prefix in the **same shared RocksDB** (the `CRDTWorkJournal` `/crdt/work/` precedent: direct datastore writes, never broadcast, same batch discipline). Record = tx path + erased CID. The format, key shape, and lookup semantics (PRUNED vs NOT_FOUND) are DESIGNED in this phase; the write happens in Phase 18's erase flow; the query-surface answer lands in Phase 19 (SURV-02).

### Claude's Discretion
- Evaluator module placement (standalone evaluator class in `src/transaction/` vs TransactionManager-owned section — follow the one-owner-per-domain rule; TxPruner service itself is Phase 18)
- Exact config key names (`pruning.*` in sgns_config.json) and the INFO log line format
- Fixture construction for the exact-set test (fresh temp DB per D-C6 discipline from 16)

### Folded Todos
None — the two bridge-todo matches (bridge-race fixture, bridge startup wiring) are the same stopword noise reviewed in Phase 16; bridge scope, left pending.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone research (file:line-verified)
- `.planning/research/SUMMARY.md` — converged architecture; Phase 17 implications ("Eligibility, policy & gating" section); research flags: Phase 17 warrants deeper research (window definition, own-frontier mechanism, lock protocol)
- `.planning/research/ARCHITECTURE.md` — eligibility inputs (genuine-consumed discriminator, priority/maxHeight, cert-slot resolution precedent), block-parse pattern, restart hazards
- `.planning/research/PITFALLS.md` — window-in-finality-epochs-never-wall-clock rationale, nonce-anchor erasure pitfall, gate-on-enum discipline

### Prior phase (landed substrate this phase builds on)
- `.planning/phases/16-erasure-primitives-bookkeeping-hardening/16-CONTEXT.md` — locked D-C1..C6 + D-01..03 (erase API shape decisions)
- `.planning/phases/16-erasure-primitives-bookkeeping-hardening/16-RESEARCH.md` — verified signatures: `StageEraseElement`, `StageBlockRemoval`, `EraseEntry` guards, `ResolveTransactionPathAndCID`
- `.planning/phases/16-erasure-primitives-bookkeeping-hardening/16-HUMAN-UAT.md` — tooling environment facts (clang tools runnable; see also repo memory)

### Requirements & roadmap
- `.planning/REQUIREMENTS.md` — PRUNE-01, PRUNE-03, GATE-01, GATE-04, GATE-06; note the conditionally-amendable out-of-scope row (spent_by_txid) per D-02 Candidate B
- `.planning/ROADMAP.md` — Phase 17 entry: goal + 5 success criteria

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets (all landed in Phase 16, zero production callers)
- `GlobalDB::EraseEntry(key, cid)` — the guarded compose the evaluator will eventually drive (Phase 18); its four guard predicates are the in-use checks
- `TransactionManager::EraseTransactionEntry` / `ResolveTransactionPathAndCID` — network-consistent path+CID resolution; the evaluator's candidate enumeration reuses the same map
- `GetCRDTHeadList()` → maxHeight for depth computation (no new API)

### Established Patterns
- `GetRegisteredOutgoingPreviousHash` (`TransactionManager.cpp:1688-1726`) — the cert-slot previous_hash resolution precedent D-01 generalizes
- `GetLocalDeltaKeyValues` (`crdt_datastore.cpp:2280-2302`) — block-parse pattern for the multi-element gate
- `LoadSgnsConfig` — config-read surface for the new knobs (config-wins precedence, safe defaults, INFO-log resolved values)
- Fresh temp-DB fixtures + three-part assertions + negative controls (Phase 16 test discipline)

### Integration Points
- `UTXOManager` CONSUMED entries — the spend-recency signal source (Candidate A: their per-key priority)
- `IsOutPointGenuinelyConsumed` (`UTXOManager.hpp:397`) — the spend discriminator
- `NodeType` enum (fail-closed gate target) and `TRANSACTION_BASE_FORMAT` (allowlist)

</code_context>

<specifics>
## Specific Ideas

- Owner's exact framing of the window problem: "we could have just spent a really old UTXO that has a high depth" — the window exists to protect recent SPENDS and their conflict window, not old creations
- Marker placement: owner confirmed RocksDB — same shared DB, node-local prefix, never broadcast

</specifics>

<deferred>
## Deferred Ideas

- **"Extra final prune"** (owner, 2026-10-16): a future prune capability distinct from the spent-based sliding window (retirement/whole-history-style) — new capability, own phase if pursued; NOT folded into v1.2 phases
- **Cert-tightened window** (require certificate presence per candidate) — deferred until a concrete need appears (research recommendation, owner concurred via A-first choice)
- **Queryable dry-run/operator report** — Phase 18 (RECL-04)
- **Own-nonce exemption** — fallback only (D-01)

### Reviewed Todos (not folded)
- `bridge-race-not-all-11-mint-within-window` (P1), `bridge-startup-wiring-mock-rpc` (P1) — stopword-noise matches, bridge scope, remain pending

</deferred>

---

*Phase: 17-Eligibility, Policy & Gating (Dry-Run)*
*Context gathered: 2026-10-16*
