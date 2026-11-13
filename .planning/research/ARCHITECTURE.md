# Architecture Research

**Domain:** Security remediation integration for an existing C++17 blockchain/crypto platform (v1.3 "Security Remediation & Network Readiness")
**Researched:** 2026-10-19
**Confidence:** HIGH (every vulnerability site below was located and read in the current `develop` tree or the sibling fork/SDK checkouts; capability items #16/#17/#18/#23/#25 are MEDIUM where they depend on unbuilt components)

## Standard Architecture

### System Overview — where the 25 findings land

The remediation does not introduce a new architecture; it closes authorization gaps at five existing boundaries and adds two new components (bridge exporter, adversarial test apparatus). The diagram maps each finding cluster onto the existing layers:

```
┌───────────────────────────────────────────────────────────────────────────────┐
│  EXTERNAL / DELIVERY (modified)                                               │
│  .github/workflows/cmake.yml  ......... #20 release-before-tests ordering      │
│  thirdparty/zkLLVM artifact download ... #19 no content-hash verification      │
├───────────────────────────────────────────────────────────────────────────────┤
│  TRANSPORT / P2P (fork repos — modified)                                      │
│  thirdparty/libp2p  gossip_core.cpp ......... #7  sign-only, no verify-on-recv │
│  thirdparty/libp2p  kademlia message.cpp / uvarint / BE readers ... #8 caps    │
│  thirdparty/libp2p  validator_default.cpp .... #15 accepts any stored value    │
│  thirdparty/libp2p  relay/*.cpp .............. #17 relay-hop incomplete        │
│  thirdparty/ipfs-pubsub gossip_pubsub.cpp:317 #17 relay/holepunch disabled     │
├───────────────────────────────────────────────────────────────────────────────┤
│  DOMAIN: Consensus & Registry (SuperGenius — modified)                        │
│  ValidatorRegistry::LoadRegistryByCid ....... #1  no authorization on CID read │
│  ConsensusManager::ValidateCertificate ...... #1  tallies against that registry│
│  ValidatorRegistry admission / equivocation . #14 policy audit                │
├───────────────────────────────────────────────────────────────────────────────┤
│  DOMAIN: Transactions / Mint / Escrow (SuperGenius — modified)                │
│  PublicChainInputValidator::GatherVerificationEvidence                        │
│    ............... #3  fail-open (empty chain-id/source-ref)                  │
│    ............... #5  receipt content not bound to mint outputs              │
│    ............... #10 no confirmation-depth / finality requirement           │
│    ............... #11 slots hash endpoint URLs, not agreeing content         │
│  GeniusInputValidator::ValidateWitness ....... #2  delegated_escrow_spend gap │
│  TransactionManager HoldEscrow/PayEscrow ..... #22 escrow sufficiency rule    │
│  TransactionManager::FilterProof ............. #21 valid_proof=true bypass    │
│  TransactionConsensusHandler .................. #4  legacy-v1 mint admission  │
│  TransferTransaction / EscrowTransaction ...... #9  .value() on hash parse    │
├───────────────────────────────────────────────────────────────────────────────┤
│  DOMAIN: Processing Grid (SuperGenius — modified + one new seam)              │
│  ProcessingSubTaskQueue::UpdateQueue ......... #13 newer-timestamp wins, no auth│
│  processing_service payout metadata .......... #13 self-asserted addresses    │
│  ProcessingValidationCore ..................... #6  redundancy=false on path   │
│  GeniusNode.cpp:3193 SplitTask(..., false ...)#6  validation disabled at split │
│  (new) execution-boundary / privacy scoping .. #23                             │
├───────────────────────────────────────────────────────────────────────────────┤
│  PERSISTENCE (SuperGenius — contract clarification only)                      │
│  CrdtDatastore::GetLocalDeltaKeyValues ....... #1  raw-DAG read feeding #1    │
├───────────────────────────────────────────────────────────────────────────────┤
│  NEW COMPONENTS                                                               │
│  (new) SGNS→EVM certificate exporter ......... #16 return-leg activation      │
│  (new) adversarial regression suite .......... spine for all 25               │
│  (new) GeniusSDK test suite / worker profile . #18 #24                        │
├───────────────────────────────────────────────────────────────────────────────┤
│  EXTERNAL CHAINS (read-only this milestone)                                   │
│  GNUSBridge.sol / GeniusOwnershipFacet.sol ... #12 on-chain role audit only   │
│  GNUSBridgeAttestor.sol ...................... #16 deployment target          │
└───────────────────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities (remediation-relevant only)

| Component | Responsibility for this milestone | File | New/Modified |
|-----------|-----------------------------------|------|--------------|
| `ValidatorRegistry` | Owns registry authorization: a registry snapshot loaded by CID must be verified against its certified update chain before use | `src/blockchain/ValidatorRegistry.cpp` (LoadRegistryByCid at :785) | Modified |
| `ConsensusManager` | Consumes verified registries only; caches verified snapshots; equivocation detection audit | `src/blockchain/Consensus.cpp` (:3513, :2589, :1474) | Modified |
| `PublicChainInputValidator` | Owns public-chain evidence rules: fail-closed on unknown chain ids, receipt-content binding, confirmation depth, content-based corroboration | `src/account/PublicChainInputValidator.cpp` (GatherVerificationEvidence :307–490) | Modified |
| `GeniusInputValidator` | Owns input-witness entitlement: the escrow-lock spend exception requires a certified task result | `src/account/GeniusInputValidator.cpp` (:443–447) | Modified |
| `TransactionManager` | Owns escrow financial semantics (HoldEscrow/PayEscrow sufficiency), CRDT element filters (FilterProof enforcement), mint ingress | `src/transaction/TransactionManager.cpp` (note: `src/transaction/`, not `src/account/`) | Modified |
| `TransactionConsensusHandler` | Owns transaction-type admission rules (legacy-v1 mint disable/authorize) | `src/transaction/TransactionConsensusHandler.cpp` (:545, :801) | Modified |
| `ProcessingSubTaskQueue` + channels | Owns queue-update authenticity (signed/ownership-gated updates over pubsub channel) | `src/processing/processing_subtask_queue.cpp` (:24), `processing_subtask_queue_channel_pubsub.*` | Modified |
| `ProcessingValidationCore` / `ProcessingServiceImpl` | Owns independent result verification and payout-metadata binding | `src/processing/processing_validation_core.*`, `processing_service.*` | Modified |
| `gossip_auth.hpp` envelope + gates | Existing app-layer signed envelope (private networks) — extend to public-network authentication | `src/base/gossip_auth.hpp` | Modified (extension) |
| libp2p gossip receive path | Wire-level signature verification before forward (strict mode) | `thirdparty/libp2p/src/protocol/gossip/impl/gossip_core.cpp` | Modified (fork) |
| libp2p readers | Length-prefix caps before allocation (uvarint, big-endian, Kademlia message reader) | `thirdparty/libp2p/src/multi/uvarint.cpp`, `src/protocol/kademlia/message.cpp:181` | Modified (fork) |
| libp2p Kademlia validator | Real default value admission (content-key or signed-record check) + storage/request limits | `thirdparty/libp2p/src/protocol/kademlia/impl/validator_default.cpp` | Modified (fork) |
| ipfs-pubsub host config | Relay/hole-punch enablement behind config | `thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:317–319` | Modified (fork) |
| libp2p relay | Complete relay-hop handler for NAT traversal | `thirdparty/libp2p/src/protocol/relay/relay.cpp` | Modified (fork) |
| `AccountMessenger` | Request-bound nonce-response matching | `src/account/AccountMessenger.cpp` | Modified |
| Bridge certificate exporter | NEW: reads canonical certificates, produces attestations, submits EVM transactions to GNUSBridgeAttestor | new module (suggested `src/watcher/` sibling, e.g. `src/bridge_exporter/`) | **New** |
| Adversarial regression suite | NEW: per-finding tests naming attacker authority / expected rejection / actual behavior | `test/src/…` + fork `test/` trees | **New** |
| GeniusSDK C surface | Length-checked copies, ownership docs, real test suite, minimal worker profile | `~/gnus/GeniusSDK/src/GeniusSDK.cpp` (:204, :798) | Modified (other repo) |

## Repo Topology and Delivery Pipeline (critical cross-repo fact)

The "libp2p fork" is not one repo and not a submodule of SuperGenius. Verified structure:

```
~/gnus/thirdparty/            ← its own git superproject (.gitmodules lists libp2p, ipfs-pubsub, …)
  libp2p/                     ← fork #8 #15 #17(relay) #7(gossip_core)
  ipfs-pubsub/                ← fork #17(disable flags) #7(gossip_pubsub host wiring)
  ipfs-lite-cpp/              ← consumed by SuperGenius (pubsub/bitswap integration)
~/gnus/SuperGenius/           ← main repo (.gitmodules: gRPCForSuperGenius, GeniusKDF,
                                ProofSystem, SGProcessingManager, docs, evmrelay — no libp2p)
~/gnus/GeniusSDK/             ← sibling repo #18 #24
(contracts repo)              ← read-only this milestone #12 #16(deploy target)
```

SuperGenius does **not** build the forks from source in CI: `cmake.yml` step "Download thirdparty release" runs `gh release download "$tag" --repo GeniusVentures/thirdparty` by branch/tag with **no checksum** (`.github/workflows/cmake.yml:318–345`). Consequence for the roadmap:

> Every fork fix (#7, #8, #15, #17) travels a four-stage pipeline: fork commit → `thirdparty` superproject submodule bump + release build → SuperGenius CI download (today unverified) → SuperGenius integration tests. **#19 (content-hash verification) should land in the same wave as, or before, the first fork fix** so the upgraded artifacts are verifiable, and the fork-side work must be scheduled with the thirdparty release lead time in mind.

## Recommended Project Structure (additions)

```
src/
├── account/                  # modified: PublicChainInputValidator (evidence rules),
│                             #          GeniusInputValidator (entitlement)
├── transaction/              # modified: TransactionManager (FilterProof, escrow sufficiency),
│                             #          TransactionConsensusHandler (legacy mint),
│                             #          Transfer/EscrowTransaction (safe hash parse)
├── blockchain/               # modified: ValidatorRegistry (authorized CID load),
│                             #          Consensus (verified-registry consumption)
├── processing/               # modified: queue auth, validation core, payout binding
├── bridge_exporter/          # NEW (#16): certificate → attestation → EVM tx submission.
│   ├── BridgeExporter.hpp    #   Why new: nothing in src/ owns EVM-bound attestation today
│   └── impl/…                #   (grep for attestor/exporter in src/ returns nothing);
│                             #   src/watcher/ owns EVM→SGNS ingress — the reverse
│                             #   direction is a different concern (invariant #1)
└── base/gossip_auth.hpp      # extended: public-network envelope policy (CR-G01 → public)
test/
├── src/adversarial/          # NEW suite root (see Test Seams below) — or per-domain
│                             #   subdirs following the existing mirror-src convention
```

Structure rationale: mirror the existing `src/` ↔ `test/src/` convention (TESTING.md); the only new source directory is the exporter, which is a genuine new domain (outbound bridge attestation) rather than a refactor of `src/watcher/`.

## Architectural Patterns

### Pattern 1: Authorization at the consuming domain boundary, raw reads stay generic

**What:** `CrdtDatastore::GetLocalDeltaKeyValues(cid)` is a generic raw-DAG persistence API. The #1 fix does *not* add filtering there (it cannot know what a registry is); instead `ValidatorRegistry::LoadRegistryByCid` — the domain owner of "what is an authorized registry" — verifies the RegistryUpdate's certification chain (base-registry link + signatures + quorum, machinery already present at `ValidatorRegistry.cpp:1111/1183/1255/1581`) before returning a snapshot.
**When to use:** any fix where untrusted data crosses a persistence/transport API into a domain rule.
**Trade-offs:** the raw API remains a foot-gun for future callers — mitigate with a Doxygen contract ("returns unverified raw DAG data; callers own authorization") rather than logic in the persistence layer.

### Pattern 2: Extend existing authentication seams before inventing new ones

**What:** `src/base/gossip_auth.hpp` already implements a sealed envelope + fail-closed gates, installed today only when a private-network membership filter exists; its header comment documents precisely the #7 gap (vendored receive path performs zero verification). The #7 fix extends this envelope/gating to public topics and/or adds fork-level verify-on-receive (strict signing mode). Same for #13: queue/result messages on public processing channels get sealed with the same envelope.
**When to use:** whenever a finding's mechanism already has a v1.1/v1.3-era authenticated predecessor (multisig `VerifyPayloadSignature`, SecureCRDT, TrustedPeerRegistry, canonical certificate store).
**Trade-offs:** fork-level verification is the correct *wire-rule* owner; the app envelope is defense-in-depth. Doing only the app envelope leaves non-participating peers forwarding unverified gossip.

### Pattern 3: Fail-closed evidence with content-addressed corroboration

**What:** `GatherVerificationEvidence` becomes: no endpoint configuration / unknown chain id / empty source reference ⇒ invalid (reverses :319–334); evidence slots carry digests of *agreeing receipt/log content* rather than `sha256(url)` (:459–466); a confirmation-depth check compares receipt block number against the source-chain head/finality before any weight is earned.
**When to use:** #3, #5, #10, #11 land as one coherent rewrite of this function plus its per-endpoint config (which already carries `bridge_contract_address` and `accepted_topic0_hashes` via `ChainRpcEndpointProvider` — extend, don't replace).
**Trade-offs:** content corroboration needs a canonical receipt digest definition (one home — invariant #2); depth checks add RPC round-trips (use the existing 10 s timeout budget and the watcher's head knowledge).

### Pattern 4: Adversarial regression suite as a first-class deliverable

**What:** each fix ships with a test that names (a) the attacker authority required (e.g. "any gossip peer", "holder of spending key without entitlement"), (b) the expected rejection, (c) the actual behavior. Reuse the existing test infrastructure: `transport_factory_` injection seam in PublicChainInputValidator (D-07/D-14, :351–357) for mock RPC; `test/src/processing_conformance_security/` already exists as a naming precedent; libp2p/ipfs-pubsub forks have their own `test/` trees for reader-cap and relay tests; GeniusSDK's empty `test/` gets its first real suite.
**When to use:** every one of the 25; the suite is the milestone's verification spine (Key Decision).

## Data Flow

### Changed flow 1 — Certificate validation (#1, #14)

```
Before: certificate.registry_cid → LoadRegistryByCid → GetLocalDeltaKeyValues (raw DAG,
        may contain filter-rejected forged RegistryUpdate) → TallyVotes → Approve
After:  certificate.registry_cid → LoadRegistryByCid →
          verify RegistryUpdate chain (base-registry CID → signature/quorum via existing
          multisig VerifyPayloadSignature path) → only then return snapshot → TallyVotes
        ConsensusManager::LoadRegistryByCidCached caches VERIFIED snapshots only
```
No wire-format change. The adversarial test injects a forged update into the DAG (bypassing the element filter), then submits a certificate referencing its CID — expected: Reject/Stalled, never Approve.

### Changed flow 2 — Bridge mint evidence (#3, #5, #10, #11)

```
Receipt RPC → parse → status ✓ → bridge addr/topic0 ✓ →
  NEW: blockNumber ≥ head − finality_depth (per-chain configurable) →
  NEW: decode event {token, amount, destination} → digest(content) into evidence slots →
  quorum = agreeing content digests across distinct endpoints →
  NEW: bind decoded {token, amount, destination} to the mint proposal's outputs
       (consume the Phase-8 canonical bridge-burn identity facts — do not recompute)
```
Fail-open branches at `:319` (empty source ref) and `:329` (empty/local chain id) removed; type-specific authorization replaces the blanket "supergenius" bypass (#3) — an explicit allowlist of local/authorized transaction types.

### Changed flow 3 — Escrow spend entitlement (#2, #22)

```
Before: transfer spends escrow-lock UTXO if uncle_hash == escrow lock address
        (GeniusInputValidator.cpp:443–447) — signature from spending key suffices
After:  delegated_escrow_spend additionally requires a canonical certified task result
        referencing this escrow (read from the certificate store's public contract),
        validated through the #1-hardened path
#22:    HoldEscrow amount checked against measured workload dimensions before worker
        acceptance (escrow domain consumes processing-measured facts; PriceQuote stays
        an advisory quote source, never an authority)
```

### Changed flow 4 — Processing grid authenticity (#6, #13)

```
Queue update / result / payout metadata  →  sealed with gossip_auth envelope on public
channels (same machinery as private networks)  →  UpdateQueue additionally verifies the
 updater is the current queue owner / entitled enqueuer (not just newer timestamp)
Result → independent verification (validation redundancy enabled on the production
 split path — GeniusNode.cpp:3189 flag) → payout addresses/developer cut derived from
 the authenticated job record, not worker-reported fields
```

### Changed flow 5 — Return leg (#16, new)

```
canonical certificate store (v3.0, slot-keyed) → BridgeExporter observes finalized
certificates → builds attestation set → submits EVM tx to GNUSBridgeAttestor
(thresholds/rotation already in contract) → confirmation watcher closes the loop
```
Reads certificates only through `ConsensusManager`'s public contract; EVM submission through `evmrelay`'s transaction-sending facilities (extend if receipt-only today).

### Unchanged flows

API/JSON-RPC transport, UTXO set management, CRDT replication mechanics, consensus voting itself — none of the 25 findings require transport-layer business logic (see invariant map below).

## Hard-Invariant Compliance Map

| Fix | Domain rule owner | Transport involvement | Invariant notes |
|-----|-------------------|----------------------|-----------------|
| #1 | `ValidatorRegistry` (authorization) | none | Rule moves into the registry owner; `ConsensusManager` stays a consumer; `GetLocalDeltaKeyValues` stays generic with a documented contract |
| #2 | Input-validation path (where the exception lives) consuming the certificate domain's verified fact | none | The entitlement *fact* is owned by the certificate/processing domain; the *check* stays in `GeniusInputValidator` — no second opinion-policy copy |
| #3/#4/#5 | `PublicChainInputValidator` (evidence) + `TransactionConsensusHandler` (type admission) + mint build path (fact carriage) | none | Receipt-decode → mint-output binding must have ONE comparison home; the Phase-8 canonical burn identity is the existing home for burn facts — extend it, don't duplicate |
| #6/#13/#21/#22 | Processing grid + `TransactionManager` (escrow) | pubsub channel carries sealed envelopes (authentication only, no rules) | Envelope sealing is wire authentication, not business logic; payout derivation stays in `processing_service` |
| #7 | fork gossip (wire rule) + app envelope (defense in depth) | verification IS the transport's job | Signature verify-on-receive is transport-layer authentication — permitted and required; no application rules in the fork |
| #8/#9 | fork readers; transaction deserialization | parsing boundary | Error-return (`outcome`) replaces `.value()` — matches error-handling contract |
| #10/#11 | `PublicChainInputValidator` (+ `evmrelay` head info source) | evmrelay supplies data only | Finality *policy* (depth per chain) lives with the evidence rule owner, not in the RPC client |
| #15 | fork Kademlia validator (admission) + `AccountMessenger` (request binding) | both are protocol-level | Match nonce responses to outstanding request ids before state update |
| #16 | NEW exporter module | EVM submission via evmrelay | Read certificates via `ConsensusManager` public contract only |
| #17 | fork relay + ipfs-pubsub config | transport capability | SuperGenius-side: config surface + topology doc |
| #18/#24/#25 | GeniusSDK / node config / lifecycle | none | Worker profile = config preset over existing `node_type`/`is_processor` surface |
| #19/#20 | CI workflow | n/a | Pipeline ordering; gate release upload on test success |
| #12 | operational audit (doc deliverable) | n/a | No contracts-repo code changes |

## Integration Points (explicit new vs modified)

### External repos

| Repo | Findings | Integration pattern | Notes / gotchas |
|------|----------|---------------------|------------------|
| `thirdparty/libp2p` (fork) | #7 #8 #15 #17 | fork commit → superproject bump → thirdparty release → SuperGenius downloads artifact | Four-stage lead time; SuperGenius cannot compile against an unbumped fork; pair with #19 so artifacts are hash-verified |
| `thirdparty/ipfs-pubsub` (fork) | #7 #17 | same pipeline | `gossip_pubsub.cpp:317–319` disable flags become config-driven |
| `GeniusSDK` (sibling) | #18 #24 | independent repo, own CI | `test/` currently CMakeLists-only — build the suite from scratch; SDK links SuperGenius node binaries (version coupling with releases) |
| contracts repo | #12 #16 | read-only / on-chain ops | #12 = on-chain role verification + doc; #16 = deploy/activate existing `GNUSBridgeAttestor.sol` (source exists; activation depends on the exporter) |
| EVM public RPC providers | #10 #11 | via existing `evmrelay` RpcManager + `ChainRpcEndpointProvider` | Operator-controlled endpoint policy: extend the existing per-endpoint config (`consensus_weight`, `bridge_contract_address`, `accepted_topic0_hashes`) |

### Internal boundaries

| Boundary | Communication | Considerations |
|----------|---------------|----------------|
| `ConsensusManager` ↔ `ValidatorRegistry` | direct call (`LoadRegistryByCid`) | Authorization moves into the registry owner; the cached loader (:2589) must not admit unverified snapshots — otherwise the fix is bypassed by the cache |
| `TransactionManager` ↔ `PublicChainInputValidator` | membership (`public_chain_input_validator_` member, `GetPublicChainInputValidator()` accessor) + `ChainRpcEndpointProvider` config | Evidence rules change inside the validator; mint-output binding needs the burn facts carried on the mint subject/proposal (Phase-8 identity) |
| `GeniusInputValidator` ↔ certificate store | new read dependency | Entitlement check consults verified certificates; keep it a narrow public-contract read, not a consensus-internal reach-in |
| `ProcessingSubTaskQueue` ↔ pubsub channel | `ProcessingSubTaskQueueChannelPubSub` | Envelope sealing applied at the channel boundary; queue keeps ownership/timestamp rules |
| `CrdtDatastore` ↔ `TransactionManager` filters | `RegisterElementFilter` callback (`FilterProof` wired at `TransactionManager.cpp:234`) | #21 removes the bypass inside the existing filter; if proofs are instead formally disabled, represent it honestly (feature flag + docs) — do not leave dead verification code |
| fork gossip ↔ app subscribers | `local_subscriptions_->forwardMessage` (gossip_core.cpp:360) | Verify-before-forward lands in the fork; app envelope gates remain for payload authentication |
| exporter ↔ certificates / evmrelay | new | Exporter is a *new* consumer of two existing public contracts |

## Dependency Graph and Build Order

```
Wave 1 (independent, immediate)
  #20 CI release gating ──┐
  #19 artifact hashing ───┤     (delivery pipeline fixed first; #19 gates fork delivery)
  #8 fork length caps ────┤     (remote-crash class; no dependencies)
  #9 safe hash parsing + fuzz ─┤  (SuperGenius-side, independent of fork pipeline)
  #12 on-chain role audit ┘     (operational; starts immediately, doc deliverable)
  #24 GeniusSDK memory safety ── (independent repo, parallel)

Wave 2 (trust-boundary closure — the security core)
  #1 registry-CID authorization  ──→ #14 admission/equivocation audit (audit consumes
                                     the hardened path; policy changes follow evidence)
  #3 + #5 + #10 + #11 bridge ingestion rewrite (one function-cluster, one wave)
  #4 legacy-v1 mint admission
  #21 FilterProof bypass removal (decision: enforce or honestly disable)
  adversarial suite scaffolding lands WITH each fix (suite IS the gate)

Wave 3 (authenticated grid + gossip — builds on Waves 1–2)
  #7 gossip verify-on-receive (fork + public envelope policy) ──→ #13 queue/result/payout
     authentication (reuses #7 envelope machinery)              ──→ #6 independent result
     verification (needs authenticated results)                 ──→ #2 escrow entitlement
     (consumes the certified-result record #6/#13 produce)      ──→ #22 escrow sufficiency
  #15 DHT admission (fork) + request-bound AccountMessenger (parallel to #7, same
     fork-release train)

Wave 4 (capabilities gated on trust closure)
  #17 relay/hole-punch or explicit topology spec   (needs #8 + #7 before public exposure)
  #25 mobile budgets + lifecycle ──→ #18 minimal worker profile (measured on mobile)
  #23 job privacy / execution boundary
  #16 return-leg activation — LAST: requires #1 (certificates trustworthy), #3/#5/#10/#11
      (forward-leg finality + binding), plus the new exporter + contract deployment
```

**Ordering rationale (explicit):**
- **#19/#20 first:** every later fix ships through this pipeline; #19 must exist before fork artifacts flow so their provenance is verifiable.
- **#8 before #7/#15/#17:** all fork exposure work rides the same thirdparty release train; parse-safety caps are prerequisite to enabling anything new on public networks.
- **#1 before #14:** the admission/equivocation audit is only meaningful against the hardened registry path; its findings may spawn follow-up policy work.
- **#1 before #2/#6/#13:** escrow entitlement and result verification bottom out in certificates being trustworthy (#1) — a certified-result record signed by a forgeable registry proves nothing.
- **Ingestion hardening (#3/#5/#10/#11) independent of Wave 3:** it touches only the evidence validator and its config; can proceed in parallel with gossip/grid work.
- **Bridge finality (#10) before return-leg activation (#16):** activating an out-bound bridge on top of unfinalized in-bound evidence multiplies, not reduces, exposure — and the exporter consumes the same certificate/finality facts.
- **#2 after (or with) #13/#6:** the entitlement fact needs an authenticated certified-result record; a minimal #2 can land earlier if it consumes the existing consensus-certificate path for task results — validate that record exists during phase research.
- **#25 before #18's measurement half:** duty-cycle controls are measured on mobile budgets; the SDK profile packaging itself is independent.

## Anti-Patterns

### Anti-Pattern 1: Fixing #1 by filtering in CrdtDatastore

**What people do:** add registry-aware filtering inside `GetLocalDeltaKeyValues` or the DAG walk.
**Why it's wrong:** the persistence layer cannot know registry authorization rules; it violates one-owning-module and forces `crdt/` to include blockchain domain knowledge (cycle risk the codebase already fights).
**Do this instead:** verify in `ValidatorRegistry` (the domain owner), document the raw API as untrusted, and make `ConsensusManager`'s cache verified-only.

### Anti-Pattern 2: Patching the mint binding at the RPC client

**What people do:** put the token/amount/destination comparison into `evmrelay`'s receipt parsing.
**Why it's wrong:** business rule in a transport library (invariant #3); the comparison needs the mint proposal's outputs, which the RPC layer never sees.
**Do this instead:** decode facts in the evidence layer, compare in the mint validation path, one home for the burn-fact identity (extend Phase-8 canonical identity).

### Anti-Pattern 3: Adding a parallel "security" signature scheme for the grid

**What people do:** invent a new signing wrapper for queue updates/results because "the gossip layer isn't trusted."
**Why it's wrong:** duplicates domain knowledge (second authentication mechanism beside `gossip_auth.hpp`, multisig, SecureCRDT); invariant #2 violation and a maintenance fork.
**Do this instead:** extend the existing envelope/gates to public channels; add ownership rules in the queue domain.

### Anti-Pattern 4: Fork fixes without the delivery pipeline

**What people do:** commit fork changes and assume SuperGenius picks them up.
**Why it's wrong:** SuperGenius consumes prebuilt thirdparty release artifacts downloaded by tag with no hash check — silent skew between fork HEAD and what CI tests.
**Do this instead:** schedule the superproject bump + release + (new) hash verification as explicit roadmap steps in every fork-touching phase.

### Anti-Pattern 5: Enabling proof verification (#21) without an ingress performance story

**What people do:** delete the `valid_proof = true; break;` bypass and ship.
**Why it's wrong:** every CRDT proof element on the ingress path now runs `VerifyFullProof` — potentially heavy zkSNARK verification per element; a liveness regression becomes a self-DoS.
**Do this instead:** measure first (or stage: enforce + sampled/batched verification with an explicit decision record); if the proof feature is not production-ready, disable it explicitly and represent it honestly — the finding allows exactly that.

## Scaling Considerations

| Concern | Now (pre-public) | Post-remediation public network | Growth path |
|---------|------------------|--------------------------------|-------------|
| Gossip verification (#7) | sign-only, verify-never | verify-on-receive all topics | signature cache per peer/message-id; strict mode required for public |
| Evidence RPC traffic (#10/#11) | 3-endpoint receipt checks | + head/depth queries per claim | cache finalized heads per chain; watcher-supplied heads |
| Registry chain verification (#1) | unverified CID read | verify update chain per CID | verified-snapshot cache keyed by CID (bounded LRU like `evidence_by_claim_`) |
| Queue/result sealing (#13) | plaintext on public channels | envelope per message | batch verification; per-channel keys (private-network pattern already proves it) |
| CI (#19/#20) | release-before-tests, unverified artifacts | gated + hashed | org-wide workflow inventory (assessment's stronger claim) later |

## Test Seams (where the adversarial suite plugs in)

| Finding(s) | Existing seam to extend | New apparatus |
|------------|------------------------|---------------|
| #1 #14 | `test/src/blockchain/consensus_certificate_test.cpp`, `validator_registry_*_test.cpp` | DAG-injection fixture (write filter-rejected update, reference its CID) |
| #3 #5 #10 #11 | `transport_factory_` injection (PublicChainInputValidator.cpp:351), `test/src/watcher/`, `test/src/bridge_e2e/` | mock RPC serving conflicting receipts, shallow blocks, colluding endpoints |
| #2 #22 | input-validator tests; `HoldEscrow`/`PayEscrow` tests | unauthorized-spend fixture: valid key, no entitlement |
| #4 | `TransactionConsensusHandler` type-rules tests | self-signed legacy mint fixture |
| #6 #13 | `test/src/processing/`, `processing_conformance_security/`, `processing_nodes/` | forged queue-update/result/payout-claim senders |
| #9 | transaction deserialization tests | libFuzzer target over Transfer/Escrow deserialization |
| #7 #8 #15 #17 | forks' own `test/` trees (libp2p test layout present) | reader-cap unit tests; relay integration pair |
| #21 | CRDT filter tests; `test/src/proof/` | invalid-proof ingress fixture |
| #24 #18 | GeniusSDK `test/` (currently empty) | first real SDK suite; worker-profile duty-cycle measurement harness |
| #20 #19 | CI itself | release-blocked-on-red drill; tampered-artifact drill |

## Sources

- `.planning/PROJECT.md` — v1.3 scope, constraints, key decisions (read in full)
- `.planning/notes/security-assessment-2026-10-response.md` — the 25 findings with credibility triage (read in full)
- Code evidence (read this session, all paths absolute):
  - `/Users/henriqueklein/gnus/SuperGenius/src/blockchain/ValidatorRegistry.cpp:785–820` (#1 raw CID load), `:1111/:1183/:1255/:1581` (existing update-chain verification machinery to reuse)
  - `/Users/henriqueklein/gnus/SuperGenius/src/blockchain/Consensus.cpp:3513–3545` (ValidateCertificate trust), `:2589` (verified-only cache seam)
  - `/Users/henriqueklein/gnus/SuperGenius/src/crdt/impl/crdt_datastore.cpp:961–983, 2280–2288` (filter bypass context, raw delta API)
  - `/Users/henriqueklein/gnus/SuperGenius/src/account/PublicChainInputValidator.cpp:307–490` (#3 :319–334, #5 :410–452, #10 absence, #11 :459–469)
  - `/Users/henriqueklein/gnus/SuperGenius/src/account/GeniusInputValidator.cpp:443–447` (#2 escrow exception)
  - `/Users/henriqueklein/gnus/SuperGenius/src/transaction/TransactionManager.cpp:3834–3853` (#21 bypass; wired at :234)
  - `/Users/henriqueklein/gnus/SuperGenius/src/transaction/TransactionConsensusHandler.cpp:545, :801` (#4)
  - `/Users/henriqueklein/gnus/SuperGenius/src/account/TransferTransaction.cpp:101`, `EscrowTransaction.cpp:106` (#9)
  - `/Users/henriqueklein/gnus/SuperGenius/src/processing/processing_subtask_queue.cpp:24–35` (#13)
  - `/Users/henriqueklein/gnus/SuperGenius/src/account/GeniusNode.cpp:3189–3194` (#6 `false` redundancy flag), `:1887–1889` (#7 sign_messages, #25 heartbeat 500 ms)
  - `/Users/henriqueklein/gnus/SuperGenius/src/base/gossip_auth.hpp` (#7 app envelope; header documents the fork's zero-verification receive path)
  - `/Users/henriqueklein/gnus/SuperGenius/.github/workflows/cmake.yml:318–345, 539–567` (#19 unverified downloads, #20 tag/upload before CTest)
  - `/Users/henriqueklein/gnus/thirdparty/libp2p/src/protocol/gossip/impl/gossip_core.cpp:203–229, 358–360` (#7)
  - `/Users/henriqueklein/gnus/thirdparty/libp2p/src/protocol/kademlia/message.cpp:181` (#8 resize from prefix), `.../impl/validator_default.cpp:12–15` (#15 accept-all)
  - `/Users/henriqueklein/gnus/thirdparty/ipfs-pubsub/src/ipfs_pubsub/gossip_pubsub.cpp:317–319` (#17 disable flags)
  - `/Users/henriqueklein/gnus/GeniusSDK/src/GeniusSDK.cpp:204, :798` (#24)
  - `/Users/henriqueklein/gnus/thirdparty/.gitmodules` + SuperGenius `.gitmodules` (repo topology: libp2p is a thirdparty-superproject submodule, not SuperGenius's)
  - `.planning/codebase/ARCHITECTURE.md`, `TESTING.md`, `PROJECT.md` constraints (layer map, CTest conventions)

---
*Architecture research for: v1.3 security remediation integration (SuperGenius + libp2p fork + GeniusSDK)*
*Researched: 2026-10-19*
