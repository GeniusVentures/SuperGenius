# Phase 21: Consensus Trust & Mint Authorization - Context

**Gathered:** 2026-10-19
**Status:** Ready for planning

<domain>
## Phase Boundary

Close the certificate trust-anchor bypass and the mint authorization gaps from the October 2026 security assessment, fail-closed, without orphaning legitimate state:

- **TRUST-01 (#1):** certificate paths loading a validator registry by CID verify the registry's authorization — forged-registry self-certification rejected — without breaking catch-up sync or historical certificate re-validation.
- **TRUST-02 (#14):** validator admission growth bounds and cross-certificate equivocation audited, findings documented, cheap guards shipped.
- **MINT-02 (#3):** public-chain evidence validation fails closed on empty/local chain identifiers and empty source references; native-mint vs bridge-mint follow explicitly separated per-type consensus rules.
- **MINT-03 (#4):** legacy-v1 mint reception disabled via admission gating — never deserialization gating.
- **MINT-04 (#5):** a burn receipt's token ID, burned amount, and destination are bound to the mint's outputs.
- **Folded todo:** secure trusted-peer genesis configuration (local trust-root hardening).

Out of scope: MINT-01 escrow entitlement (Phase 26), BRIDGE-01/02 confirmation-depth and RPC corroboration (Phase 24), VER/REL apparatus (Phase 25, though this phase's fixes carry their own adversarial+compat test pairs).

</domain>

<decisions>
## Implementation Decisions

### Trust anchor & grandfathering
- **D-01:** **Finality-bound grandfather rule** for TRUST-01. A certificate that already reached durable finality is never re-checked against the new registry-authorization rule — finality itself is the proof of acceptance. Only in-flight/new certificates take the strict verified-registry path. No persisted markers, no migration pass; restart/sync/merge of old history behaves exactly as today. Accepted residual: a pre-upgrade forged cert that somehow finalized stays final (it is final under today's rules too).

### Mint authorization
- **D-02:** **Hard-disable legacy-v1 mint at admission** for MINT-03. Newly submitted v1 mint transactions are rejected at every ingress (consensus ingress, sync admission, RPC submission). Pre-existing v1-mint outputs remain spendable. No authority allowlist or config flag is built — no machinery to misconfigure.

### Authorization verdict behavior
- **D-03:** **Reject / Defer(bounded) / Accept verdict model** at every new authorization boundary this phase introduces. When referenced state (registry update, quorum evidence) has not arrived locally: defer-and-hold, retrying as dependencies land (registry-update callback / new CRDT head); reject only on deadline expiry, with buffer-full oldest-first eviction as the memory backstop. The deferral window is a config value in `network_config.json` (beside the existing reconnect/watermark knobs, per the v1.0 config-wins-with-safe-defaults pattern) with a **generous ~30-minute default** and a test/dev override so multi-node suites stay fast. User emphasis: the window must be considerable so valid late-arriving dependencies are never wrongly rejected.

### Validator admission
- **D-04:** **Audit + cheap guards** for TRUST-02. The documented admission/equivocation audit ships together with the inexpensive guards it identifies — expected shapes: a configurable cap on ACTIVE validator additions per certified registry update, and a cross-certificate equivocation check reusing the existing durable one-vote-per-slot ledger. The audit's findings get teeth in-phase, not a follow-up.

### Claude's Discretion
- Verification mechanism for the strict registry path (full update-chain replay vs. anchored delta from last-confirmed state) — replay reusing ValidatorRegistry's existing same-file machinery is the likely shape; researcher validates sync-time cost.
- Placement/ownership of the shared verdict primitive (which owning module exposes Reject/Defer(bounded)/Accept) — planner decides with the one-owning-module rule.
- Naming and taxonomy of the per-type mint consensus rules (MINT-02's "explicitly separated" rules).
- Exact eviction policy details beyond oldest-first-under-pressure.
- How the folded trusted-peer-genesis-config hardening decomposes (pinned manifest hash, persisted genesis identity, restart-time mismatch check) — the todo's Problem section is the requirement; design is planner/researcher territory.

### Folded Todos
- **Secure trusted-peer genesis configuration** (`.planning/todos/pending/2026-08-10-secure-trusted-peer-genesis-configuration.md`, area: security) — folded into this phase by user selection. Problem: `trusted_peers`, `bootstrapper_node`, `trusted_peer_quorum_threshold`, and `burn_config_quorum_threshold` are loaded from mutable `sgns_config.json`; before TrustedPeerRegistry genesis is confirmed, the configured bootstrapper is sole signer and the peer list is copied into the live cache with no pinned manifest hash, persisted genesis identity, or restart-time mismatch check — an attacker who can replace the JSON and supply matching keys redefines the node's trust root. Fits this phase as trust-anchor hardening alongside TRUST-01. Files named by the todo: `src/account/GeniusNode.cpp:438`, `src/account/GeniusNode.cpp:758`, `src/trustedpeer/TrustedPeerRegistry.cpp:138`, `src/account/BurnConfig.cpp:95`, `src/securecrdt/QuorumThresholdValidation.hpp:29`, `example/node_test/sgns_config.json:10`.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Assessment evidence (what we are fixing and why)
- `.planning/notes/security-assessment-2026-10-response.md` — the 25 findings with developer credibility triage; findings #1, #3, #4, #5, #14 are this phase's subject matter, with file references (ValidatorRegistry.cpp, Consensus.cpp, PublicChainInputValidator.cpp, TransactionConsensusHandler.cpp, MintTransaction.cpp).

### Milestone research (validated during roadmap creation)
- `.planning/research/ARCHITECTURE.md` — owning-module mapping with file:line evidence: `ValidatorRegistry::LoadRegistryByCid` (ValidatorRegistry.cpp:785) vs. update-chain verification machinery already in the same file (:1111, :1183, :1255, :1581); ConsensusManager CID cache (Consensus.cpp:2589) must become verified-only; ValidateCertificate re-validation sites (Consensus.cpp:2712, :3244, :3347, :3390); `PublicChainInputValidator::GatherVerificationEvidence` (PublicChainInputValidator.cpp:307–490) — fail-open branches :319/:329 and receipt-content binding gap :410–452 are this phase's MINT-02/MINT-04 (the confirmation-depth and RPC-corroboration halves belong to Phase 24).
- `.planning/research/PITFALLS.md` — verdict-model rationale (Reject/Defer(bounded)/Accept), state-orphaning traps (historical re-validation, legacy-mint cutover, tombstone path), and the CRDT-propagation-order vs. fail-closed tension behind D-03.
- `.planning/research/FEATURES.md` — ecosystem correct-patterns: Tendermint-style signed validator-set binding for TRUST-01; Wormhole-style exact attestation-payload binding for MINT-04.
- `.planning/research/SUMMARY.md` — synthesized build order and the four hard ordering constraints.
- `.planning/research/STACK.md` — what NOT to add (all remediation machinery is in-tree).

### Codebase intelligence
- `.planning/codebase/CONCERNS.md` — god-file safe-modification guidance for ValidatorRegistry.cpp / Consensus.cpp / TransactionManager (prefer new cohesive classes injected via constructor, e.g. the BridgeConsensusAdapter pattern; read the full method before modifying; check for dead code below any return).
- `.planning/todos/pending/2026-08-10-secure-trusted-peer-genesis-configuration.md` — the folded todo's full problem statement.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **ValidatorRegistry update-chain verification machinery** (same file, four sites) — the authorization check for LoadRegistryByCid should reuse this, not invent a parallel verifier.
- **Durable one-vote-per-slot ledger** (v3.0 work) — the cross-certificate equivocation guard for TRUST-02 reuses it rather than building new detection.
- **`ConsensusAuth` primitives** (signing-bytes/SHA-256/VerifySignature) — per the v1.1 key decision, use these directly, not ConsensusManager's proposal/vote/certificate lifecycle.
- **Phase-8 canonical burn identity** — carries the facts MINT-04 must bind (token ID, amount, destination) from receipt to mint outputs.
- **`network_config.json` config-read pattern** (v1.0: config-wins precedence, safe defaults, port-resolution Doxygen) — D-03's deferral window and D-04's admission cap follow it.

### Established Patterns
- `outcome::result<T>` error propagation — all new fallible paths; no blank error codes (CONCERNS.md tech debt).
- Adversarial + compat test-pair discipline — every fix here ships both (named attacker authority → expected rejection → actual behavior, plus a twin proving the pre-upgrade legitimate path survives); formalized as a suite only in Phase 25.
- Admission gating over deserialization gating — D-02's mechanism, matching the requirement's constraint that boot/sync/history keep working.
- BridgeConsensusAdapter-style constructor injection — the safe way to add authorization logic near TransactionManager without growing the god class.

### Integration Points
- `ConsensusManager::ValidateCertificate` re-validation call sites (restart :2712, sync :3244, merge :3347, vote :3390) — every behavior change here must survive all four.
- `ValidatorRegistry::LoadRegistryByCid` (8 call sites) and the ConsensusManager CID cache (:2589) — the cache must serve verified-only data or it re-opens finding #1.
- `PublicChainInputValidator::GatherVerificationEvidence` — MINT-02's fail-open branches and MINT-04's receipt binding land here; coordinate with Phase 24 which rewrites the same function's finality/corroboration halves.
- Transaction-type admission dispatch (`transaction_parsers` map) — where the v1-mint hard-disable hooks.

</code_context>

<specifics>
## Specific Ideas

- User's explicit emphasis on D-03: "the time has to be considerable so we don't wrongly reject valid late dependencies" — when in doubt, lengthen the window; correctness of legitimate traffic outranks buffer hygiene.
- No other specific references — open to standard approaches elsewhere.

</specifics>

<deferred>
## Deferred Ideas

None raised during discussion — discussion stayed within phase scope.

### Reviewed Todos (not folded)
- **bridge_race fixture — not all 11 nodes mint within the 90s race window** (`.planning/todos/pending/bridge-race-not-all-11-mint-within-window.md`) — reviewed as a possible Phase 21 fold (exact-once mint behavior); left in pending as test-infrastructure follow-up; carries a stale `resolves_phase: 08` from a prior milestone.
- **Bridge Startup Wiring + Mock RPC Endpoints** (`.planning/todos/pending/bridge-startup-wiring-mock-rpc.md`) — reviewed; deliberately tagged `resolves_phase: 24` during v1.3 roadmap approval; belongs to Phase 24's bridge ingestion work.

</deferred>

---

*Phase: 21-Consensus Trust & Mint Authorization*
*Context gathered: 2026-10-19*
