# Phase 13: Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-11
**Phase:** 13-Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps
**Areas discussed:** Genesis authority and ceremony, Quorum policy, Production operations, Startup and recovery

---

## Todo Cross-Reference

| Option | Description | Selected |
|--------|-------------|----------|
| Fold it | Carry the bootstrap-manifest, quorum-policy, validation, and tamper-test requirements into Phase 13. | ✓ |
| Leave separate | Keep the security todo pending for later work. | |

**User's choice:** Fold “Secure trusted-peer genesis configuration” into Phase 13.
**Notes:** Two bridge-related fuzzy matches were reviewed as unrelated and left separate.

---

## Genesis Authority and Ceremony

### Initial trust boundary

| Option | Description | Selected |
|--------|-------------|----------|
| Trusted configuration on every boot | Remote malicious peers are in scope; local JSON tampering is not. | |
| Trust on first boot, then pin confirmed state | Trust initial operator configuration; reject later edits, rollback, or mismatches. | ✓ |
| Authenticated configuration before first boot | Protect fresh nodes using a hash or public key stored outside mutable JSON. | |
| Other | User-defined trust boundary. | |

**User's choice:** Trust on first boot, then pin confirmed state.
**Notes:** The user clarified that a bad actor changing only its own `bootstrapper_node` should not alter honest nodes' trust. Code inspection showed that structurally valid remote values enter CRDT but unauthorized signatures do not count during `ReadIfQuorum()`, separating safety from possible liveness pressure.

### Persisted genesis identity

| Option | Description | Selected |
|--------|-------------|----------|
| Canonical genesis fingerprint | Bind network ID, bootstrapper public key, ordered peers, policy version, and initial quorum policies. | ✓ |
| Confirmed TPR payload hash only | Pin only the initial peer-list payload. | |
| Peer list plus bootstrapper key | Protect membership and bootstrap authority but leave policy outside the identity. | |
| Other | User-defined identity. | |

**User's choice:** Canonical genesis fingerprint.
**Notes:** This fingerprint becomes authoritative after first confirmation.

### Bootstrap invocation

| Option | Description | Selected |
|--------|-------------|----------|
| Dedicated one-shot genesis command | Sign, submit, confirm, and remove ephemeral key material outside normal startup. | ✓ |
| Bootstrapper mode in the node | Temporarily start a node with bootstrap authority and key material. | |
| Pre-signed artifact import | Produce a signature offline and publish it from a node that never receives the private key. | |
| Other | User-defined ceremony. | |

**User's choice:** Dedicated one-shot genesis command.
**Notes:** Normal node startup must never receive or persist the ephemeral private key.

### Initial peer verification

| Option | Description | Selected |
|--------|-------------|----------|
| Proof of ownership plus independent review | Enrollment signatures and at least two reviewers. | |
| Proof of ownership only | Enrollment signatures with one operator assembling the list. | |
| Manual out-of-band verification only | Collect public keys through trusted channels without enrollment signatures. | ✓ |
| Other | User-defined verification. | |

**User's choice:** Manual out-of-band verification.
**Notes:** The user has always viewed this as a manual project effort: ask trusted participants for their public keys and insert them manually. Tooling still validates format, uniqueness, non-empty membership, canonical order, and fingerprint display. Private keys are never requested.

---

## Quorum Policy

### Policy authority

| Option | Description | Selected |
|--------|-------------|----------|
| Versioned quorum policy state | Persist genesis thresholds and require current-quorum approval for later changes. | ✓ |
| Deterministic formulas in code | Derive all effective thresholds from signer count. | |
| Hybrid policy | Derive membership threshold while storing BurnConfig threshold in governed state. | |
| Other | User-defined authority. | |

**User's choice:** Versioned quorum policy state.
**Notes:** JSON may carry bootstrap copies or diagnostics but cannot override confirmed policy.

### Trusted-peer membership floor

| Option | Description | Selected |
|--------|-------------|----------|
| Strict majority | Require `floor(M/2)+1`; retain reasonable liveness. | ✓ |
| Two-thirds supermajority | Stronger takeover resistance with less availability. | |
| Unanimous | Require all current peers. | |
| Other | User-defined invariant. | |

**User's choice:** Strict majority.
**Notes:** Preserves the prior Phase 11 minimum-safety decision using the clearer strict-majority formula.

### BurnConfig floor

| Option | Description | Selected |
|--------|-------------|----------|
| Two-thirds supermajority | Require `ceil(2M/3)` at minimum for economic-policy changes. | ✓ |
| Strict majority | Match the membership threshold floor. | |
| Unanimous | Require every current trusted peer. | |
| Other | User-defined invariant. | |

**User's choice:** Two-thirds supermajority.
**Notes:** Signed policy may require more but never less.

### Policy transition

| Option | Description | Selected |
|--------|-------------|----------|
| Current policy authorizes its successor | Link previous hash and increasing version; activate atomically after current-policy quorum. | ✓ |
| Both old and new quorums approve | Require authorization under current and replacement policy. | |
| Proposed policy authorizes itself | Evaluate with the candidate's own signer set and threshold. | |
| Other | User-defined transition. | |

**User's choice:** Current policy authorizes its successor.
**Notes:** Prevents circular threshold lowering and signer-set self-authorization.

---

## Production Operations

### Candidate and approval workflow

| Option | Description | Selected |
|--------|-------------|----------|
| Local administrative CLI | One-shot local propose/sign commands using locally controlled keys. | |
| Offline signed artifacts | Prepare proposal files offline, then import/publish them. | |
| Authenticated JSON-RPC administration API | Remote propose/sign/confirm operations. | |
| Callback-driven operator approval | Running nodes observe SecureCrdt candidates and request explicit local approval from trusted-peer operators. | ✓ |

**User's choice:** Callback-driven operator approval.
**Notes:** The user specified that a node writes a new value, trusted-peer nodes receive it, and their users decide whether to sign and approve. Receiving a value never signs automatically.

### Candidate publisher authorization

| Option | Description | Selected |
|--------|-------------|----------|
| Current trusted peers only | Require a currently trusted peer to authenticate the candidate envelope. | ✓ |
| Any network node | Allow open proposals while counting only trusted signatures. | |
| Separate proposer allowlist | Govern proposal and approval authority separately. | |
| Other | User-defined authorization. | |

**User's choice:** Current trusted peers only.
**Notes:** Prevents arbitrary structurally valid writes from becoming retained operator-visible candidates.

### Proposer approval

| Option | Description | Selected |
|--------|-------------|----------|
| Explicit proposal counts as approval | The intentional write's signature is the proposer's first quorum approval. | ✓ |
| Proposal and approval remain separate | Require a second action from the proposer before its signature counts. | |
| Policy-selectable | Let each record type choose. | |
| Other | User-defined behavior. | |

**User's choice:** Explicit proposal counts as approval.
**Notes:** Relaying or receiving a proposal never counts as approval.

### Candidate representation and concurrency

| Option | Description | Selected |
|--------|-------------|----------|
| Content-addressed candidates | Store each candidate and its signatures under the exact candidate hash/version. | ✓ |
| Single mutable pending slot | Let new values replace the prior candidate. | |
| One active proposal lock | Reject new candidates until the active one resolves. | |
| Direct SecureCrdt write without a separate proposal layer | User clarification: the Safe/SecureCrdt record itself is the candidate. | ✓ |

**User's choice:** Direct, content-addressed SecureCrdt candidates; no separate proposal protocol.
**Notes:** The user questioned whether a proposal abstraction was needed. The agreed model treats “proposal” as a name for an unconfirmed SecureCrdt value. Candidate-specific keys keep signatures bound to the exact approved bytes and prevent mutable-base overwrite races.

---

## Startup and Recovery

### Fresh-node behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Restricted bootstrap mode | Permit networking/CRDT sync but disable policy authority and BurnConfig-dependent economic operations. | ✓ |
| Provisional operation from configuration | Treat first-boot JSON peers and policies as active until CRDT confirms them. | |
| Refuse all startup | Exit unless genesis is already stored locally. | |
| Other | User-defined behavior. | |

**User's choice:** Restricted bootstrap mode.
**Notes:** Configured peers are bootstrap input, not an active quorum before confirmation.

### Restart configuration mismatch

| Option | Description | Selected |
|--------|-------------|----------|
| Use persisted state and raise a critical error | Ignore changed trust fields; continue confirmed state; fail on network-ID mismatch. | ✓ |
| Fail startup on any mismatch | Require exact restoration of original configuration. | |
| Enter restricted recovery mode | Disable sensitive operations until operator resolution. | |
| Other | User-defined behavior. | |

**User's choice:** Use persisted state and raise a critical error.
**Notes:** Persisted confirmed identity becomes authoritative after first boot.

### Rollback and fork handling

| Option | Description | Selected |
|--------|-------------|----------|
| Keep last-known-good and reject rollback/forks | Accept only linked, correctly versioned successors. | ✓ |
| Accept any state with a valid quorum | Allow older quorum-signed state to replace current state. | |
| Enter restricted recovery mode | Stop sensitive operations whenever CRDT cannot reproduce local state. | |
| Other | User-defined behavior. | |

**User's choice:** Keep last-known-good and reject rollback/forks.
**Notes:** Older, conflicting, non-descendant, or missing CRDT data cannot erase confirmed state.

### Account-selection lifecycle

| Option | Description | Selected |
|--------|-------------|----------|
| Keep policy components node-scoped and stable | Construct/register once per GeniusNode/GlobalDB lifetime. | ✓ |
| Explicitly unregister and recreate | Tear down and replace callbacks during every account switch. | |
| Recreate GlobalDB too | Rebuild the entire CRDT stack per account. | |
| Other | User-defined ownership. | |

**User's choice:** Keep policy components node-scoped and stable.
**Notes:** Trusted-peer and burn policy are network-scoped. Recreated `TransactionManager` instances consume the existing BurnConfig cache.

---

## The agent's Discretion

- Deterministic encoding and local persistence mechanism for genesis fingerprints and policy records.
- Exact local UI/command used to present candidates and capture explicit operator approval.
- Candidate expiration and garbage collection after activation.
- One-shot genesis command naming and command-line syntax.

## Deferred Ideas

- No new product capabilities were deferred.
- Two unrelated bridge todos were reviewed and left in their existing backlog.
