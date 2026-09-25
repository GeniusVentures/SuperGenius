# Phase 13: Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps - Context

**Gathered:** 2026-08-11
**Status:** Ready for planning

<domain>
## Phase Boundary

Close the v1.1 product-level trust and integration gaps around `TrustedPeerRegistry`, quorum-governed policy, and `BurnConfig`. This phase delivers a manual one-time trusted-peer genesis ceremony, first-boot confirmation with persisted genesis identity, authenticated/versioned quorum policy, production SecureCrdt candidate-and-approval flows for membership and burn changes, rollback-safe restart behavior, and node-scoped callback ownership across account selection. It also adds the validation and tamper/E2E coverage required to prove those paths.

The phase reuses CRDT as the only transport and does not add a separate proposal protocol, pubsub channel, consensus lifecycle, or remote administration RPC. It does not broaden the previously narrowed Phase 12 `ValidatorRegistry` migration.

</domain>

<decisions>
## Implementation Decisions

### Genesis authority and ceremony
- **D-01 — Trust boundary:** Initial bootstrap configuration is trusted on first boot because the project operator manually obtains the real trusted participants' public keys/addresses through already-trusted channels. Once genesis is confirmed, mutable JSON is no longer authoritative for bootstrap membership or quorum policy.
- **D-02 — Confirmed identity:** Persist a canonical genesis fingerprint binding the network ID, ephemeral bootstrapper public key, canonically ordered initial peer list, policy version, and initial TPR/BurnConfig quorum-policy values. This persisted identity becomes the restart trust anchor.
- **D-03 — One-shot bootstrap command:** Genesis is created by a dedicated one-shot command/tool. It accepts the reviewed genesis input and ephemeral bootstrapper private key, signs and submits the TPR genesis record through the normal SecureCrdt flow, verifies confirmation, and removes the ephemeral key material. Normal node startup never receives or stores that private key.
- **D-04 — Manual peer collection:** No enrollment-signature or multi-person approval workflow is required. Tooling must still reject empty, duplicate, or malformed public keys, canonicalize ordering, and display the final peer list and fingerprint for explicit operator review. Private keys are never collected from trusted participants. Current example addresses must be labeled non-production placeholders.

### Quorum policy
- **D-05 — Authoritative policy state:** Both the trusted-peer membership threshold and BurnConfig threshold live in persisted, versioned quorum-policy state initialized by the canonical genesis input. Locally editable JSON may expose copies for diagnostics or bootstrap input but cannot override confirmed policy.
- **D-06 — Membership safety floor:** TrustedPeerRegistry membership changes require at least a strict majority of the current signer set: `floor(M / 2) + 1`, with a non-empty unique valid signer set and `1 <= threshold <= M`. A signed policy may require more but never less.
- **D-07 — Burn safety floor:** BurnConfig changes require at least `ceil(2M / 3)` of the current trusted-peer signer set. A signed policy may require more but never less.
- **D-08 — Successor authorization:** The currently confirmed policy authorizes its successor. Every candidate carries the expected previous-state hash and a monotonically increasing version; signatures are evaluated under the current signer set and thresholds, and the successor activates atomically only after current-policy quorum. A proposed policy never authorizes itself.

### Production SecureCrdt operations
- **D-09 — Direct candidate writes:** There is no separate proposal subsystem. Proposing means explicitly writing a candidate TrustedPeerRegistry or BurnConfig value into SecureCrdt. CRDT callbacks surface new candidates to trusted-peer nodes; receiving or relaying a candidate never auto-signs it.
- **D-10 — Authorized proposers:** Honest nodes retain and surface candidates only when the candidate envelope is authenticated by a member of the currently confirmed trusted-peer set. This closes the current path where structurally valid writes from arbitrary peers can enter the candidate slot.
- **D-11 — Explicit local approval:** Trusted-peer operators decide locally whether to approve a detected candidate. Approval signs the exact canonical candidate bytes and publishes that signature through SecureCrdt. An explicit operator-confirmed proposal write counts as the proposer's first approval; arbitrary future values are never auto-approved.
- **D-12 — Content-addressed candidates:** Candidate keys include the candidate content hash/version, and approval signatures are stored beneath that exact candidate key. Multiple candidates may coexist without overwriting one another. For a given expected previous-state hash/version, the first valid candidate to reach current-policy quorum becomes effective and competing candidates become stale.

### Startup, persistence, and callback lifecycle
- **D-13 — Restricted first boot:** A fresh node may start networking and CRDT synchronization before TPR genesis confirmation, but it must not treat configured peers as an active policy quorum, approve policy changes, or perform BurnConfig-dependent economic operations until genesis is confirmed.
- **D-14 — Restart authority:** On restart, valid persisted confirmed state is authoritative. Conflicting `trusted_peers`, `bootstrapper_node`, or quorum fields from mutable JSON are ignored and produce a critical diagnostic; a network-ID mismatch still fails startup.
- **D-15 — Rollback/fork rejection:** Persisted last-known-good state cannot be replaced by older, missing, conflicting, or non-descendant CRDT state. Only a correctly versioned successor linked to the persisted current hash and confirmed under current policy may activate. Rejected rollback/fork data raises an operational alert without erasing last-known-good state.
- **D-16 — Node-scoped ownership:** `SecureCrdt`, `TrustedPeerRegistry`, `BurnConfig`, their registrations, and the confirmed policy cache live for the `GeniusNode`/GlobalDB lifetime rather than the selected account's lifetime. Callbacks register once; `SelectAccount()` may recreate `TransactionManager`, which consumes the existing BurnConfig state without recreating network-policy components.

### The agent's Discretion
- Exact binary encoding and local storage path for the canonical genesis fingerprint and versioned policy records, provided hashing is deterministic and crash-safe.
- Exact local operator presentation mechanism for detected candidates (interactive node UI, local administrative command, or equivalent), provided it is not a remotely exposed unauthenticated API and approval remains explicit.
- Candidate retention, expiration, and garbage-collection details after a winner activates, provided stale candidates can never later become effective against a different previous-state hash/version.
- Exact naming and command-line syntax for the one-shot genesis tool.

### Folded Todos
- **Secure trusted-peer genesis configuration** (`.planning/todos/pending/2026-08-10-secure-trusted-peer-genesis-configuration.md`) — folds BOOT-01..04, POLICY-01, VALID-01, and TEST-01 into this phase: document the manual ceremony and placeholder status; create and persist the canonical genesis identity; remove mutable JSON as post-confirmation authority; enforce complete signer/threshold validation; and cover tampering, mismatch, rollback, restart, production genesis, and live BurnConfig behavior in tests.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Milestone intent and audit findings
- `.planning/PROJECT.md` — v1.1 goal, constraints, and prior architectural decisions for SecureCrdt, TrustedPeerRegistry, and BurnConfig.
- `.planning/REQUIREMENTS.md` — v1.1 MSIG/SCRDT/TPR/BURN requirements and traceability; status is stale and must be reconciled through Phase 13 verification.
- `.planning/STATE.md` — current milestone state and accumulated roadmap context.
- `.planning/v1.1-MILESTONE-AUDIT.md` — authoritative gap inventory: unwired TPR genesis, missing production ingress, BurnConfig callback loss, and mutable bootstrap/quorum configuration.
- `.planning/todos/pending/2026-08-10-secure-trusted-peer-genesis-configuration.md` — folded BOOT/POLICY/VALID/TEST security requirements.

### Prior locked phase decisions
- `.planning/phases/10-trustedpeerregistry/10-CONTEXT.md` — one-use ephemeral bootstrapper, whole-list membership updates, and cached current-peer signer source.
- `.planning/phases/11-burnconfig-quorum-wiring/11-CONTEXT.md` — separate TPR/Burn thresholds, genesis-only automatic behavior, TransactionManager cache, and GeniusNode wiring intent; Phase 13 supersedes mutable-JSON policy authority.
- `.planning/phases/12-validatorregistry-migration/12-CONTEXT.md` — preserves the narrow ValidatorRegistry signature-verification migration and excludes a deeper storage/quorum refactor.

### SecureCrdt and trusted-peer implementation
- `src/securecrdt/SecureCrdt.hpp` — existing propose, signature, quorum-read, and filter APIs to evolve for content-addressed candidates.
- `src/securecrdt/SecureCrdt.cpp` — current single-base-value layout; remote filters perform structural/signature checks while authorization is re-derived during quorum reads.
- `src/securecrdt/SecureCrdtRegistry.hpp` — key-pattern policy registration and dynamic signer-set-source contract.
- `src/securecrdt/QuorumThresholdValidation.hpp` — current majority-floor validation to extend for complete bounds and policy-specific floors.
- `src/trustedpeer/TrustedPeerRegistry.hpp` — current genesis, membership-change, confirmation, cache, and registration API.
- `src/trustedpeer/TrustedPeerRegistry.cpp` — current pre-confirmation bootstrapper signer source and cache transition after `TryConfirm()`.

### Product wiring and lifecycle
- `src/account/GeniusNode.cpp` — parses the four mutable trust fields, constructs TPR/BurnConfig, and recreates account-scoped components in `SelectAccount()`.
- `src/account/GeniusNode.hpp` — current trust configuration members and ownership/lifetime declarations.
- `src/account/BurnConfig.hpp` — BurnConfig payload, quorum signer source, cache target, and callback contract.
- `src/account/BurnConfig.cpp` — current genesis seeding and GlobalDB callback registration implementation.
- `src/account/TransactionManager.hpp` — BurnConfig-backed cached burn basis-points consumption surface.
- `src/account/TransactionManager.cpp` — `PayEscrow` burn calculation and account-specific manager lifecycle.
- `src/crdt/globaldb/globaldb.hpp` — element-filter and new-element callback registration APIs.
- `src/crdt/globaldb/globaldb.cpp` — callback registration behavior that currently retains the first pattern registration.
- `example/node_test/sgns_config.json` — provisional bootstrap values that must be clearly marked non-production and reconciled with the one-shot ceremony.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `TrustedPeerRegistry::SeedGenesis` / `TryConfirm`: retain the ordinary SecureCrdt signature-and-quorum path, but invoke it from the one-shot genesis flow and persist the resulting identity.
- `SecureCrdt::ProposeValue`, `AddSignature`, and `ReadIfQuorum`: reusable primitives for candidate values, exact-payload signatures, and reader-side quorum derivation; their key layout must become candidate-specific.
- `SecureCrdtRegistryEntry::signer_set_source`: natural point to resolve the currently confirmed, persisted policy rather than mutable JSON.
- `BurnConfig`'s atomic cache target: keep the live burn value available to recreated `TransactionManager` instances while moving callback ownership to node lifetime.

### Established Patterns
- CRDT remains the only transport: candidate values and signatures propagate through GlobalDB filters/callbacks, with no new pubsub, consensus, or RPC lifecycle.
- Signatures bind canonical payload bytes, so candidate hashing, previous-state linking, and version encoding must share one deterministic serialization.
- Reader-side quorum re-derivation remains authoritative; there is no trusted mutable "final" write. Persisted last-known-good identity constrains which quorum-confirmed successor is eligible.
- Outcome-based construction/startup errors and fail-closed validation match existing project conventions.

### Integration Points
- `GeniusNode` startup must distinguish fresh bootstrap, confirmed restart, mismatch, and rollback states before enabling transaction/economic behavior.
- The one-shot command must reach the same GlobalDB/SecureCrdt keyspace used by production nodes without placing the ephemeral private key in ordinary node configuration.
- TPR and quorum policy must become confirmed before BurnConfig policy and BurnConfig-dependent transaction behavior are enabled.
- `SelectAccount()` must stop destroying and recreating network-policy components; only account-specific consumers should be replaced.
- Tests must cover cross-node candidate propagation, explicit trusted-peer approval, quorum activation, altered JSON, bootstrapper replacement, threshold manipulation, restart persistence, rollback rejection, and account switching.

</code_context>

<specifics>
## Specific Ideas

The user's intended model is intentionally operational and manual at genesis: the project asks known trusted people for their public keys, inserts those addresses into the genesis input, uses one offline ephemeral bootstrapper key through a one-shot command to create the signed CRDT genesis record, and destroys that key after successful confirmation. A malicious peer changing only its own bootstrap configuration must not redefine honest nodes' trust; after first confirmation, each node's persisted genesis fingerprint prevents local configuration drift from silently redefining its view.

For later changes, the user wants SecureCrdt itself to be the proposal medium: one trusted peer writes and signs a candidate, other trusted peers detect it through callbacks, and their operators deliberately decide whether to add signatures. "Proposal" is naming for an unconfirmed SecureCrdt candidate, not a separate protocol.

</specifics>

<deferred>
## Deferred Ideas

No new product capabilities were deferred from the discussion.

### Reviewed Todos (not folded)
- **bridge_race fixture — not all 11 nodes mint within the 90s race window (post-fix)** — unrelated bridge-test timing issue; outside Phase 13's trust-policy boundary.
- **Bridge Startup Wiring + Mock RPC Endpoints** — unrelated bridge/RPC integration work; outside Phase 13 and still tracked separately.

</deferred>

---

*Phase: 13-Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps*
*Context gathered: 2026-08-11*
