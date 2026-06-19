---
phase: 06
phase_name: network-voting-weight-classes-tier-2
discussed: 2026-06-19
branch: explore_fix_chain_rpc_json
decisions: 10
status: locked
---

# Phase 06 — Context & User Decisions

> Generated from `/gsd:discuss-phase 6 --assumptions` — user's design replaces the original
> cohort-membership model with a cryptographic slot-based RPC-hash voting system.

## Architecture Decision Records

### D-01: Slot-Based ConsensusVote Extension (LOCKED)

`ConsensusVote` proto is extended with 3 `bytes32` fields:

```
slot_0_hash: bytes32  // DIRECT_API endpoint hash (0x0 if N/A)
slot_1_hash: bytes32  // PUBLIC endpoint hash
slot_2_hash: bytes32  // PUBLIC endpoint hash
```

Each slot carries an RPC URL hash proving which endpoint the validator checked.

**Rationale:** Cryptographically binds a vote to the RPC endpoint used, enabling peers to
detect fabricated votes (the ROADMAP threat model concern). No separate hash registration
in the validator registry — the hash lives in the vote only (see D-05).

### D-02: Slot 0 — DIRECT_API 50% Weight (LOCKED)

- Only validators with a paid/API-key RPC endpoint can fill slot 0.
- 1 valid hash suffices (no duplication requirement).
- Slot 0 weight contribution: **50% × sum(reputation of all slot-0 voters)**.
- Reputation = existing `ValidatorEntry.weight` field (accumulated via consensus participation).

**Rationale:** DIRECT_API nodes are pre-authorized (API key), so the trust signal is their
reputation accumulation, not RPC endpoint agreement. A single valid hash is enough.

### D-03: Slots 1-2 — PUBLIC 25% Weight with Hash Deduplication (LOCKED)

Per-slot algorithm:
1. Collect all votes for the slot.
2. Group votes by their `slot_N_hash` value.
3. **Discard groups with fewer than 2 distinct validators** (solo hashes → eliminated).
4. Sum the reputation of all validators in the remaining (qualifying) groups.
5. Slot weight contribution: **25% × summed reputation**.

Example for slot 1 with total slot-1 reputation pool = 1000:
- Hash `0x00`: 3 validators, combined rep = 150 → qualifies (≥2) → adds 150
- Hash `0x01`: 20 validators, combined rep = 500 → qualifies → adds 500
- Hash `0x02`: 1 validator, rep = 50 → SOLO → discarded
- Qualifying sum = 650; slot weight = 650 × 0.25 = 162.5

**Rationale:** Prevents a single PUBLIC RPC endpoint from becoming a capture point.
Requiring ≥2 validators to independently check the same endpoint means fabricating
a PUBLIC slot requires either collusion across distinct nodes or compromising multiple
independent RPC credentials.

### D-04: No Hash Registration in Registry (LOCKED)

The RPC URL hash is NOT stored in `ValidatorEntry` or any registry field. It exists
only in `ConsensusVote`. A validator's reputation (accumulated `weight`) carries the
trust signal; the hash proves which endpoint was checked for THIS vote.

**Rationale:** Keeping the hash in the vote only avoids a registration-time lying-about-hashes
vector and a registry schema change. The hash is verified per-message by slot deduplication
rules — a validator can't inflate its contribution by fabricating a hash because the slot
deduplication algorithm only counts hash groups with ≥2 distinct validators.

### D-05: Abstention on Invalid RPC (LOCKED)

A node that can't produce a valid RPC check for any endpoint does NOT vote. It marks the
transaction as seen/invalid instead. This is fail-closed — a failing RPC endpoint doesn't
become a silent approve.

### D-06: Full Consensus — 2 of 3 Slots (LOCKED)

A certificate is produced when **≥2 of 3 slots pass**. A slot "passes" needs clarification
from the plan/research step (see Open Design Question 1 below), but the 2/3 rule is locked.

### D-07: Reputation = Existing Registry Weight (LOCKED)

"Reputation" in this phase means `ValidatorEntry.weight` (already accumulated via
`ApplyVoteEffects`) and `penalty_score` / `missed_epochs` (existing penalty mechanics).
No new reputation daemon or scoring function is introduced. The existing weight
distribution naturally skews toward trusted nodes (genesis, FULL role), which aligns
with the design intent.

### D-08: Role::FULL Promotion Stays (LOCKED)

The `Role::FULL` promotion via `ApplyVoteEffects` (06-03) is retained. It operates
independently of the slot-based voting: a promoted FULL node gets higher weight via the
existing weight accumulation path, which flows into slot weight contributions naturally.
No separate "reputation identifies full nodes" mechanic is needed beyond what's already
in `ApplyVoteEffects`.

### D-09: Proto Change Required (LOCKED)

Unlike the deferred-initial-plan approach, `ConsensusVote` proto IS extended in this
phase. The 3 `bytes32` hash slots require a schema change. This is a consensus-internal
message — no external API impact — but does require coordinated validator upgrade.

### D-10: Tier 1 Remains (LOCKED)

The existing Phase 5 per-node RPC verification (≥2 of 3 endpoints, `PublicChainInputValidator`)
continues unchanged. Phase 6 adds the **network-level** slot-based quorum ON TOP of
per-node verification. A validator must still pass Tier 1 (local RPC check) before its
vote and slot hashes are considered at the network level.

## Explicitly Out of Scope (v1)

| Concern | Disposition |
|---------|-------------|
| RPC hash registration in `ValidatorEntry` proto | D-04 — not needed |
| Separate reputation daemon or scoring engine | D-07 — reuse existing weight |
| `sha256(rpc_response)` data commitments in votes | Out of scope — only endpoint identity hash |
| Per-chain slot allocation | v1: node-level classification only |

## Open Design Question (for planning)

**Q1: Slot pass threshold.** Each slot computes a weight contribution. What threshold
must that contribution cross for the slot to "pass"?

Possibilities:
- **(a)** The slot's weight contribution must be ≥ the slot's multiplier of the TOTAL
  reputation pool for that slot type (e.g., slot 1 weight ≥ 25% of all node reputations
  that COULD vote in slot 1).
- **(b)** The slot's weight contribution must be ≥ the slot's multiplier of the total
  reputation OF THOSE WHO ACTUALLY VOTED in that slot.
- **(c)** The multiplier IS the gate — no additional threshold; the 50%/25% factor
  is applied and the result feeds into a cumulative quorum check where total across
  passing slots must cross 2/3 of network reputation.

**The planner MUST resolve this with the user before task decomposition** — it
determines the arithmetic in `EvaluateQuorum`.

## Plan Impact

The existing 06-01 through 06-04 plans are **invalidated**. This CONTEXT.md replaces
the binary-cohort model with a cryptographic slot model. Required replan:

- **06-01** → Proto extension (3 slot hashes in ConsensusVote)
- **06-02** → Slot-based tally algorithm (hash grouping, deduplication, per-slot scoring)
- **06-03** → Role::FULL promotion (retained, largely unchanged)
- **06-04** → Slot-voting tests + deduplication edge cases
