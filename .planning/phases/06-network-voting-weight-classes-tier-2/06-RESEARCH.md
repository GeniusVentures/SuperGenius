# Phase 6: Network Voting Weight Classes (Tier 2) - Research

**Researched:** 2026-06-19
**Domain:** Consensus voting / validator registry / bridge-mint security
**Confidence:** MEDIUM

## Summary

Phase 6 adds a **second, network-level trust layer** on top of the per-node RPC
verification (Tier 1, delivered in Phase 5). The phase goal — "direct API-key nodes
carry 50% voting weight, public-only RPC nodes carry 25% weight … final approval
requires both cohorts to independently meet thresholds" — is a cohort-partitioned
quorum rule that applies **only to bridge-mint subjects**, layered onto an existing
single-pool weighted-voting consensus.

The codebase already contains most of the primitives this phase needs, but several
are wired for a different purpose and three foundational gaps must be closed first:

1. **A weight system already exists** — `ValidatorRegistry::WeightConfig`
   (`src/blockchain/ValidatorRegistry.hpp:66`) with `genesis_weight_`, `full_weight_`,
   `regular_weight_`, `sharded_weight_`, plus `QuorumThreshold()` / `IsQuorum()`
   (`ValidatorRegistry.cpp:310-333`). Production runs at quorum `2/3` single-pool
   (`src/blockchain/impl/Blockchain.cpp:131-135`). But this is **one pool**, not two
   cohorts, and there is **no per-subject-type quorum** today.
2. **A `Role` enum already exists** (`GENESIS | FULL | REGULAR | SHARDED`,
   `ValidatorRegistry.proto`) — but `Role::FULL` is **never assigned anywhere in src/
   or test/** (verified by grep). `CreateGenesisRegistry` sets `GENESIS`; new
   validators are stamped `REGULAR` (`ValidatorRegistry.cpp:1693`). There is no
   promotion path from `REGULAR` → `FULL`, which is the "reputation identifies full
   nodes" requirement.
3. **The API-key signal already exists** — `eth::rpc::RpcEndpointConfig` carries
   `is_paid`, `is_public`, `api_key_env_var`, `api_key_literal`
   (`evmrelay/include/eth/rpc_manager_config.hpp:16`). These are the exact signals
   needed to classify a node into the "direct API-key" cohort vs. "public-only"
   cohort. But this signal is currently consumed **only inside per-node RPC
   verification** (`PublicChainInputValidator::WeightedRpcEndpoint`), not propagated
   to the consensus/registry layer where cohort membership must live.

A critical naming trap: `PublicChainInputValidator::WeightedRpcEndpoint` (Phase 5)
**already documents** "Direct (api-key) endpoints contribute 50% weight. Public
endpoints … 25% weight" (`src/account/PublicChainInputValidator.hpp:28-31`). That is
**Tier 1 (per-node, local RPC majority)**, NOT Tier 2 (network cohort voting). The
two are easily conflated. Phase 6 operates on the **consensus vote tally**, not the
RPC endpoint tally.

**Primary recommendation:** Implement Tier 2 as a **subject-type-gated, two-cohort
quorum policy** inside `ConsensusManager` (the single owner of `TallyVotes` and the
incremental `HandleVote` tally), keyed off `EmbeddedTransaction::kMintV2` (the
existing bridge-mint discriminator). Introduce a `ValidatorCohort` derivation on
`ValidatorRegistry` (mapping `Role` + a new optional API-key/direct-endpoint flag to
`DIRECT_API | PUBLIC_ONLY`), add a `TwoTierQuorumPolicy` that requires each cohort to
independently reach a configurable threshold, and **do NOT modify the proto
`ConsensusCertificate`** in v1 — store cohort breakdown as an in-memory `QuorumTally`
extension only (certificate remains single-pool for backward compatibility). Close
the `Role::FULL` gap with a reputation-driven promotion in `ApplyVoteEffects` using
the existing `approval_increment_` / `missed_epochs_` fields.

This phase touches shared consensus hot paths (`TallyVotes`, `HandleVote`). Per
CLAUDE.md, any change to shared libraries requires the **full regression suite**,
not just changed-file tests.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Node cohort classification (DIRECT_API vs PUBLIC_ONLY) | API / Backend (ValidatorRegistry) | RPC config layer | The registry owns validator identity and weights; cohort is a property of the validator entry, derived from RPC config + Role. |
| Per-cohort quorum arithmetic | API / Backend (ConsensusManager) | — | `ConsensusManager` is the sole owner of `TallyVotes` and the incremental vote tally; quorum math must not be duplicated. |
| Bridge-mint subject discrimination | API / Backend (ConsensusManager / TransactionManager) | — | `EmbeddedTransaction::kMintV2` + non-empty `chain_id` is the existing discriminator; reused, not reinvented. |
| Reputation → Role::FULL promotion | API / Backend (ValidatorRegistry) | — | `ApplyVoteEffects` already mutates weight/penalty; promotion belongs there. |
| RPC-result vote commitments (stretch / deferred) | API / Backend (ConsensusVote proto) | ConsensusManager | The design note proposes `sha256(rpc_response)` commitments to detect fabricated votes; this is a proto change deferred from v1 (see Open Questions). |
| Cohort threshold configuration | Frontend Server / node config (DevConfig_st + WeightConfig) | — | New thresholds ride alongside the existing `WeightConfig` aggregate. |

## Standard Stack

No new third-party packages are required. This phase is pure C++17 against the
existing project stack. All capabilities are built on existing internal types.

### Core (existing, reused)

| Component | Location | Purpose | Why Standard |
|-----------|----------|---------|--------------|
| `ValidatorRegistry` | `src/blockchain/ValidatorRegistry.{hpp,cpp}` | Validator set, weights, quorum, reputation | Already the single owner of weight/quorum logic `[VERIFIED: codebase]` |
| `ConsensusManager` | `src/blockchain/Consensus.{hpp,cpp}` | Vote tally, certificate production | Sole owner of `TallyVotes` + incremental `HandleVote` tally `[VERIFIED: codebase]` |
| `WeightConfig` | `ValidatorRegistry.hpp:66` | Weight + penalty tunables | Already an aggregate struct with sensible defaults `[VERIFIED: codebase]` |
| `Role` / `Status` enums | `ValidatorRegistry.proto` | Validator classification | Proto-defined; `FULL` exists but is unassigned `[VERIFIED: codebase]` |
| `RpcEndpointConfig` | `evmrelay/include/eth/rpc_manager_config.hpp:16` | API-key / paid / public signals | Already carries `is_paid`, `is_public`, `api_key_*` `[VERIFIED: codebase]` |
| `WeightedRpcEndpoint` | `src/account/PublicChainInputValidator.hpp:33` | Tier 1 per-node RPC weights | Reference for the 50/25 naming; **Tier 1 only** `[VERIFIED: codebase]` |
| `EmbeddedTransaction::kMintV2` | `blockchain/impl/proto/Consensus.proto` | Bridge-mint discriminator | Existing oneof tag; `chain_id` non-empty confirms bridge source `[VERIFIED: codebase]` |

### Supporting (existing test infrastructure, reused)

| Component | Location | Purpose |
|-----------|----------|---------|
| `ConsensusManagerTestAccess` | `test/src/blockchain/consensus_certificate_test.cpp:12` | Friend-access for private tally/proposal state in tests |
| `MakeRegistry` / `MakeManager` helpers | `consensus_certificate_test.cpp:99,136` | 3-line registry+manager fixture pattern (quorum `1/1` in tests) |
| `ASSERT_WAIT_FOR_CONDITION` | `test/testutil/wait_condition.hpp` | Project-mandated polling template (no `sleep_for`) |

**Installation:** None. No `npm`/`pip`/`cargo` packages. Build is the existing
CMake + Ninja flow:

```bash
cd build/OSX/Debug && cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && ninja
```

## Package Legitimacy Audit

> **N/A — no external packages installed.** This phase adds no third-party
> dependencies. All work is against the existing internal C++17 stack (Boost,
> libsecp256k1, spdlog, gtest, protobuf — all already in `thirdparty/` per
> CLAUDE.md). slopcheck skipped: no packages to audit.

## Architecture Patterns

### System Architecture Diagram

```
                                  ┌─────────────────────────────────────────────┐
   Burn event (EVM)               │  Tier 1 — per-node RPC verification         │
   ───────────────────►  BridgeRelayer  ──►  PublicChainInputValidator          │
                                  │   VerifyPublicChainSmartContract()          │
                                  │   (≥2 of 3 endpoints, local majority)       │
                                  │   WeightedRpcEndpoint (50/25 — Tier 1 only) │
                                  └──────────────────┬──────────────────────────┘
                                                     │ MintFunds() → MintTransactionV2
                                                     ▼
                                  ┌─────────────────────────────────────────────┐
                                  │  Consensus proposal (NonceSubject w/         │
                                  │   EmbeddedTransaction::kMintV2)              │
                                  └──────────────────┬──────────────────────────┘
                                                     │ PubSub broadcast
                                                     ▼
              ┌────────────────────────────────────────────────────────────────────┐
              │  Tier 2 — network cohort voting  ◄── THIS PHASE                    │
              │                                                                       │
              │   ConsensusManager::TallyVotes / HandleVote                          │
              │        │                                                              │
              │        ├─► Is subject a bridge mint? (kMintV2 + chain_id≠"" )        │
              │        │      │                                                      │
              │        │      ├─ NO  ► single-pool quorum (unchanged, 2/3)           │
              │        │      │                                                              │
              │        │      └─ YES ► TwoTierQuorumPolicy                            │
              │        │              │                                                  │
              │        │              ├─ DIRECT_API cohort   (Role::FULL/API-key)      │
              │        │              │    threshold ≥ kDirectThreshold (e.g. 51%)     │
              │        │              │                                                  │
              │        │              ├─ PUBLIC_ONLY cohort (Role::REGULAR)            │
              │        │              │    threshold ≥ kPublicThreshold (e.g. 51%)     │
              │        │              │                                                  │
              │        │              └─ has_quorum = direct_ok AND public_ok          │
              │        │                                                                 │
              │        └─ ValidatorRegistry::CohortOf(validator_id)                   │
              │              ▲                                                         │
              │              │ derived from Role + RPC config (is_paid/api_key)       │
              └──────────────┼─────────────────────────────────────────────────────────┘
                             │
                             ▼
                                  ┌─────────────────────────────────────────────┐
                                  │  ConsensusCertificate (unchanged schema)     │
                                  │   total_weight / approved_weight (single     │
                                  │   pool) — cohort detail in-memory only in v1 │
                                  └─────────────────────────────────────────────┘
```

Trace the primary use case: a bridge burn on an EVM chain is detected by
`BridgeRelayer`, verified locally (Tier 1), turned into a `MintTransactionV2`
proposal, broadcast; each receiving validator's `ConsensusManager` tallies the
incoming votes. For bridge mints the new `TwoTierQuorumPolicy` partitions each
voter into a cohort via `ValidatorRegistry::CohortOf()`, accumulates weight per
cohort, and requires **both** cohorts to independently meet their thresholds before
`has_quorum` becomes true and a certificate may be produced.

### Recommended Project Structure

No new top-level directories. Additions are surgical:

```
src/blockchain/
├── Consensus.hpp            # Extend QuorumTally; add TwoTierQuorumPolicy
├── Consensus.cpp            # Modify TallyVotes + HandleVote tally (2 sites)
├── ValidatorRegistry.hpp    # Add CohortOf(); extend WeightConfig w/ cohort thresholds
├── ValidatorRegistry.cpp    # Add cohort derivation; Role::FULL promotion in ApplyVoteEffects
└── impl/proto/
    └── Consensus.proto      # UNCHANGED in v1 (certificate stays single-pool)
src/account/
├── GeniusNode.cpp           # Propagate node's own cohort (from RPC config) to registry
└── PublicChainInputValidator.hpp  # Expose HasDirectApiEndpoint() for self-classification
test/src/blockchain/
└── consensus_two_tier_test.cpp  # NEW — cohort partition + dual-threshold tests
```

### Pattern 1: Single-owner quorum math (DO NOT duplicate)

**What:** All weight/quorum arithmetic lives in `ValidatorRegistry`
(`QuorumThreshold`, `IsQuorum`, `TotalWeight`) and is invoked solely by
`ConsensusManager::TallyVotes` plus the incremental tally in `ConsensusManager`'s
`HandleVote` path.

**When to use:** Always — the project enforces this. Phase 6's two-tier policy is a
**new quorum predicate** that must be evaluated at the same two call sites, not a
parallel tally implemented elsewhere.

**Example (the two tally sites that must both honor cohorts):**

```cpp
// Site 1 — certificate creation: src/blockchain/Consensus.cpp:1257, 1387
auto tally_result = TallyVotes( proposal, votes );
// ...
tally.has_quorum = registry_->IsQuorum( approved_weight, total_weight );

// Site 2 — incremental vote handling: src/blockchain/Consensus.cpp:2299-2309
if ( it->second.total_weight == 0 )
{
    it->second.total_weight = registry_->TotalWeight( proposal_registry );
}
it->second.approved_weight += validator->weight();
has_quorum = registry_->IsQuorum( it->second.approved_weight, it->second.total_weight );
```

Both sites must consult the policy. The recommended shape keeps `IsQuorum` for
non-bridge subjects and delegates bridge subjects to `TwoTierQuorumPolicy::Satisfied`.

### Pattern 2: Subject-type dispatch is already canonical

**What:** `ConsensusManager` dispatches validation/handling by
`ComputeSubjectTypeHash(subject_type)` (`NONCE_SUBJECT_TYPE`, etc.). Bridge mints
are a **transaction category inside** the nonce subject, discriminated by
`EmbeddedTransaction::kMintV2` + non-empty `chain_id`
(`src/account/TransactionManager.cpp:633, 1098, 3683`).

**When to use:** To decide "does the two-tier policy apply to this subject?" —
decode the nonce subject, check the embedded transaction case, and check
`tx->GetChainId()` is non-empty. This mirrors the existing
`TransactionManager::GetValidationChainId()` logic. **Do not invent a new subject
type for bridge mints** — they intentionally ride the nonce path.

### Pattern 3: Reputation reuse (no new scoring system)

**What:** `ValidatorRegistry::ApplyVoteEffects`
(`ValidatorRegistry.cpp:1711-1807`) already mutates `weight`, `penalty_score`,
`missed_epochs`, and `status` per vote. Correct votes increment weight up to a
role cap; incorrect votes accumulate penalty toward `BLACKLISTED`.

**When to use:** The "reputation identifies full nodes" requirement is satisfied by
adding a **promotion rule** inside `ApplyVoteEffects`: when a `REGULAR` validator's
weight crosses a configurable `full_promotion_weight_` threshold AND its
`penalty_score` is below `penalty_threshold_`, promote `Role::REGULAR` →
`Role::FULL`. This closes the "FULL is never assigned" gap using existing fields.
No separate reputation daemon.

### Anti-Patterns to Avoid

- **Don't conflate Tier 1 and Tier 2.** `WeightedRpcEndpoint` (50/25) is per-node
  RPC majority. Tier 2 is network cohort quorum. They share the 50/25 vocabulary
  but operate on different tallies. Naming new types `TwoTier*` / `Cohort*`
  deliberately avoids reusing the `Weighted` name.
- **Don't store cohort breakdown in `ConsensusCertificate` in v1.** The
  certificate is a consensus-critical, replicated protobuf. Changing its schema
  breaks all peers and risks consensus divergence. Keep cohort detail in the
  in-memory `QuorumTally` for v1; certificate stays single-pool `[ASSUMED]`.
- **Don't hand-roll signature verification or weight math.** `TallyVotes` already
  calls `GeniusAccount::VerifySignature` (`Consensus.cpp:1363`) — reuse exactly.
- **Don't add a new subject type.** Bridge mints are nonce subjects with `kMintV2`.
- **Don't use `sleep_for` in tests** (CLAUDE.md mandate). Use
  `ASSERT_WAIT_FOR_CONDITION`.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Weighted quorum arithmetic | New cohort math from scratch | Extend `WeightConfig` + add `TwoTierQuorumPolicy` next to `IsQuorum` | `QuorumThreshold`/`IsQuorum` already handle ceil-division + zero-weight edge cases (`ValidatorRegistry.cpp:310-322`) |
| Vote signature verification | Reimplement | `GeniusAccount::VerifySignature` (already in `TallyVotes`) | Canonical secp256k1 path; reused for vote validity |
| Node "is full node" flag | New field | Existing `GeniusNode::is_full_node_` + `RpcEndpointConfig.is_paid` | The signal exists; needs propagation, not reinvention |
| Validator classification | New taxonomy | Existing `Role` enum + new `CohortOf()` derivation | `Role` is proto-defined and already weight-bearing |
| Reputation scoring | New daemon/score | `ApplyVoteEffects` weight/penalty mutation + promotion rule | Existing fields (`weight`, `penalty_score`, `missed_epochs`) already track reputation |
| Bridge-mint detection | New subject type or flag | `EmbeddedTransaction::kMintV2` + non-empty `chain_id` | Existing discriminator used by `TransactionManager` |

**Key insight:** This phase's security value comes from the **dual-cohort AND
predicate** (both cohorts must independently agree), not from novel cryptography.
The project's threat model (ROADMAP "Consensus Trust Model for Bridge Mints")
explicitly notes reputation alone is insufficient because colluding validators can
skip RPC verification. The two-cohort AND rule raises the cost of collusion: an
attacker must corrupt nodes in **both** cohorts simultaneously.

## Runtime State Inventory

> Phase 6 introduces new consensus rules but does **not** rename, rebrand, or
> migrate existing identifiers. However, it adds new registry semantics
> (`Role::FULL` assignment), so a partial inventory applies.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | Existing registries in GlobalDB have `ValidatorEntry.role` only ever set to `GENESIS` or `REGULAR`. After this phase, `FULL` becomes possible. | **Code edit only** — no data migration. Old `REGULAR` entries remain valid; promotion happens incrementally via `ApplyVoteEffects`. Existing certificates remain valid (single-pool schema unchanged). |
| Live service config | None — no external services embed cohort strings. | None — verified: no n8n/Datadog/Tailscale references in scope. |
| OS-registered state | None. | None. |
| Secrets/env vars | API keys (`api_key_env_var`) already used by Tier 1 RPC; Phase 6 reads the **presence** of a direct endpoint, not the key value. No new secrets. | None — key names unchanged. |
| Build artifacts | None — no package renames; `.egg-info`/global installs N/A for C++. | None. |

**Nothing found requiring data migration.** The change is forward-compatible:
existing single-pool certificates and `REGULAR`-only registries continue to validate.
The two-tier policy only activates for new bridge-mint proposals.

## Common Pitfalls

### Pitfall 1: Modifying only one of the two tally sites

**What goes wrong:** Bridge-mint certificates either never produce (only
`TallyVotes` updated, incremental path still single-pool) or produce prematurely
(only incremental path updated, certificate `TallyVotes` still single-pool).
**Why it happens:** There are **two** independent quorum evaluations —
`ConsensusManager::TallyVotes` (`Consensus.cpp:1299, 1399`) for certificate
creation, and the inline tally in the `HandleVote` proposal-state path
(`Consensus.cpp:2299-2309`).
**How to avoid:** Route both through one shared helper, e.g.
`ConsensusManager::EvaluateQuorum(proposal, votes, registry)` that internally picks
single-pool vs `TwoTierQuorumPolicy` based on subject type. Never inline the cohort
math in two places.
**Warning signs:** Certificate is produced but `HandleVote` never fires
`quorum_reached`, or vice versa.

### Pitfall 2: Treating `Role::FULL` as already populated

**What goes wrong:** Cohort derivation `Role::FULL → DIRECT_API` yields an empty
DIRECT_API cohort on day one, so no bridge mint can ever reach the direct threshold
→ bridge mints stall forever.
**Why it happens:** `Role::FULL` is referenced in switch statements but **never
assigned** (verified). Assuming it is populated is the trap.
**How to avoid:** Either (a) ship the promotion rule in `ApplyVoteEffects` in the
same wave, or (b) derive cohort **primarily from RPC config** (`is_paid` /
`api_key_*` presence) with `Role::FULL` as a secondary signal, so the cohort is
non-empty from genesis if any node has a paid endpoint configured. Recommend (b)
for v1 liveness; layer (a) on top.
**Warning signs:** DIRECT_API cohort weight is 0 in logs; all bridge mints fail
quorum.

### Pitfall 3: Confusing Tier 1 weights with Tier 2 cohorts

**What goes wrong:** A developer reads `WeightedRpcEndpoint` (50/25, Tier 1) and
thinks Tier 2 is already implemented, or copies its semantics incorrectly into the
consensus layer.
**Why it happens:** Identical 50/25 vocabulary, adjacent code areas, same domain.
**How to avoid:** Name all new types with `Cohort`/`TwoTier` vocabulary, never
`Weighted`. Add a doc-comment cross-reference at both sites. Keep Tier 1 in
`PublicChainInputValidator`, Tier 2 in `ConsensusManager`/`ValidatorRegistry`.
**Warning signs:** A PR that "adds 50% weight to votes" by editing
`WeightedRpcEndpoint`.

### Pitfall 4: Forgetting the full regression suite

**What goes wrong:** Shared consensus library change passes changed-file tests but
breaks `genius_node_test` or other consumers linked to the same library.
**Why it happens:** CLAUDE.md mandate — code touching shared libraries must run ALL
linked tests, not just changed-file tests.
**How to avoid:** Run the full `ninja` + complete test suite before any push. The
GSD ship workflow does not pre-check this.
**Warning signs:** Green PR, red integration test on merge.

### Pitfall 5: Non-deterministic cohort derivation across peers

**What goes wrong:** Peer A classifies validator X as DIRECT_API, Peer B as
PUBLIC_ONLY → they disagree on quorum → no certificate.
**Why it happens:** If cohort derivation depends on each peer's **local** view of
X's RPC config (which may differ), it is non-deterministic.
**How to avoid:** Cohort membership must be a function of **registry state only**
(`Role`, `weight`, and a new optional registry-stored `cohort`/`has_direct_api`
flag) — never a function of the local node's own RPC config. The local RPC config
informs the node's **own self-registration**, which then propagates via the
registry CRDT to all peers. All peers derive from the same registry snapshot.
**Warning signs:** Quorum flips depending on which peer is aggregating.

## Code Examples

All examples are derived from the actual codebase (verified by reading the files).

### Existing single-pool tally to extend

```cpp
// Source: src/blockchain/Consensus.cpp:1299-1397 (TallyVotes) and
//         src/blockchain/Consensus.cpp:2299-2309 (incremental HandleVote tally)
uint64_t total_weight    = ValidatorRegistry::TotalWeight( registry );
uint64_t approved_weight = 0;
std::unordered_set<std::string> seen;

for ( const auto &vote : votes )
{
    if ( vote.proposal_id() != proposal.proposal_id() ) { continue; }
    if ( !seen.insert( vote.voter_id() ).second ) { continue; }

    const auto *validator = ValidatorRegistry::FindValidator( registry, vote.voter_id() );
    if ( !validator || validator->status() != ValidatorRegistry::Status::ACTIVE ) { continue; }

    auto signing_bytes = VoteSigningBytes( vote );
    if ( signing_bytes.has_error() ) { continue; }
    if ( !GeniusAccount::VerifySignature( vote.voter_id(),
                                           vote.signature(),
                                           signing_bytes.value() ) ) { continue; }
    if ( vote.approve() )
    {
        approved_weight += validator->weight();
    }
}
// Phase 6 extension point: when subject is a bridge mint, replace the
// single-pool predicate below with TwoTierQuorumPolicy::Satisfied(...).
tally.has_quorum = registry_->IsQuorum( approved_weight, total_weight );
```

### Existing WeightConfig to extend

```cpp
// Source: src/blockchain/ValidatorRegistry.hpp:66-84
struct WeightConfig
{
    uint64_t genesis_weight_                  = 50000;
    uint64_t full_weight_                     = 1000;   // <- currently dead (FULL never assigned)
    uint64_t regular_weight_                  = 1;
    uint64_t sharded_weight_                  = 1;
    // ... caps, penalty fields ...
    // Phase 6 additions (recommended):
    // uint64_t full_promotion_weight_        = 500;   // REGULAR->FULL threshold
    // uint64_t direct_cohort_numerator_      = 51;    // DIRECT_API cohort quorum
    // uint64_t direct_cohort_denominator_    = 100;
    // uint64_t public_cohort_numerator_      = 51;    // PUBLIC_ONLY cohort quorum
    // uint64_t public_cohort_denominator_    = 100;
};
```

### Existing RPC endpoint signals (the cohort input)

```cpp
// Source: evmrelay/include/eth/rpc_manager_config.hpp:16-29
struct RpcEndpointConfig
{
    std::string chain_name;
    uint64_t    chain_id = 0;
    std::string url_template;
    std::optional<std::string> api_key_env_var;   // <- presence => direct/API-key
    std::optional<std::string> api_key_literal;   // <- presence => direct/API-key
    uint32_t    priority = 0;
    uint32_t    weight = 0;
    uint32_t    rate_limit_per_second = 0;
    bool        is_paid = false;                  // <- true => direct/paid
    bool        is_public = true;                 // <- true && !is_paid => public-only
    bool        verified = false;
};
```

### Existing bridge-mint discriminator

```cpp
// Source: src/account/TransactionManager.cpp:3916, 633, 1098
if ( nonce_subject.value().transaction().transaction_case()
         == EmbeddedTransaction::TRANSACTION_NOT_SET ) { /* reject */ }
// ...
const auto chain_id = tx->GetChainId();           // non-empty => bridge source
if ( chain_id.empty() ) { /* internal transfer */ }
```

### Existing reputation mutation (promotion hook site)

```cpp
// Source: src/blockchain/ValidatorRegistry.cpp:1738-1762 (inside ApplyVoteEffects)
if ( entry.status() == Status::ACTIVE )
{
    const uint64_t increment = weight_config_.approval_increment_;
    if ( increment > 0 )
    {
        uint64_t role_cap = /* per-role cap */;
        const uint64_t clamped = std::min( entry.weight() + increment, role_cap );
        entry.set_weight( clamped );
        // Phase 6 promotion hook:
        // if ( entry.role() == Role::REGULAR
        //      && clamped >= weight_config_.full_promotion_weight_
        //      && entry.penalty_score() < weight_config_.penalty_threshold_ )
        //     entry.set_role( Role::FULL );
    }
}
```

### Test topology pattern to follow

```cpp
// Source: test/src/blockchain/consensus_certificate_test.cpp:99-134, 431-489
// Pattern: MakeRegistry(db, account) with quorum 1/1, StoreGenesisRegistry,
// ASSERT_WAIT_FOR_CONDITION until LoadRegistry succeeds, then CreateProposal +
// CreateVote + TallyVotes. ConsensusManagerTestAccess exposes private state.
auto registry = ValidatorRegistry::New( db, /*num*/1, /*den*/1,
                                        ValidatorRegistry::WeightConfig{},
                                        account->GetAddress(), /*block_request*/... );
registry->StoreGenesisRegistry( account->GetAddress(), signer );
ASSERT_WAIT_FOR_CONDITION( /*registry LoadRegistry succeeds*/, 2s, ... );

auto tally = manager->TallyVotes( proposal, { vote1, vote2 }, registry_snapshot, cid );
EXPECT_TRUE( tally.value().has_quorum );
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Single-pool weighted quorum for all subjects | (unchanged for non-bridge) | — | Phase 6 preserves single-pool for internal transfers; only bridge mints get two-tier |
| `Role::FULL` defined but unassigned | (still unassigned at research time) | — | Phase 6 must close this gap (promotion rule +/or RPC-config-derived cohort) |
| Per-node RPC majority (Tier 1, ≥2 of 3) | Delivered Phase 5 | 2026-06 | Tier 1 is a precondition; Phase 6 layers network cohort voting on top |
| `ConsensusCertificate` single-pool schema | (unchanged in v1) | — | Cohort breakdown stays in-memory; no proto migration |

**Deprecated/outdated:**
- The `rpc-verification-tiers.md` design note's mention of `sha256(rpc_response)`
  vote commitments is **not implemented** and is treated as a deferred stretch goal
  (see Open Questions). It would require a `ConsensusVote` proto extension and is
  out of scope for the first pass of Phase 6.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Cohort breakdown should stay out of the `ConsensusCertificate` protobuf in v1 (in-memory only) | Architecture / Anti-Patterns | If downstream consumers (other peers, block explorers) need verifiable cohort evidence, the certificate schema must be extended — a larger, consensus-breaking change. Needs user confirmation. |
| A2 | Cohort membership should be derived from registry state only (not local RPC config) for determinism | Pitfall 5 | If the user wants cohort to reflect each peer's live RPC connectivity (not registry-stored classification), the determinism guarantee breaks and a different design is needed. |
| A3 | `Role::FULL` promotion via `ApplyVoteEffects` is the intended "reputation identifies full nodes" mechanism | Pattern 3 | The user may envision a separate, more sophisticated reputation system (e.g., slashing history, uptime SLAs). If so, the promotion rule is too simplistic. |
| A4 | 50%/25% in the phase goal are **cohort weight caps** (DIRECT_API cohort total weight = 50% of network, PUBLIC_ONLY = 25%), not vote-counting ratios | Summary / Architecture | The phase goal wording is ambiguous. If 50/25 are instead per-vote multipliers or quorum thresholds, the arithmetic differs. The `rpc-verification-tiers.md` note frames them as cohort weight classes, which this research follows. **Needs user confirmation at discuss-phase.** |
| A5 | Both cohorts use an independent ≥51% threshold (the note says "≥51% of this cohort") | Summary | If a different per-cohort threshold is intended (e.g. 2/3), only constants change. Low risk. |
| A6 | The genesis/authority node is implicitly in the DIRECT_API cohort | Architecture | If genesis should be a special third cohort or excluded from cohort voting, the predicate changes. |

## Open Questions

1. **Are 50% / 25% cohort weights, or per-cohort quorum thresholds?**
   - What we know: The phase goal and `rpc-verification-tiers.md` describe "direct
     API-key nodes carry 50% voting weight, public-only RPC nodes carry 25% weight"
     and "Require ≥51% of this cohort to approve."
   - What's unclear: Whether 50/25 are (a) the **share of total network weight** each
     cohort contributes, or (b) **per-vote weight multipliers** within a cohort, or
     (c) something else. This materially changes the arithmetic.
   - Recommendation: Resolve at `/gsd:discuss-phase 6`. Until then, treat as
     per-cohort independent thresholds (≥51% within each cohort), which is what the
     design note's "Require ≥51% of this cohort" sentence states most directly.

2. **Should `ConsensusVote` carry an RPC-result commitment (`sha256(rpc_response)`)?**
   - What we know: The ROADMAP "Consensus Trust Model for Bridge Mints" note proposes
     this so peers can detect fabricated votes. `ConsensusVote` proto today has only
     `voter_id, approve, timestamp, signature`.
   - What's unclear: Whether this is in-scope for Phase 6 or deferred.
   - Recommendation: **Defer to a follow-up.** It requires a proto change (consensus
     break) and adds bandwidth to every vote. The two-cohort AND rule delivers the
     primary security value without it.

3. **How does a node learn its own cohort at startup?**
   - What we know: `GeniusNode` has `is_full_node_` (constructor bool); RPC endpoints
     come from `chains_config.json` via `ChainRpcEndpointProvider`; `RpcEndpointConfig`
     has `is_paid`/`api_key_*`.
   - What's unclear: Whether a node with **any** paid/direct endpoint across any chain
     is DIRECT_API, or whether it's per-chain, or whether it must be a configured
     deployment-time property.
   - Recommendation: Node-level classification (any direct endpoint → DIRECT_API),
     persisted into the validator registry on self-registration, so all peers agree.
     Confirm with user.

4. **Is `Role::FULL` promotion automatic, or operator-gated?**
   - What we know: `ApplyVoteEffects` is the natural hook; no operator UI exists.
   - What's unclear: Whether promotion should be purely algorithmic (weight
     threshold) or require off-chain operator action.
   - Recommendation: Algorithmic in v1 (closes the "FULL never assigned" gap with
     least code), with the threshold configurable in `WeightConfig`.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build | ✓ | homebrew cmake | — |
| Ninja | Build | ✓ | homebrew ninja | — |
| clang++ (C++17) | Build | ✓ | Apple clang 17.0.0 | — |
| build/OSX/Debug | Incremental build/test | ✓ | exists (CMakeCache.txt present) | rebuild if stale |
| libsecp256k1 (thirdparty) | Vote signature verification | ✓ | thirdparty/ | — |
| gtest (thirdparty) | Tests | ✓ | thirdparty/ | — |
| spdlog (thirdparty) | Logging (CLAUDE.md mandate) | ✓ | thirdparty/ | — |

**Missing dependencies with no fallback:** None.
**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Google Test (gtest) via CMake `enable_testing()` |
| Config file | `test/src/CMakeLists.txt` (+ per-dir `CMakeLists.txt`) |
| Quick run command | `cd build/OSX/Debug && ctest -R ConsensusTwoTier -j8 --output-on-failure` |
| Full suite command | `cd build/OSX/Debug && ninja && ctest -j8 --output-on-failure` (CLAUDE.md: full regression on shared-lib changes) |

### Phase Requirements → Test Map

> Phase 6 has no formal REQ-IDs in ROADMAP yet (plans = 0). The map below covers the
> behaviors implied by the phase goal; the planner should mint REQ-IDs at planning.

| Req ID (proposed) | Behavior | Test Type | Automated Command | File Exists? |
|-------------------|----------|-----------|-------------------|-------------|
| REQ-COHORT-01 | `CohortOf()` returns DIRECT_API for a validator with paid/api-key RPC config | unit | `ctest -R CohortOfDirectApi` | ❌ Wave 0 |
| REQ-COHORT-02 | `CohortOf()` returns PUBLIC_ONLY for a REGULAR validator with public-only RPC | unit | `ctest -R CohortOfPublicOnly` | ❌ Wave 0 |
| REQ-QUORUM-01 | Bridge-mint subject with only DIRECT_API quorum (PUBLIC below threshold) → no certificate | unit | `ctest -R TwoTierPublicMissing` | ❌ Wave 0 |
| REQ-QUORUM-02 | Bridge-mint subject with only PUBLIC_ONLY quorum (DIRECT below threshold) → no certificate | unit | `ctest -R TwoTierDirectMissing` | ❌ Wave 0 |
| REQ-QUORUM-03 | Bridge-mint subject with both cohorts meeting thresholds → certificate produced | unit | `ctest -R TwoTierBothMeet` | ❌ Wave 0 |
| REQ-QUORUM-04 | Non-bridge subject (internal transfer) → single-pool quorum unchanged | unit | `ctest -R TwoTierNonBridgeUnchanged` | ❌ Wave 0 |
| REQ-QUORUM-05 | Incremental `HandleVote` tally and `TallyVotes` agree on two-tier quorum | unit | `ctest -R TwoTierIncrementalAgrees` | ❌ Wave 0 |
| REQ-REPUT-01 | REGULAR validator promoted to FULL when weight ≥ threshold and penalty low | unit | `ctest -R RolePromotionToFull` | ❌ Wave 0 |
| REQ-DETERM-01 | Two peers derive same cohort for same validator from same registry snapshot | unit | `ctest -R CohortDeterminism` | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `cd build/OSX/Debug && ninja && ctest -R ConsensusTwoTier -j8`
- **Per wave merge:** `cd build/OSX/Debug && ninja && ctest -j8` (full consensus + account suites)
- **Phase gate:** Full suite green (`ctest -j8`) AND `genius_node_test` green before `/gsd:verify-work` (CLAUDE.md shared-library rule)

### Wave 0 Gaps

- [ ] `test/src/blockchain/consensus_two_tier_test.cpp` — covers REQ-COHORT-01/02, REQ-QUORUM-01..05, REQ-DETERM-01
- [ ] `test/src/blockchain/validator_registry_promotion_test.cpp` — covers REQ-REPUT-01 (Role::FULL promotion)
- [ ] Extend `ConsensusManagerTestAccess` if `EvaluateQuorum` helper or cohort tally needs friend access
- [ ] No framework install needed — gtest already in thirdparty/

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | Validator identity is secp256k1-key-based (`GeniusAccount::VerifySignature`); unchanged by this phase |
| V3 Session Management | no | No sessions; consensus votes are stateless signed messages |
| V4 Access Control | yes | **Cohort-based quorum is an access-control predicate on certificate production** — the core of this phase. Two-cohort AND rule raises collusion cost. |
| V5 Input Validation | yes | Subject-type discrimination (`kMintV2` + `chain_id`) gates the policy; malformed subjects already rejected by `HandleNonceConsensusSubject` |
| V6 Cryptography | yes | Vote signature verification reused verbatim (`VerifySignature`); no new crypto hand-rolled |
| V7 Error Handling/Logging | yes | All new quorum decisions must log cohort weights/thresholds via spdlog (CLAUDE.md: no printf) |

### Known Threat Patterns for Bridge-Mint Consensus

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Compromised validator votes Approve without RPC verification (ROADMAP threat model) | Spoofing / Elevation of privilege | **Two-cohort AND predicate** (this phase) — attacker must corrupt nodes in BOTH cohorts; single cohort cannot mint alone |
| Quorum of colluding nodes mints fake tokens | Tampering | Cohort partitioning + independent thresholds; combined with existing secp256k1 vote signatures |
| Non-deterministic cohort classification causes consensus divergence | Repudiation | Cohort derived from **registry state only** (deterministic across peers), never local RPC config (Pitfall 5) |
| Sybil: attacker floods PUBLIC_ONLY cohort with cheap nodes | Elevation of privilege | Per-cohort weight still bounded by registry `weight`/`Role`; PUBLIC_ONLY cohort weight is naturally lower; reputation promotion gates FULL/DIRECT_API |
| Bridge-mint stall (liveness denial) | Denial of service | Configurable thresholds; if a cohort has 0 weight, policy must fail-open or fail-closed **by config** — this is a decision point (see Open Questions / Pitfall 2) |
| Weight-overflow in cohort accumulation | Tampering | Use `uint64_t` with saturation; existing `TotalWeight` pattern already sums into `uint64_t` |

## Sources

### Primary (HIGH confidence)

- **Codebase (read directly this session):**
  - `src/blockchain/ValidatorRegistry.hpp` — `WeightConfig` (L66), `CertificateVotes` (L338), `Role`/`Status` usage, `QuorumThreshold`/`IsQuorum` signatures
  - `src/blockchain/ValidatorRegistry.cpp` — `ComputeWeight` (L253), `TotalWeight` (L294), `QuorumThreshold` (L310), `IsQuorum` (L324), `CreateGenesisRegistry` (L335), `ApplyVoteEffects` (L1711), `Role::FULL` never assigned (grep-verified)
  - `src/blockchain/Consensus.hpp` — `ConsensusManager`, `QuorumTally` (L213), `TallyVotes` signatures (L1299/1399), `Check`/`ValidationResult`
  - `src/blockchain/Consensus.cpp` — `TallyVotes` (L1299-1397), `CreateCertificate` (L1246-1290), incremental `HandleVote` tally (L2299-2309)
  - `src/blockchain/impl/proto/Consensus.proto` — `ConsensusCertificate` (single-pool: `total_weight`/`approved_weight`), `EmbeddedTransaction::kMintV2`, `ConsensusVote` (no commitment field)
  - `src/blockchain/impl/proto/ValidatorRegistry.proto` — `Role { GENESIS|FULL|REGULAR|SHARDED }`, `Status`, `ValidatorEntry`
  - `src/blockchain/impl/Blockchain.cpp` — registry construction with quorum `2/3` (L131-135), `GetAuthorizedFullNodeAddress()` genesis authority
  - `src/account/PublicChainInputValidator.hpp` — `WeightedRpcEndpoint` 50/25 doc (Tier 1), `SetRpcEndpoints`, `SetTransportFactory`
  - `evmrelay/include/eth/rpc_manager_config.hpp` — `RpcEndpointConfig` (`is_paid`, `is_public`, `api_key_env_var`, `api_key_literal`)
  - `src/account/TransactionManager.cpp` — `HandleNonceConsensusSubject` (L3899), `kMintV2`/`chain_id` discrimination (L633, 1098, 3683, 3916)
  - `src/account/GeniusNode.cpp` — `is_full_node_` ctor flag, `ConfigureRpcEndpoint`→`SetRpcEndpoints` (L1976-1984)
  - `test/src/blockchain/consensus_certificate_test.cpp` — `ConsensusManagerTestAccess` (L12), `MakeRegistry`/`MakeManager` (L99, 136), `TallyVotesWithRegistry` test (L431)

- **Project planning notes (read this session):**
  - `.planning/notes/rpc-verification-tiers.md` — authoritative Tier 1 vs Tier 2 design split
  - `.planning/notes/deferred-consensus-validation.md` — Pending/Reject/Stalled lifecycle (Phase 7, informs vote semantics)
  - `.planning/ROADMAP.md` — Phase 6 goal + "Consensus Trust Model for Bridge Mints" threat analysis
  - `.planning/phases/05-startup-wiring-mock-rpc/05-CONTEXT.md` — Phase 5 decisions (D-06 direct endpoints, D-17/18/19 UTXO changes)

### Secondary (MEDIUM confidence)

- None — all findings verified directly against the codebase.

### Tertiary (LOW confidence)

- None.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all components read directly from source; no external libraries.
- Architecture: HIGH — two tally sites, weight system, role enum, RPC signals all verified in code.
- Pitfalls: HIGH — derived from actual code structure (two tally sites, unassigned `Role::FULL`, Tier 1/2 naming overlap).
- Cohort weight semantics (50/25 meaning): MEDIUM — wording in phase goal + design note is ambiguous; flagged as A4 for user confirmation.

**Research date:** 2026-06-19
**Valid until:** 2026-07-19 (30 days; stable internal-architecture research — no external API drift)

---

## Recommended Plan Breakdown (for the planner)

> Not a section the planner consumes verbatim, but included to guide
> `/gsd:plan-phase`. Trim wave count if the user merges scope at discuss-phase.

**Proposed: 4 plans across 3 waves.**

### Wave 1 (foundation — parallel where independent)

- **06-01 — Cohort derivation + registry self-classification.** Add
  `ValidatorRegistry::CohortOf(validator_id) -> {DIRECT_API, PUBLIC_ONLY}` derived
  from registry state; add `has_direct_api` (or reuse `Role::FULL`) as the stored
  signal; wire `GeniusNode` to set the node's own cohort from RPC config
  (`is_paid`/`api_key_*` presence) at self-registration. No quorum change yet.
  Files: `ValidatorRegistry.{hpp,cpp}`, `GeniusNode.cpp`,
  `PublicChainInputValidator.hpp` (expose `HasDirectApiEndpoint()`).

### Wave 2 (quorum policy — depends on Wave 1)

- **06-02 — TwoTierQuorumPolicy + shared `EvaluateQuorum` helper.** Add the policy
  struct, the `IsBridgeMintSubject()` discriminator (`kMintV2` + `chain_id≠""`),
  and route **both** tally sites (`TallyVotes` + incremental `HandleVote`) through
  one `EvaluateQuorum`. Non-bridge subjects use the existing single-pool path
  unchanged. Extend `WeightConfig` with cohort thresholds. This is the riskiest
  plan — it touches shared consensus hot paths.
  Files: `Consensus.{hpp,cpp}`, `ValidatorRegistry.hpp` (WeightConfig).
- **06-03 — Role::FULL promotion (reputation).** Add the promotion rule in
  `ApplyVoteEffects` using existing `weight`/`penalty_score`/`missed_epochs` fields
  + new `full_promotion_weight_`. Closes the "FULL never assigned" gap.
  Files: `ValidatorRegistry.{hpp,cpp}`.
  *(Parallel to 06-02; both depend only on 06-01.)*

### Wave 3 (tests + integration — depends on Waves 1+2)

- **06-04 — Two-tier quorum + cohort + promotion tests.** New
  `consensus_two_tier_test.cpp` and `validator_registry_promotion_test.cpp`;
  extend `ConsensusManagerTestAccess` as needed. Covers all proposed REQ-COHORT,
  REQ-QUORUM, REQ-REPUT, REQ-DETERM behaviors. Full regression run per CLAUDE.md.

**Key files that will be modified:**

- `src/blockchain/Consensus.hpp` / `.cpp` (two tally sites + new helper + policy)
- `src/blockchain/ValidatorRegistry.hpp` / `.cpp` (cohort derivation, WeightConfig
  extension, FULL promotion)
- `src/account/GeniusNode.cpp` (self-classification wiring)
- `src/account/PublicChainInputValidator.hpp` (expose direct-endpoint presence)
- `test/src/blockchain/consensus_two_tier_test.cpp` (NEW)
- `test/src/blockchain/validator_registry_promotion_test.cpp` (NEW)

**Key files that will NOT be modified (v1):**

- `src/blockchain/impl/proto/Consensus.proto` — certificate/vote schema unchanged
  (A1; needs user confirmation to break this constraint)
