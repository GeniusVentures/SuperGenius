# Feature Research

**Domain:** Security remediation & network-readiness capabilities for an L1 + distributed-compute + bridge platform (post-external-assessment)
**Researched:** 2026-10-19
**Confidence:** HIGH for ecosystem patterns (verified against libp2p specs, Tendermint/Cosmos docs, SLSA spec, Ethereum finality docs, bridge/compute-network practice); MEDIUM for project-fit notes (based on the assessment-response triage and PROJECT.md, not fresh code reads)

**Evidence base:** `.planning/notes/security-assessment-2026-10-response.md` (25 findings with credibility triage) and `.planning/PROJECT.md`. Findings are cited below as `#N`.

---

## How to Read This Document

The assessment response makes an explicit triage, and the roadmap must respect it. Every capability cluster below is tagged:

| Tag | Meaning | Findings |
|-----|---------|----------|
| **[VULN]** | Security-vulnerability fix — a trust-boundary bypass that must close before public exposure | #1–#9, #13, #21, #24 |
| **[HARDEN]** | Operational hardening — works today but is not defensible under adversarial/public operation | #15, #18, #19, #20, #25 |
| **[CAPABILITY]** | Unfinished product capability — a feature the network promises but does not yet deliver | #6, #16, #17, #22, #23 |

The response doc's own sequencing rule: safety boundaries (VULN + release gating) are verified **first**; capabilities come after, because their verification depends on the fixed boundaries. One cluster (**adversarial regression suite**) is cross-cutting — it is the verification spine for all three categories, not a standalone feature.

**What "table stakes" means here:** what any comparable system (L1, bridge, compute network, libp2p deployment) already does for this capability. A security fix below table stakes is itself a finding. **Differentiators** are where SuperGenius can credibly exceed practice — mostly by reusing machinery it already validated (SecureCRDT, TrustedPeerRegistry genesis/quorum, canonical certificate finality, Phase 12's real-socket fault-test apparatus).

---

## Feature Landscape

### Table Stakes — Cluster A: Adversarial Regression Testing (cross-cutting spine)

Not a numbered finding; it is the response doc's discipline requirement ("every test names the attacker authority required, expected rejection, and actual behavior") and PROJECT.md's v1.3 decision "adversarial regression suite is the verification spine."

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Negative/adversarial tests at every validation boundary | Bitcoin Core ships functional adversarial peer tests (invalid tx/block message suites); Ethereum runs Hive, a black-box harness that spawns real clients and feeds them malformed/adversarial consensus and p2p input. Node software without these is presumed vulnerable. | MEDIUM | SuperGenius already has the apparatus pattern: Phase 12's real-socket four-peer fault proof (TEST-01..06) ran production-path PubSub/CRDT/RocksDB/consensus/Mint-ingress. Extend it from fault-injection to attacker-injection. |
| Fuzzing of all deserialization/parse boundaries wired into CI | Table stakes since oss-fuzz; the protobuf CVE record (nested-group recursion DoS, CVE-2024-7254; PHP varint DoS, CVE-2026-6409) shows even the most-deployed parser library needs it. Trail of Bits guidance: never recurse on untrusted input. | MEDIUM | Targets: fork's uvarint/big-endian/Kademlia readers (#8), `TransferTransaction`/`EscrowTransaction` hash parsing (#9). libFuzzer targets in the fork + SuperGenius ingestion path. |
| Test metadata: attacker authority + expected rejection + actual behavior | Standard in security regression suites (a vuln fix without a named attack vector test is unverifiable). The response doc elevates it to house style. | LOW | Naming convention, not machinery. Cheap, high leverage. |
| Black-box network-level adversarial harness (Hive-style) | Ethereum's Hive is the reference: clients tested through their public wire surface, not internal helpers. | HIGH if built generic | PROJECT.md constraint already says multi-node tests must exercise production ingress, not direct local-author helper calls. A minimal "malicious peer" test node that replays forgeries (#1, #4, #5, #13) through real PubSub is the right scope; a generic Hive is not. |

### Table Stakes — Cluster B: Consensus Trust Hardening [VULN] (#1, #14)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Validator-set data loaded by reference is authorization-verified against a quorum-committed anchor | The canonical pattern is Tendermint/Cosmos: the hash of the next validator set is committed and signed inside each block header (`NextValidatorSetHash`), so any validator-set data a node fetches is checked against a signed digest — the light client safely tracks even 100% set turnover. No serious chain trusts a fetched blob because "a CID pointed at it." | MEDIUM-HIGH | Fix shape for `LoadRegistryByCid()`: a registry blob is authoritative only if it verifies as a link in the quorum-signed update chain rooted in the authenticated genesis registry. SuperGenius already validated the exact machinery needed: TrustedPeerRegistry's genesis-ceremony + quorum-signed successor updates (TPR-01..03) and `multisig::VerifyPayloadSignature`. The hard part the response doc flags: CRDT historical-CID retrieval **explicitly bypasses element filtering** — that bypass path must not be able to serve consensus-critical data unfiltered. |
| Equivocation/admission audit | Chains publish validator admission policy and equivocation/slashing responses (Ethereum, Cosmos). One-vote-per-slot finality is already validated (v3.0), so the residual is cross-certificate equivocation detection and admission-rate policy. | MEDIUM (audit) | #14 is mixed-credibility: admission already goes through certified updates. Deliverable is an audit + documented policy, not a new mechanism. Genuinely new slashing machinery would be a separate milestone. |

### Table Stakes — Cluster C: Mint & Escrow Authorization Binding [VULN] (#2–#5)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Fail-closed validation: no empty-identity success paths (#3) | Every bridge/issuance system's rule: unknown/unverifiable source state ⇒ reject. Fail-open on empty chain ID is the class of bug behind multiple nine-figure bridge losses (Nomad's initializable root, Wormhole's guardian-set confusion). | LOW-MEDIUM | `PublicChainInputValidator` returning success for empty/local chain IDs or empty source references must become explicit type-authorized paths. |
| Legacy/unsafe transaction types disabled or authority-enforced (#4) | Protocol deprecations on other chains ship as hard-fork cutoffs with explicit height/version gating; a legacy type that bypasses validation is never left registered. | LOW | Disable legacy-v1 mint reception (simplest) or route it through the same mint authority as v2. `CheckTransactionTypeRules()` returning true for parameter-less UTXO types is a default-allow anti-pattern to eliminate. |
| Attestation/receipt facts bound exactly to the effect they authorize (#5) | Wormhole/tBTC/CCIP pattern: the attestation commits to a full payload hash; the executor binds mint outputs to exactly that payload, so a valid receipt for X can never mint Y. This is the single most load-bearing bridge invariant. | MEDIUM | Bind decoded event's token ID + burned amount + destination into the authorized mint's outputs. Even honest RPCs can't compensate for missing this binding (response doc's own emphasis). |
| Escrow spend requires entitlement, not key possession (#2) | Escrow/release patterns in compute marketplaces authorize payout against a **state fact** (completed/accepted work), with the key as identity, not authority. | MEDIUM | The ownership exception in `UTXOManager`/`GeniusInputValidator` must additionally require a certified task result. Natural dependency anchor for #6/#13/#22 (see DAG). |
| Replay protection / one-shot accounting on authorization facts | Table stakes everywhere (nonces, certificate slot keys). | LOW | Already largely guaranteed by v3.0 canonical-slot certificates — reuse, don't reinvent. |

### Table Stakes — Cluster D: Bridge Ingestion Safety [VULN] (#10, #11)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Source-chain finality/confirmation depth before irreversible native issuance (#10) | Standard bridge practice: wait for source finality, not first inclusion. On Ethereum PoS that means finalized checkpoints (~2 epochs ≈ 12.8 min, per Casper FFG); bridges that skipped or shortcut finality waits are the canonical exploit class (Ronin, Nomad aftermath analyses; Jump Crypto's bridging/finality writeup). Configurable depth for non-finality chains. | MEDIUM | Add a finalized-vs-depth policy per source chain in `PublicChainInputValidator`; a successful receipt ≠ finalized burn. Handle out-of-order/reorg'd events explicitly. |
| Multi-endpoint corroboration compares **content**, not successes (#11) | Oracle-network practice: quorum of independent sources must agree on **identical data** (block hash at height, receipt fields, decoded event), with chain-ID checks and per-endpoint operator policy. Counting successful responses (even across URLs) is explicitly the anti-pattern. | MEDIUM | Recorded slot hashes must hash agreeing block/event content, not endpoint URLs. Weighted success counts ≠ independently corroborated state. |

### Table Stakes — Cluster E: Wire & Allocation Hardening [VULN] (#8, #9)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Maximum message size enforced before allocation at every parse boundary (#8) | Bitcoin caps protocol messages at 4 MiB before reading; every libp2p implementation caps frame/varint reads. The fork's uvarint, big-endian, and Kademlia readers allocating from untrusted length prefixes is off-pattern. | LOW-MEDIUM per site | Mechanical fixes, but the surface is broad across the fork. Introduce one max-size constant + per-reader enforcement; cap varints at 9–10 bytes. |
| Depth/nesting limits, not just size | Verified caveat: protobuf's own CVE history (CVE-2024-7254 nested groups; CVE-2026-6409 negative varints) shows **size caps alone are insufficient** — small deeply-nested messages exhaust the stack. | MEDIUM | Any recursive-descent parser in the fork and in tx deserialization needs a depth budget. |
| Safe deserialization: errors, not `.value()` crashes, on network data (#9) | Universal rule in adversarially-tested node software: no `std::optional`/expected-`.value()` on untrusted input paths. | LOW | `TransferTransaction`/`EscrowTransaction` hash-parsing returns error results. Pair with fuzzing (Cluster A). |

### Table Stakes — Cluster F: Gossip Receive-Path Signature Verification [VULN] (#7)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Verify-then-forward on public networks (Strict policy) | libp2p pubsub spec defines Strict signing policies; go/js/rust implementations default to strict signature verification on receive (unsigned messages rejected). Send-side signing without receive-side verification — SuperGenius's current state — is the Lax/legacy mode the spec marks "insecure, to be deprecated." | LOW-MEDIUM | Enforce the app-level signed envelope on public networks, not only when a private-network membership filter exists (Phase 15 machinery already provides the envelope; the gap is the public-path default). |
| Signer identity bound to claimed source | 2026 gossipsub CVE (StrictSign not binding message key to `from` peer ID) shows even strict modes must bind identity to key, or attackers spoof RSA-keyed peers. | LOW | When verifying, check the signing key corresponds to the claimed author/peer, not merely that a signature is valid. |
| Peer scoring / ignore-on-invalid | gossipsub v1.1 peer scoring is the reference Sybil/DoS countermeasure. | MEDIUM | Differentiator-tier: table stakes is verify-and-drop; scoring is the upgrade. |

### Table Stakes — Cluster G: DHT Admission/Validation [HARDEN→VULN-adjacent] (#15)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Per-namespace record validators; verify on retrieval and before storage | The libp2p Kademlia spec requires DHT entries be validated at both points via a supplied record Validator (e.g., IPNS records signed by the key that is the record key). The fork's default validator accepting arbitrary values is off-spec. A 2026 `@libp2p/kad-dht` CVE (unauthenticated PUT_VALUE records landing in the datastore → disk exhaustion) demonstrates the exact failure mode. | MEDIUM | Fork work: install real validators for stored namespaces; reject unvalidated PUT_VALUE. |
| Storage/resource quotas (record size, count, TTL) | Standard in every DHT deployment (IPFS caps record size/replication). | LOW-MEDIUM | Bounds per namespace; evict on TTL. |
| Request-bound authenticated responses | Unsolicited responses must not mutate request state; nonce/account responses must be bound to the request and authenticated. | MEDIUM | `AccountMessenger` split: admission/resource controls vs authenticated request-bound responses. The nonce-poisoning exploit is credible but unproven per the response doc — write the targeted test first (Cluster A). |

### Table Stakes — Cluster H: Authenticated Processing-Queue/Payout Authority [VULN] (#13)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Queue-state transitions authenticated (signer + monotonic sequence) | Golem's demand/offer agreements are signed by both parties; Akash authenticates leases via account certificates. Newer-wins-by-timestamp on a public channel is self-assertion, not consensus. | MEDIUM | `ProcessingSubTaskQueue::UpdateQueue()` must verify the replacement is signed by the queue's authorized authority. Private networks already have stronger controls (Phase 15) — port that discipline to the public path. |
| Results signed by the assigned worker; payout address + developer split fixed at agreement time | Marketplace pattern: payout terms are in the signed agreement, never derived from reported results. | MEDIUM-HIGH | Requires job-assignment state to record the payout contract before execution. Feeds directly into #6/#22. |

### Table Stakes — Cluster I: Enforced Proof Verification on Ingress [VULN] (#21)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Fail-closed: verify, or disable and represent honestly | ZK-rollup pattern: proofs are verified before any economic effect; a system that sets `valid_proof = true` and never calls `VerifyFullProof()` is misrepresenting a security control. | LOW (remove bypass) / MEDIUM (real wiring) | Two acceptable endpoints: (a) delete the bypass and enforce verification where it's affordable — before economic effect (certificate/payout), not necessarily at deserialization; or (b) explicitly disable the proof path and stop representing it as a control. Never a third state that claims verification. |

### Table Stakes — Cluster J: Independent Result Verification Before Payout [CAPABILITY] (#6)

The genuinely hard cluster. How comparable systems do it:

| Approach | Who | Cost | Verdict |
|----------|-----|------|---------|
| Redundant execution + majority vote | BOINC/SETI@home, Golem gWASM | 2–3x compute | Table stakes for general workloads; catches naive fraud, cheap, probabilistic |
| Optimistic + challenge/fraud proofs | Truebit | Verification-game complexity | Strong but heavy; rarely shipped in production networks |
| ZK proofs of execution | zkVM ecosystem (RiscZero/SP1 class) | Prover overhead per task | Not tractable as the default path for arbitrary/AI workloads this milestone |
| TEE remote attestation | Phala, Secret, iExec class | Hardware dependency, enclave engineering | Differentiator; the honest path to #23 as well |
| Reputation | most networks | low | Not a security control; at most a routing input |

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Production path never pays on unverified self-reported results | Minimum bar in every compute marketplace: something independent of the claimant gates settlement (Akash: lease/time-based escrow against an accepted deployment; Golem: verification scheme + invoice dispute flow). | HIGH | The production task-splitting call currently passes `false` for validation redundancy (`GeniusNode.cpp`). Table-stakes floor: turn redundancy on / re-execute deterministic checks before payout release. |
| Deterministic execution environment | Precondition for any re-execution check (gWASM's determinism is why WASM was chosen). | MEDIUM | Even a "verify by re-running a sample/portion" strategy needs determinism. |
| Tiered verification by job value | Differentiated pattern: small jobs optimistic, medium redundant, large ZK/TEE. | HIGH | The GCS Execution Integrity System is the architectural answer but unimplemented — this milestone should land the redundant/deterministic tier, not the full system. |

### Table Stakes — Cluster K: Escrow-Sufficiency Validation [CAPABILITY] (#22)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Independent workload validation + escrow ≥ max-cost check before worker acceptance | Akash's deposit/escrow-backed leases: the lease doesn't start unless the deposit covers the max price; acceptance against self-reported dimensions or an unaudited external quote is the anti-pattern. | MEDIUM | Check at job acceptance: independently validated dimensions × price ≤ escrowed amount, else reject. External price feeds (CoinGecko) may inform pricing but must not authorize settlement — consensus-approved billing per job. |

### Table Stakes — Cluster L: SDK Memory Safety + Worker Profile [VULN/#24, HARDEN/#18]

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Bounded copies + length checks in C SDK boundaries (#24) | Universal C-API rule (strlcpy-style discipline); `MakeGeniusAddress()` copying arbitrary-length input into a fixed array is a textbook defect. | LOW | Fix + documented ownership convention (caller-frees vs SDK-owned) + real test suite (currently only a CMakeLists in `test/`). Run under ASan in CI. |
| Minimal worker profile (#18) | Embedding SDKs ship size/resource profiles (Unity/native SDK norms; UnityGeniusSDK already exists — the report's "no Unity plugin" claim is wrong). | MEDIUM | Build surgery: compile/link-time exclusion of full-node components; a worker that doesn't validate or store history. Measured CPU/GPU duty-cycle controls (MEDIUM, hardware-in-loop measurement). |

### Table Stakes — Cluster M: Release Gating + Dependency Integrity [HARDEN] (#19, #20)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Release artifacts published only after required tests pass (#20) | Baseline CI/CD hygiene: release job `needs:` test jobs; GitHub protected environments for publish. Confirmed broken today (release tag/upload precedes the Run tests step). | LOW | Workflow ordering fix. Land first — everything after ships under the gate. |
| Dependencies pinned by content hash, verified in CI (#19) | SLSA practice: pin actions/artifacts by SHA-256 digest, not mutable branch/tag; verify downloaded third-party/zkLLVM artifacts against expected hashes in `cmake.yml`. | LOW-MEDIUM | `FetchContent`/download steps take `URL_HASH`; release artifacts get recorded digests. |
| Build the pinned superproject combination in CI | The stronger half of #19. | MEDIUM | Scheduled job that builds pinned submodule revisions from source (not just consuming release artifacts) — catches skew the artifact path hides. |

### Table Stakes — Cluster N: NAT Relay/Hole-Punch [CAPABILITY] (#17)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Circuit Relay v2 complete in the fork (relay-hop handler finished, limits configured) | libp2p's two-stage NAT story: relay for fallback transit, DCUtR for direct upgrade. Relay v2 with reservations/duration/byte limits is the standard mobile story; the fork currently disables relay + both hole-punch roles and its relay-hop handler is incomplete. | MEDIUM-HIGH | Fork protocol work. Well-provisioned full nodes act as relays; phones connect through them. |
| DCUtR hole punching (client+server roles enabled) | Standard upgrade path — measured ~60% direct-connection success for rust-libp2p DCUtR; symmetric NATs still need the relay. | MEDIUM | Enable + test; do not make correctness depend on it (see Anti-Features). |
| OR: explicit topology specification | The honest alternative the response doc names: "phones behind carrier NAT connect via reachable full-node relays; direct mesh is best-effort." | LOW | Even with relay+DCUtR done, publish the topology contract — a 60% hole-punch rate means relays remain the backbone either way. |

### Table Stakes — Cluster O: Job Privacy & Execution Isolation [CAPABILITY] (#23)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Defined execution boundary (named sandbox: WASM/container/microVM/OS process) with documented threat model | Compute platforms name their isolation boundary (Akash: containers/VMs; iExec/Secret/Phala: TEEs). "Executes wherever the worker process runs" is not a boundary. | MEDIUM (define+enforce) / HIGH (strong isolation) | The milestone requirement is "scoped privacy + defined execution boundary" — honest scoping: access-scoped inputs (scoped credentials, encrypted transport, permissioned networks — partially exists via Phase 15) plus a named, enforced boundary. |
| Confidentiality against the executor only via TEE/FHE | Physics of the problem: software-only access-scoping cannot hide inputs from the party executing them. | HIGH | Differentiator tier (TEE attestation bound to payout). Do not claim confidentiality the design doesn't provide (see Anti-Features). |

### Table Stakes — Cluster P: Mobile Resource Budgets [HARDEN] (#25)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Adaptive budgets (heartbeat/connection watermarks by power/network state), metered-network rules | Mobile P2P apps (Status.im and peers) treat battery/metered-data as first-class config; static full-node watermarks on phones fail in the field. | MEDIUM | `network_config.json` already holds watermarks/reconnect — make them adaptive to platform state (Android `ConnectivityManager`, iOS `NWPathMonitor`). |
| Lifecycle recovery demonstrated (OS kill → relaunch → rejoin, state intact) | Platform requirement for background compute (Android foreground-service/WorkManager norms; iOS background-processing constraints). | MEDIUM-HIGH | iOS is the genuinely constrained platform (background execution is time-sliced); Android background-worker controls exist but recovery needs a demonstrated test. |

### Table Stakes — Cluster Q: On-Chain EVM Privileged-Role Audit [HARDEN/operational] (#12)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Verify deployed roles on-chain (owner/minter/upgrader: EOA vs multisig), document risk and one-step-ownership-transfer exposure | Standard post-assessment operational check; source-level privilege risk is confirmed, deployed control is unknown until read from chain. | LOW | Read the deployed proxies/ACLs on mainnet, document who holds MINTER_ROLE/owner, flag EOA control and the owner limiter exception. Remediation (multisig/timelock) is contracts-repo work — out of scope here per PROJECT.md; record as operational finding. |

### Table Stakes — Cluster R: SGNS→EVM Bridge Return-Leg Activation [CAPABILITY] (#16)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Attestation-based return leg: quorum certificate of burn on SuperGenius → verified on EVM → mint | `GNUSBridgeAttestor.sol` already implements certificate-based bridgeIn with genesis attestors, thresholds, root rotation — the Wormhole/tBTC-shaped design exists; activation awaits the SuperGenius exporter. | MEDIUM-HIGH | Cross-system: exporter feeds attestors from canonical certificates (v3.0 machinery). Table-stakes activation checklist: attestor-set rotation drill, replay protection (nonce/certificate-slot binding), rate/size limits, monitoring, and verification of actually-deployed selectors on the claimed mainnet addresses. **Hard ordering constraint: must not activate before #1 closes** — a forgeable registry would forge burn certificates and mint on EVM. |

---

### Differentiators (Competitive Advantage)

Aligned with PROJECT.md Core Value (one burn ⇒ at most one certificate + one mint) and existing validated machinery.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Adversarial regression suite with named attacker-authority metadata | Exceeds most chains' practice (few teams make "attacker authority required / expected rejection / actual behavior" a machine-checked format); turns every fix into a permanent public-exposure gate | MEDIUM | Reuse Phase 12's real-socket four-peer apparatus as the "malicious peer" harness. |
| Registry authorization via quorum-signed update chain from genesis | Tendermint binds valset hashes into headers; SuperGenius can bind registry CIDs into quorum-signed registry-update records — same property without header changes, reusing TrustedPeerRegistry's validated ceremony | MEDIUM-HIGH | The historical-CID-filtering bypass must close for this to hold (the real fix, not a patch on `LoadRegistryByCid` alone). |
| Content-corroborated multi-endpoint trust with per-operator policy | Most bridges use single oracle/guardian sets; content-hash-quorum across independent RPC providers is stronger than common practice and directly monetizes #11 | MEDIUM | Slot hashes of agreeing block/event content, not URL success counts. |
| Tiered result verification (deterministic re-execution sample → redundancy → TEE/ZK later) | Honest incremental path to GCS Execution Integrity; few compute networks verify beyond reputation today | HIGH | Phase-able: land deterministic + redundant tiers this milestone; TEE/ZK later. |
| Adversarial-quality discipline on a C++ stack (libFuzzer + ASan/CI + black-box malicious-peer E2E) | C++ node software with oss-fuzz-grade hygiene is rare; catches the `.value()`/unbounded-copy class systematically | MEDIUM | Cluster A + E pairing. |
| SLSA-style provenance for release artifacts | Beyond hash pinning; `actions/attest-build-provenance`-class attestation + SBOM | MEDIUM | Only after the table-stakes gating (#20) works. |

### Anti-Features (Commonly Requested, Often Problematic)

Each of these is the *current defective pattern* or a tempting shortcut — flagged with warnings because they must not survive into the roadmap's REQ language.

| Feature | Why Requested | Why Problematic | Alternative |
|----------|---------------|-----------------|-------------|
| **Trust-the-endpoint corroboration** (count successful RPC responses, hash endpoint URLs as "evidence") | Cheap, looks like redundancy | Operator-colluding or misconfigured endpoints produce agreeing successes with divergent content; weighted success counts are spoofable by one party controlling URLs | Quorum agreement on identical content (block hash at height, receipt fields, decoded event) + chain-ID checks + per-endpoint operator policy |
| **Security-by-flag / fail-open defaults** (`valid_proof = true`, empty-chain-ID success, parameter-less-type `return true`, signature enforcement only when a filter happens to exist) | Avoids breaking old flows during migration | Any downgrade path is an attack path; the assessment's confirmed findings are exactly these | Fail-closed at every trust boundary; explicit type-authorized allowlists; no config that silently disables verification on public networks |
| **Verification theater** (signing messages on send without receive-path enforcement) | Signing shows intent and passes review | Only receive-path verification is a control; send-side signing alone is the spec's deprecated Lax mode | Strict verify-then-forward + identity binding (mind the 2026 StrictSign key/`from`-binding CVE) |
| **Building full ZK/GCS Execution Integrity before the redundant tier** | Assessment pressure to "solve verification" | Months of prover engineering while the production path pays on self-reported results | Tiered: deterministic sample + redundancy now; ZK/TEE later where job value justifies |
| **Promising unconstrained phone-to-phone mesh** | Product story ("any two phones form the network") | DCUtR hole punching succeeds ~60% in the field; symmetric NATs never punch; battery/radio cost of direct mesh is prohibitive | Relay-through-full-nodes as the supported topology; DCUtR as best-effort upgrade; publish the topology contract |
| **Claiming job confidentiality without TEE** | Sales answer to "are my models private?" | Software access-scoping cannot hide inputs from the executor; a false claim becomes a liability finding | Document the actual boundary: scoped access + permissioned networks + (later) TEE attestation |
| **Always rebuild all dependencies from source in every CI run** | Maximal supply-chain rigor (the report's stronger demand) | Destroys CI latency/cost; nobody runs it per-PR | Pin by SHA-256 + verify hashes per run; scheduled job builds the pinned superproject combination |
| **Leaving legacy paths enabled "for compatibility"** | Fear of breaking old clients/transactions | An unvalidated legacy type is an authorization bypass (#4) | Explicit deprecation cutoff; disabled reception or equal authority enforcement |
| **Deep vertical refactors bundled with security fixes** | "While we're in here, clean up `is_full_node_` / god-class structure" | Mixing behavior-preserving refactor with security change makes the adversarial regression unattributable; PROJECT.md change discipline forbids it | Refactor first, separately, tests green — then the security change its own diff |

---

## Feature Dependencies

```
[Adversarial regression suite (A)]  ──verifies──>  every cluster (built incrementally, one adversarial test per fix)

[Release gating M#20]  ──gates──>  all subsequent shipped artifacts (land first)

[Wire/gossip/DHT foundations E,F,G]
    E (#8,#9) ──safe parse boundary for──> A's fuzz targets, all network input
    F (#7 receive-path auth) ──authentic transport for──> H (#13 queue/result auth)
                                            │
                                            v
[H: queue/payout authority #13] ──authenticated results──> [J: result verification #6]
[K: escrow sufficiency #22] ──funded/validated work──────> [J: result verification #6]
[C#2: escrow entitlement]    ──settlement authority──────> [J: payout release]
[I: proof enforcement #21]   ──enhances (a tier)────────> [J: result verification #6]
                                                                    │
[J verified result] ──certified task result required by──> [C#2 escrow spend final state]

[B: registry auth #1] ──trust anchor for certificates──> [R: bridge return leg #16]
[D: finality+content corroboration #10,#11] ──verified burn facts──> [C#5 receipt→mint binding]
[v3.0 canonical certificates (validated)] ──exporter input──> [R: bridge return leg #16]

[L#24 SDK memory safety] ──safe base──> [L#18 worker profile] ──runs──> [P: mobile budgets #25]
[N: relay/hole-punch #17] ──connectivity precondition──> [P: mobile readiness #25]

[B #14 audit], [Q #12 on-chain role audit], [M#19 dependency hashes] ── independent, parallel
```

### Dependency Notes

- **J (#6) is the dependency sink:** result verification consumes #13 (authenticated claims), #22 (sufficient escrow), #2 (entitled settlement), and #21 (proof tier). Ship those first or J verifies forgeries faithfully.
- **R (#16) hard-depends on B (#1):** activating the EVM return leg with a forgeable validator registry converts a native trust bug into an EVM mint bug — strictly worse. Also depends on the exporter (certificate pipeline exists from v3.0; exporter exposure is the new work) and on D (#10/#11) for burn-fact quality.
- **C#5 binding depends on D:** exact receipt-fact binding is only meaningful once the receipt itself is finality-gated and content-corroborated.
- **F underpins H:** signed-queue discipline is void if the transport accepts spoofed gossip sources.
- **P (#25) depends on N (#17) for connectivity and L#18 for the runnable minimal worker** — mobile readiness is last in the chain.
- **A (suite) and M#20 (gating) precede everything operationally:** per PROJECT.md, the suite is the verification spine and the release gate is the shipping control.

---

## MVP Definition

For a security milestone, "MVP" = the public-exposure safety boundary. The response doc's own order: vulnerabilities → hardening → capabilities.

### Launch With (v1.3 core = safety boundary)

- [ ] Adversarial regression suite apparatus + metadata convention (Cluster A) — the proof mechanism for everything else
- [ ] Registry-CID authorization chain (#1) — highest-credibility critical finding
- [ ] Escrow entitlement (#2), fail-closed mint paths (#3), legacy-mint disable (#4), receipt-fact binding (#5)
- [ ] Confirmation-depth/finality + content corroboration (#10, #11)
- [ ] Length caps + safe deserialization (#8, #9) — "simple, high-value fix" per the response doc
- [ ] Gossip receive-path verification (#7)
- [ ] Proof-bypass removal, fail-closed (#21)
- [ ] Queue/payout authentication (#13)
- [ ] SDK memory safety + tests (#24)
- [ ] Release-before-tests ordering fix (#20) — trivial, urgent, lands first

### Add After Validation (hardening)

- [ ] DHT validators/quotas + request-bound responses (#15)
- [ ] Dependency hash verification + pinned-combination build (#19)
- [ ] Result verification production tier: redundancy/deterministic re-execution (#6) — starts after its dependency cluster (#2, #13, #22) merges
- [ ] Escrow sufficiency (#22)
- [ ] Worker profile (#18)
- [ ] On-chain EVM role audit (#12); admission/equivocation audit (#14)
- [ ] Mobile budgets/lifecycle (#25); relay/hole-punch or explicit topology (#17)

### Future Consideration (post-v1.3 unless dependency-complete)

- [ ] TEE/ZK verification tiers for #6 and #23 — separate change stream (GCS Execution Integrity)
- [ ] Contracts-repo remediation (multisig/timelock for #12 findings)
- [ ] Full phone-to-phone mesh ambitions beyond relay backbone + best-effort DCUtR

Note: PROJECT.md commits **all 25 findings to v1.3**; the Launch/After/Future split above is *sequencing within the milestone*, consistent with "only after these safety boundaries are verified" — not deferral.

---

## Feature Prioritization Matrix

| Cluster (findings) | Category | User/Network Value | Implementation Cost | Priority |
|---|---|---|---|---|
| A. Adversarial suite | spine | HIGH | MEDIUM | P1 |
| B. Registry auth (#1, #14) | VULN | HIGH | MEDIUM-HIGH | P1 |
| C. Mint/escrow binding (#2–#5) | VULN | HIGH | MEDIUM | P1 |
| D. Bridge finality + corroboration (#10, #11) | VULN | HIGH | MEDIUM | P1 |
| E. Wire/allocation caps (#8, #9) | VULN | HIGH | LOW-MEDIUM | P1 |
| F. Gossip receive verification (#7) | VULN | HIGH | LOW-MEDIUM | P1 |
| I. Proof-bypass removal (#21) | VULN | HIGH | LOW | P1 |
| M#20. Release ordering gate | HARDEN | HIGH | LOW | P1 |
| H. Queue/payout auth (#13) | VULN | HIGH | MEDIUM-HIGH | P1 (feeds J) |
| L#24. SDK memory safety | VULN | MEDIUM | LOW | P1 |
| G. DHT admission (#15) | HARDEN | MEDIUM-HIGH | MEDIUM | P2 |
| M#19. Dependency hashes + pinned build | HARDEN | MEDIUM | LOW-MEDIUM | P2 |
| K. Escrow sufficiency (#22) | CAPABILITY | HIGH | MEDIUM | P2 (J prerequisite) |
| J. Result verification tier (#6) | CAPABILITY | HIGH | HIGH | P2 (after #2/#13/#22) |
| Q. EVM role audit (#12) | HARDEN | MEDIUM | LOW | P2 |
| B#14. Admission/equivocation audit | VULN-adjacent | MEDIUM | MEDIUM | P2 |
| L#18. Worker profile | HARDEN | MEDIUM | MEDIUM | P2–P3 |
| N. Relay/hole-punch (#17) | CAPABILITY | MEDIUM-HIGH | MEDIUM-HIGH | P2–P3 |
| R. Bridge return leg (#16) | CAPABILITY | HIGH | MEDIUM-HIGH | P3 (strictly after #1; exporter dependency) |
| O. Privacy/isolation (#23) | CAPABILITY | MEDIUM | HIGH | P3 (define boundary now; TEE later) |
| P. Mobile budgets (#25) | HARDEN | MEDIUM | MEDIUM-HIGH | P3 |

---

## Competitor / Comparable-System Analysis

| Capability | L1s (Bitcoin/Ethereum) | Cosmos/Tendermint | Bridges (Wormhole/tBTC/CCIP) | Compute nets (Golem/Akash/BOINC/Truebit) | libp2p ecosystem | Our approach |
|---|---|---|---|---|---|---|
| Validator-set authority | Not applicable (protocol-fixed/staking) | `NextValidatorSetHash` signed in header; light client verifies valset transitions safely | Guardian/signer sets with rotation ceremonies | n/a | n/a | Quorum-signed registry update chain from genesis, verified at CID load (reuse TrustedPeerRegistry ceremony) |
| Adversarial testing | Functional adversarial suites, oss-fuzz | Formal light-client proofs | Audits + bug bounties | n/a | Protocol conformance tests | Named-attacker-authority regression suite on real-socket multi-node apparatus |
| Bridge finality | Finalized-checkpoint semantics (2-epoch FFG) | Instant finality | Wait-for-finality + risk layers (CCIP); skipping it is the historic exploit class | n/a | n/a | Per-source finality/depth policy before irreversible mint |
| Source-data corroboration | n/a | n/a | Oracle quorum on identical data | n/a | n/a | Content-hash quorum across independent RPC endpoints |
| Wire DoS resistance | 4 MiB message cap before read | n/a | n/a | n/a | Frame/varint caps per implementation | Cap-before-allocate at all fork parse boundaries + depth limits + fuzzing |
| Gossip auth | n/a | n/a | n/a | n/a | Strict signing policy default (verify-then-forward); 2026 key-binding CVE as cautionary nuance | Enforce signed envelope on public path; bind signer to identity |
| DHT validation | n/a | n/a | n/a | n/a | Spec: record validators at retrieval+storage; 2026 PUT_VALUE CVE | Real validators + quotas; request-bound responses |
| Result verification | n/a | n/a | n/a | Redundancy (BOINC/gWASM), escrow+disputes (Golem), optimistic challenges (Truebit), TEE (iExec-class) | n/a | Tiered: deterministic sample/redundancy now; ZK/TEE later |
| Escrow economics | n/a | n/a | n/a | Deposit-covered leases before start (Akash) | n/a | Sufficiency check at worker acceptance |
| Supply chain | Reproducible builds | n/a | n/a | n/a | n/a | SHA-256 pinning + gated releases + (later) provenance |
| NAT traversal | n/a | n/a | n/a | n/a | Relay v2 + DCUtR (~60% punch success) | Finish relay v2 + DCUtR; publish relay-backbone topology contract |
| Mobile | Light clients, not full nodes | n/a | n/a | n/a | Relay-dependent mobile patterns | Worker-profile node + adaptive budgets + lifecycle proof |

---

## Genuinely Hard Things (honest scoping for the roadmap)

1. **Independent result verification for arbitrary/AI workloads (#6).** No deployed network verifies nondeterministic compute cheaply. Redundancy is 2–3x cost and probabilistic; ZK prover overhead per task is prohibitive as a default; TEE is an engineering program. Scope this milestone to the deterministic/redundant tier and say so.
2. **Registry authorization without breaking liveness (#1).** The historical-CID retrieval bypassing element filtering is load-bearing for catch-up; closing it without stranding sync is the design work. The TrustedPeerRegistry pattern shows the shape; the consensus-certificate path raises the stakes.
3. **Confidentiality against the executor (#23).** Unsolved in software without TEE/FHE. Define the honest boundary; don't promise confidentiality.
4. **iOS background execution (#25).** Platform time-slices background work; full participation from iOS is constrained by OS policy, not by SuperGenius code. Demonstrate recovery; budget expectations accordingly.
5. **Phone-to-phone mesh (#17).** ~60% hole-punch success ceiling; relay backbone is the architecture. Finishing the fork's relay-hop handler is real protocol engineering.
6. **Cross-repo sequencing discipline.** Fixes #8/#15/#17 live in the libp2p fork, #24 in GeniusSDK — three release trains to synchronize under the new release gate.

---

## Sources

- [libp2p pubsub spec (Strict/Lax signing policies)](https://github.com/libp2p/specs/blob/master/pubsub/README.md) — HIGH (official spec)
- [go-libp2p-pubsub strict signature verification docs](https://pkg.go.dev/github.com/libp2p/go-libp2p-pubsub) — HIGH
- 2026 gossipsub StrictSign key/`from`-binding CVE (CVE-2026-86038; npm advisory) — MEDIUM-HIGH (advisory record; search-mediated)
- [libp2p Kademlia spec (record validation at retrieval + storage)](https://github.com/libp2p/specs/blob/master/kad-dht/README.md) — HIGH
- [go-libp2p-kad-dht Validator interface](https://pkg.go.dev) — HIGH; 2026 `@libp2p/kad-dht` PUT_VALUE validation CVE — MEDIUM-HIGH
- [Tendermint light client docs + paper (NextValidatorSetHash signed in header)](https://docs.tendermint.com/v0.34/tendermint-core/light-client.html) — HIGH
- [ethereum/hive (end-to-end adversarial client test harness)](https://github.com/ethereum/hive) — HIGH; [Antithesis Ethereum Merge testing](https://antithesis.com/blog/ethereum_merge) — MEDIUM
- [Ethereum.org PoS finality](https://ethereum.org/developers/docs/consensus-mechanisms/pos); Quantstamp Merge analysis (2-epoch FFG finality); Vitalik "Epochs and slots all the way down" — HIGH
- [Jump Crypto: Bridging and Finality](https://jumpcrypto.com/resources/bridging-and-finality-an-introduction); [Dwellir bridge finality overview](https://www.dwellir.com/blog/cross-chain-bridges-explained) — MEDIUM
- [SLSA build provenance spec](https://slsa.dev/spec/v1.2/build-provenance); SHA-256 pinning practice ([encryptionconsulting.com](https://www.encryptionconsulting.com/slsa-build-provenance-and-signing-in-ci-cd-pipelines), [KEDA http-add-on #1604](https://github.com/kedacorp/http-add-on/issues/1604)) — HIGH
- Protobuf DoS record: CVE-2024-7254 (nested groups), CVE-2026-6409 (PHP varints), Trail of Bits "Don't recurse on untrusted input" — HIGH
- [Golem gWASM verification](https://medium.com/golem-project/verification-in-gwasm-7b9fb68b2fd7); [Golem payments/escrow docs](https://docs.golem.network/docs/golem/payments) — MEDIUM
- DCUtR/hole-punch success (~60%, rust-libp2p forum; [zig-libp2p issue #91](https://github.com/blockblaz/zig-libp2p/issues/91); IPFS hole-punching blog; arXiv longitudinal study Oct 2025) — MEDIUM-HIGH
- Project evidence base: `.planning/notes/security-assessment-2026-10-response.md`, `.planning/PROJECT.md` — HIGH (primary)

---
*Feature research for: SuperGenius v1.3 Security Remediation & Network Readiness*
*Researched: 2026-10-19*
