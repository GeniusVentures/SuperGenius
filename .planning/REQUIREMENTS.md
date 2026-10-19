# Requirements: SuperGenius — v1.3 Security Remediation & Network Readiness

**Defined:** 2026-10-19
**Core Value:** One external burn must produce at most one authoritative certificate and one mint effect, even when proposals, certificates, and CRDT data arrive in different orders or nodes restart — and nothing an unauthenticated peer sends can change that.

**Evidence base:** `.planning/notes/security-assessment-2026-10-response.md` (25 findings, developer credibility triage). Each requirement carries its finding number(s).
**Driver:** pre-public-network gate. VER-01 is the verification spine for every other requirement.

## v1.3 Requirements

### Verification & Release Engineering

- [ ] **VER-01**: An adversarial regression suite exists as a first-class ctest target set — every security fix ships a test naming the attacker authority required, the expected rejection, and the actual behavior, and the suite gates releases
- [ ] **REL-01**: CI publishes releases only after required tests pass; release tags are immutable; a documented hotfix path exists (#20)
- [ ] **REL-02**: CI verifies thirdparty/zkLLVM artifact digests (GitHub API digest or committed checksum manifest) and pins workflow actions by SHA (#19)
- [ ] **REL-03**: Fork changes (libp2p, ipfs-pubsub) carry a FORK-DELTA manifest and a mixed-version interop matrix before riding the release pipeline

### Consensus Trust

- [ ] **TRUST-01**: Certificate paths loading a validator registry by CID verify the registry's authorization — forged-registry self-certification is rejected — without breaking catch-up sync or historical certificate re-validation (#1)
- [ ] **TRUST-02**: Validator admission growth bounds and cross-certificate equivocation detection are audited, with findings documented (#14)

### Mint & Escrow Authorization

- [ ] **MINT-01**: Escrow release requires entitled authority (certified task result); a signature from the spending key alone at the lock-address exception is rejected (#2)
- [ ] **MINT-02**: Public-chain evidence validation fails closed on empty/local chain identifiers and empty source references; native-mint and bridge-mint follow explicitly separated per-type consensus rules (#3)
- [ ] **MINT-03**: Legacy-v1 mint reception is disabled or mint authority enforced via admission gating — never deserialization gating, so boot/sync/history spends keep working (#4)
- [ ] **MINT-04**: A burn receipt's token ID, burned amount, and destination are bound to the mint's outputs — even fully honest RPC endpoints cannot authorize a mismatched mint (#5)

### Bridge Ingestion Safety

- [ ] **BRIDGE-01**: Irreversible native issuance requires source-chain confirmation depth/finality, proven reorg-safe under an anvil reorg test (#10)
- [ ] **BRIDGE-02**: Multi-endpoint RPC corroboration requires agreeing block/event content across distinct operators — not endpoint-URL-hash success counts (#11)

### Wire & Ingestion Hardening

- [ ] **WIRE-01**: All fork parse boundaries (uvarint, big-endian, Kademlia readers) cap length and nesting depth before allocation (#8)
- [ ] **WIRE-02**: Transaction deserialization returns errors on malformed hashes (no `.value()` on network-derived data), with fuzz coverage of the ingestion path (#9)

### Gossip & DHT Authentication

- [ ] **GOSSIP-01**: Public-network gossip messages are signature-verified before mesh forwarding, with signing-key↔`from` identity binding and per-topic policy; before/after throughput is benchmarked (#7)
- [ ] **GOSSIP-02**: DHT stores and retrieves only validator-passed records with admission/resource limits; nonce responses are bound to outstanding requests (#15)

### Processing-Grid Integrity

- [ ] **GRID-01**: Queue updates, task-result ownership, payout addresses, and developer splits are authenticated — not accepted by timestamp or self-assertion (#13)
- [ ] **GRID-02**: Proof verification is enforced on the active ingress path (the `valid_proof = true` bypass is removed), with a proof-format-drift inventory and staged rollout so stored-proof drift does not tombstone valid transactions (#21)
- [ ] **GRID-03**: Worker results are independently verified before payout via the deterministic/redundant execution tier (redundant-worker agreement); ZK/TEE tiers are deferred to v2 (#6)
- [ ] **GRID-04**: Escrow sufficiency is independently validated against the workload dimensions before worker acceptance (#22)

### Topology · Isolation · Mobile

- [ ] **TOPO-01**: Relay/hole-punch is completed limits-first (circuit-relay-v2 reservation caps, per-peer/IP limits) **or** the relay-through-full-nodes topology is published as an explicit supported contract (#17)
- [ ] **PRIV-01**: The job privacy boundary is explicitly defined (a documented no-confidentiality-without-TEE statement) with scoped controls for job metadata and input access (#23)
- [ ] **MOB-01**: Mobile nodes have adaptive connection/data budgets, metered-data rules, and demonstrated OS-termination recovery (#25)

### SDK · On-Chain Audit · Return Leg

- [ ] **SDK-01**: GeniusSDK fixed-buffer copies are length-checked and memory-safe; ownership conventions are documented; the three referenced-but-missing test targets are implemented and run (#24)
- [ ] **SDK-02**: A minimal worker profile exists with measured CPU/GPU duty-cycle controls (#18)
- [ ] **EVM-01**: Deployed EVM privileged roles (owner/minter/upgrader) and bridge selectors are audited directly on-chain, with results and a remediation list documented (#12)
- [ ] **RET-01**: The SGNS→EVM return leg is activated end-to-end on the certificate-based attestor path (new exporter module feeding GNUSBridgeAttestor) (#16)

## v2 Requirements

Deferred beyond v1.3. Tracked, not in the current roadmap.

### Result Verification (deep tiers)

- **GRID-V2-01**: ZK-proof-based result verification for nondeterministic workloads
- **GRID-V2-02**: TEE-based executor attestation

### Confidentiality

- **PRIV-V2-01**: Confidential job execution against the executor (requires TEE or equivalent)

### Smart-Contract Hardening (contracts repo)

- **EVM-V2-01**: Two-step ownership transfer; admin/upgrader role separation
- **EVM-V2-02**: Holder-allowance requirement for burning held tokens; owner limiter-exception removal

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Smart-contract code changes in the GNUS contracts repo | Separate change stream and release cadence; #12 is covered as an on-chain operational audit only. Hardening tracked as EVM-V2-01/02 |
| Rewriting the consensus architecture | The assessment's defects are boundary/authorization gaps in existing modules, not architecture flaws; the "wrong architecture / 12–18 months" verdict is not accepted as established |
| v1.2 light-node pruning work | Executes on its own branch; this branch's v1.3 planning assumes its existence, not its absence |
| Public-network exposure before the gate | The milestone's own driver: no exposure until VER-01 gates the fixed paths |
| New node roles, consensus redesign, or tokenomics changes | Not required by any of the 25 findings |
| Light nodes serving historical sync to other peers | Carried over as a standing v1.2-era boundary |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| (to be filled by roadmapper) | | |

**Coverage:**
- v1.3 requirements: 26 total
- Mapped to phases: 0
- Unmapped: 26 ⚠️ (roadmap creation pending)

---
*Requirements defined: 2026-10-19 after v1.3 milestone research synthesis*
