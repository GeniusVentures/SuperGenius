# Phase 21: Consensus Trust & Mint Authorization - Research

**Researched:** 2026-10-19
**Domain:** Consensus/registry trust anchoring + mint authorization in the existing C++17 SuperGenius tree (findings #1, #3, #4, #5, #14 + folded trusted-peer-genesis todo)
**Confidence:** HIGH — every code claim below was verified against current `develop` source this session with file:line citations. Milestone-research line numbers were re-checked and **several have drifted or were undercounted** (documented below); this file supersedes them.

## Summary

This phase closes five authorization gaps plus one folded todo, all inside existing modules — no new libraries, no new components except small verdict/defer plumbing and tests. The single most important discovery of this research: **the codebase already contains four independent three-verdict models and two defer/retry mechanisms that together implement almost exactly D-03's Reject/Defer(bounded)/Accept semantics.** `ValidatorRegistry::VerifyUpdateClassified` returns `kValid / kMissingDependency / kInvalid` and is already wired fail-closed at the registry CRDT filter (`FilterRegistryUpdate` → Accept/Stall/Reject); `ConsensusManager::Check` has `Approve/Reject/Pending/Stalled`; `IInputValidator::WitnessVerdict` has `kValid/kNotSynced/kInvalid`; the CRDT layer has `ElementFilterResult::Stall()` backed by the durable `CRDTWorkJournal` retry loop. TRUST-01's fix is therefore *not* a new verifier — it is routing the CID-load path through the verification machinery that already guards the broadcast path, with a recursion-safe internal raw loader and a finality-bound grandfather probe (D-01) whose mechanism is directly observable: **finality in this codebase = durable readback of the `/cert/<slot_key>` record** (Consensus.hpp:650, Consensus.cpp:4683), and `SubmitCertificate` already probes exactly that record before deciding (Consensus.cpp:2735-2749).

For MINT-03 (D-02), the risk shape is narrower than the assessment implies and the compat mechanics are cleaner: spendability is already certificate-gated (`GeniusInputValidator::ValidateWitness` requires the producer transaction's certificate — inputs from uncertified transactions are permanently `kNotSynced`, i.e. unspendable), and `MintFunds` already creates only MintV2. The v1 gap is that `CheckTransactionTypeRules` returns `true` for any transaction without UTXO parameters (TransactionConsensusHandler.cpp:801-820, TODO at :545), so a self-signed v1 mint *can still be voted on and certified* — that consensus ingress is the load-bearing gate to close. One trap found: `Migration3_4_0To3_5_0.cpp:369` synthesizes zero-value v1 mints as nonce fillers — a blanket type block breaks old-DB migration. For MINT-04, the comparison targets and the one-home identity already exist (`MintTransactionV2::GetSlotID()` = `mint-v2:{chain}:{token}:{amount}:{dest}:{burn_hash}`), and the receipt-event decode must reuse evmrelay's `BridgeEventClaim`/`abi_decoder` rather than a new decoder. The folded trusted-peer todo is ~80% delivered by v1.1 Phase 13 (TrustStartupController/TrustStateStore/GenesisManifest with `FATAL_TRUST_MISMATCH` restart checks); the residual is pinning the *configured* JSON manifest to a ceremony fingerprint on fresh nodes — a narrow task using existing `GenesisManifest::Fingerprint()`/`DecodeAndVerify`.

**Primary recommendation:** Route every certificate-path registry load through a new verified loader in `ValidatorRegistry` (chain-walk with memoized verified CIDs, `enforce_time_window=false`, `kMissingDependency`→Stalled), grandfather via the durable slot-record probe at the certificate layer, keep the verdict semantics in each owning module's existing enum (no new god-enum), add only the bounded-defer wrapper (deadline + FIFO cap + oldest-first eviction, modeled on `evidence_by_claim_`/`evidence_order_`) with the window in `network_config.json`, gate v1 mint at consensus ingress + local submission preflight (certificate-conditional Stall at the sync filter), and bind MINT-04 facts by decoding the receipt log with evmrelay's abi_decoder and comparing against the MintV2's own fields at exactly one home.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** **Finality-bound grandfather rule** for TRUST-01. A certificate that already reached durable finality is never re-checked against the new registry-authorization rule — finality itself is the proof of acceptance. Only in-flight/new certificates take the strict verified-registry path. No persisted markers, no migration pass; restart/sync/merge of old history behaves exactly as today. Accepted residual: a pre-upgrade forged cert that somehow finalized stays final (it is final under today's rules too).
- **D-02:** **Hard-disable legacy-v1 mint at admission** for MINT-03. Newly submitted v1 mint transactions are rejected at every ingress (consensus ingress, sync admission, RPC submission). Pre-existing v1-mint outputs remain spendable. No authority allowlist or config flag is built — no machinery to misconfigure.
- **D-03:** **Reject / Defer(bounded) / Accept verdict model** at every new authorization boundary this phase introduces. When referenced state (registry update, quorum evidence) has not arrived locally: defer-and-hold, retrying as dependencies land (registry-update callback / new CRDT head); reject only on deadline expiry, with buffer-full oldest-first eviction as the memory backstop. The deferral window is a config value in `network_config.json` (beside the existing reconnect/watermark knobs, per the v1.0 config-wins-with-safe-defaults pattern) with a **generous ~30-minute default** and a test/dev override so multi-node suites stay fast. User emphasis: the window must be considerable so valid late-arriving dependencies are never wrongly rejected.
- **D-04:** **Audit + cheap guards** for TRUST-02. The documented admission/equivocation audit ships together with the inexpensive guards it identifies — expected shapes: a configurable cap on ACTIVE validator additions per certified registry update, and a cross-certificate equivocation check reusing the existing durable one-vote-per-slot ledger. The audit's findings get teeth in-phase, not a follow-up.

### Claude's Discretion
- Verification mechanism for the strict registry path (full update-chain replay vs. anchored delta from last-confirmed state) — replay reusing ValidatorRegistry's existing same-file machinery is the likely shape; researcher validates sync-time cost.
- Placement/ownership of the shared verdict primitive (which owning module exposes Reject/Defer(bounded)/Accept) — planner decides with the one-owning-module rule.
- Naming and taxonomy of the per-type mint consensus rules (MINT-02's "explicitly separated" rules).
- Exact eviction policy details beyond oldest-first-under-pressure.
- How the folded trusted-peer-genesis-config hardening decomposes (pinned manifest hash, persisted genesis identity, restart-time mismatch check) — the todo's Problem section is the requirement; design is planner/researcher territory.

### Folded Todos
- **Secure trusted-peer genesis configuration** (`.planning/todos/pending/2026-08-10-secure-trusted-peer-genesis-configuration.md`, area: security) — folded into this phase by user selection. Problem: `trusted_peers`, `bootstrapper_node`, `trusted_peer_quorum_threshold`, and `burn_config_quorum_threshold` are loaded from mutable `sgns_config.json`; before TrustedPeerRegistry genesis is confirmed, the configured bootstrapper is sole signer and the peer list is copied into the live cache with no pinned manifest hash, persisted genesis identity, or restart-time mismatch check — an attacker who can replace the JSON and supply matching keys redefines the node's trust root. Fits this phase as trust-anchor hardening alongside TRUST-01. Files named by the todo: `src/account/GeniusNode.cpp:438`, `src/account/GeniusNode.cpp:758`, `src/trustedpeer/TrustedPeerRegistry.cpp:138`, `src/account/BurnConfig.cpp:95`, `src/securecrdt/QuorumThresholdValidation.hpp:29`, `example/node_test/sgns_config.json:10`.

### Deferred Ideas (OUT OF SCOPE)
- None raised during discussion — discussion stayed within phase scope.
- Reviewed todos NOT folded: bridge_race 90s window fixture (test-infrastructure follow-up); bridge startup wiring + mock RPC (`resolves_phase: 24`).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TRUST-01 | Certificate paths loading a validator registry by CID verify the registry's authorization — forged-registry self-certification rejected — without breaking catch-up sync or historical certificate re-validation (#1) | Bypass root verified (raw DAG read skips filtering, crdt_datastore.cpp:2280-2288); `VerifyUpdateClassified` machinery verified as the reuse target (:1551-1836); full 10-site consumer inventory + recursion hazard + grandfather mechanism (durable slot-record probe) documented below |
| TRUST-02 | Validator admission growth bounds and cross-certificate equivocation detection audited, findings documented (#14) | Cap already exists but is unwired (`max_new_validators_per_update_`, default 10, setter has no production caller); insertion path REGULAR/ACTIVE verified (:2145-2146); equivocation assets inventoried (ActiveVoteRecord ledger, log-only mint-hash conflict detection, batch vote-summing clamp) |
| MINT-02 | Public-chain evidence validation fails closed on empty/local chain identifiers and empty source references; native-mint and bridge-mint follow explicitly separated per-type consensus rules (#3) | Fail-open branches verified (:319-326, :329-334); existing per-type quorum split verified (`IsPublicChainMintChainId`/`IsBridgeMintSubject`/`DecideQuorum`); legitimate local-chain-id users inventoried for the allowlist (escrow scope, genesis, GeniusInputValidator registration) |
| MINT-03 | Legacy-v1 mint reception disabled via admission gating — never deserialization gating, so boot/sync/history spends keep working (#4) | Ingress map verified (consensus ingress / sync filter / local submission preflight); certificate-gated spendability verified (uncertified outputs unspendable); zero-value migration-mint trap found; `ValidateSubject` is structural-only so consensus-ingress gating does not break cert re-validation |
| MINT-04 | Burn receipt's token ID, burned amount, destination bound to mint's outputs — honest RPCs cannot authorize a mismatched mint (#5) | Current receipt check verified (status+address+topic0 only, :410-452); canonical burn identity verified (`MintTransactionV2::GetSlotID`); decode assets verified (evmrelay `BridgeEventClaim`, `abi_decoder`, registered event descriptors); single-comparison-home placement analyzed |

</phase_requirements>

## Project Constraints (from CLAUDE.md)

- C++17 only; no C++20 features. Follow repo `.clang-format`/`.clang-tidy`; Allman braces; `enum class`; `constexpr` over macros; `.hpp/.cpp`; Doxygen file headers; project-root-relative includes.
- `outcome::result<T>` for fallible paths — never blank error codes (CONCERNS.md tech debt; name the failing rule in new error paths).
- One owning module per domain rule; never duplicate logic (grep first); no business logic in transport (gRPC/RPC/serializers only marshal).
- Refactor first, then change behavior; gates not promises (ctest-enforced).
- Hard invariants: ownership/lifetime explicit (RAII, no raw new/delete); smallest abstraction (free function unless state requires class); public contracts small; portability via platform abstraction (no `#ifdef` in normal sources).
- Build-system pre-PR checklist applies if CMake changes (this phase likely adds test targets — use the existing `addtest()` function, `cmake/functions.cmake`).
- Adversarial + compat test-pair discipline: every fix ships both (named attacker authority → expected rejection → actual behavior, plus the pre-upgrade legitimate path twin). Wait-condition templates (`test/testutil/wait_condition.hpp`), never `sleep_for`.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Registry authorization (what is an authorized registry snapshot) | ValidatorRegistry (domain owner) | — | It already owns update-chain verification (`VerifyUpdateClassified`) and the registry CRDT filter; the CID loader is its public contract |
| Verified-registry consumption + caching | ConsensusManager | ValidatorRegistry | Consensus consumes registries at 10+ sites; its `LoadRegistryByCidCached` memo must serve verified-only data |
| Finality observation (D-01 grandfather probe) | ConsensusManager | CRDT datastore (read-only) | The `/cert/<slot_key>` durable record is ConsensusManager's own write product; probe is a `db_->Get` |
| Transaction-type admission (v1-mint disable) | TransactionConsensusHandler (consensus ingress) | TransactionManager (sync filter + submission preflight) | Type rules already live in `CheckTransactionTypeRules`; filters/preflight are TransactionManager's ingress surfaces |
| Public-chain evidence rules (fail-closed branches, receipt decode) | PublicChainInputValidator | evmrelay (decode library only) | Evidence rules are the validator's domain; evmrelay supplies data extraction, never comparisons |
| Burn-fact identity (token/amount/destination binding) | MintTransactionV2 slot identity (extend) | PublicChainInputValidator (comparison site) | One home per fact: `GetSlotID()` already carries all four facts; the comparison must read them, not recompute |
| Verdict semantics (Accept/Reject/Defer) | Each owning module's existing enum | — | `Check`, `UpdateVerification`, `WitnessVerdict`, `BatchSubjectDecision` already exist per domain; do not create a cross-module god-enum |
| Bounded-defer mechanism (deadline + eviction) | New small helper beside its first consumer | `crdt::CRDTWorkJournal` (durable retry) | Mechanism (generic TTL/FIFO) vs policy (per-domain); planner places it |
| Trusted-peer genesis pinning | GeniusNode boot path + GenesisManifest | TrustStartupController | Manifest canonicalization/fingerprint already owned by `GenesisManifest`; boot wiring is GeniusNode's |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| (in-tree C++17) | current develop | All remediation machinery | STACK.md conclusion holds: nothing external needed for this phase — verification, decode, retry, config, and test apparatus all exist in-tree [VERIFIED: source read this session] |
| `ValidatorRegistry::VerifyUpdateClassified` | in-tree | Registry update-chain verification (genesis root, base-registry link, signatures, quorum) | Already returns the three-verdict `UpdateVerification`; already wired fail-closed at the registry CRDT filter [VERIFIED: ValidatorRegistry.cpp:1450-1487, :1551-1836] |
| `crdt::CRDTWorkJournal` | in-tree | Durable stalled-work retry (MarkSeen/MarkStalled(backoff)/MarkDone/RecoverStaleProcessing 15s) | The proven defer backstop for certificate work; Consensus.cpp:4564-4726 [VERIFIED: source read] |
| evmrelay `eth::abi_decoder` + `BridgeEventClaim` | submodule | Receipt log event decode (token/amount/recipient extraction) | Full ABI decode + typed claim struct already exist; event descriptors for both bridge events registered [VERIFIED: evmrelay/include/eth/abi_decoder.hpp:116, bridge_event.hpp:19-40, eth_watch_cli.hpp:137-154] |
| `ConsensusAuth` primitives | in-tree | Signing bytes / signature verification | v1.1 key decision: use these directly, never ConsensusManager lifecycle [CITED: 21-CONTEXT.md Reusable Assets] |
| GTest + CTest (`addtest()`) | in-tree | Test framework + `wait_condition.hpp` macros | Existing convention [VERIFIED: test/src/blockchain/CMakeLists.txt, test/testutil/wait_condition.hpp:101-145] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `IInputValidator` registry | in-tree | Chain-id → validator routing (`SelectInputValidator`) | MINT-02 allowlist must extend this routing, not bypass it |
| `GenesisManifest`/`TrustStateStore`/`TrustStartupController` | in-tree (v1.1) | Folded-todo residual: pinned-fingerprint verification of the configured manifest | Fresh-node boot only; restart mismatch already delivered |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Chain-walk verification with memoized verified CIDs | Anchored delta only (stop at `cached_registry_id_`) | Anchor-only is faster but fails on fresh nodes (no cache) and after cache eviction; walk-with-memo is O(depth) once then O(1) — recommended |
| New shared verdict enum | Per-module enums + bounded-defer helper | A god-enum couples domains and violates one-owning-module; the semantics already exist per module — recommended: convention + mechanism |
| Certificate-conditional Stall for v1 at sync filter | Consensus+RPC gating only | Filter-only Stall stalls whole deltas (perf risk on tx volumes); consensus+RPC only leaves inert stored v1 (unspendable) — planner weighs; see Pitfall 6 |

**Installation:** none — no new packages.

## Package Legitimacy Audit

> This phase installs **zero external packages** (all-C++ in-tree remediation; STACK.md's "nothing to add" conclusion re-verified against the fix sites this session). Audit not applicable; no `[ASSUMED]` package names appear in this research.

## Architecture Patterns

### System Architecture Diagram

```
                        ┌──────────────── Peer / gossip / CRDT delta ────────────────┐
                        │                                                             │
   certificate arrives  ▼                                                             ▼  registry update arrives
┌──────────────────────────────────────┐                    ┌──────────────────────────────────────┐
│ ConsensusManager certificate path    │                    │ ValidatorRegistry ingest             │
│ (9 ValidateCertificate call sites)   │                    │ FilterRegistryUpdate (VERIFIED path) │
│  SubmitCertificate :2712/:2741       │                    │  → VerifyUpdateClassified            │
│  ProcessCertificates :3244/:3145     │                    │    kValid→Accept                     │
│  FilterCertificate :3353/:3395  ─────┼─► LoadRegistryByCid│    kMissingDependency→Stall (retry)  │
│  HandleCertificate :3766             │    (ValidatorRegistry.cpp:785)                      │
│  HasAcceptedCertificateForSlot :1383 │      RAW DAG READ — BYPASSES THE FILTER  ──┐        │
│  GetCertificateBySlot :4754          │                                          │        │
│  DispatchStalled... :4617            │                                          ▼        │
│                                      │                     db_->GetLocalDeltaKeyValues(cid)   │
│ D-01 grandfather probe:              │                     (crdt_datastore.cpp:2280,          │
│  db_->Get(/cert/<slot>) exists?      │                      created_by_self=true ⇒ no filter) │
│   yes → skip new authz (final)       │                                                    │
│   no  → strict verified load ────────┼─► NEW: verified loader (chain-walk to genesis,      │
│                                      │    memoized verified CIDs, kMissingDependency→      │
│                                      │    Check::Stalled; forged → Check::Reject)          │
└──────────┬───────────────────────────┘                                                    │
           │ verdict memo (Approve/Reject only; Stalled never cached)                       │
           ▼                                                                                │
┌─────────────────────────────────────────┐    ┌──────────────────────────────────────────┐
│ Mint/transaction admission              │    │ D-03 defer backstop                      │
│ consensus ingress: CheckTransaction-    │    │ CRDTWorkJournal (MarkStalled+backoff,    │
│   TypeRules (TCH.cpp:801) + subject     │    │ RecoverStaleProcessing 15s tick) +       │
│   handler (HandleNonceConsensusSubject) │    │ NEW bounded-defer helper (deadline from  │
│ sync filter: FilterTransaction (TM:3772)│    │ network_config.json ~30min, FIFO cap,    │
│ local submit: SendTransactionItem       │    │ oldest-first eviction — evidence_order_  │
│   preflight loop (TM:2024-2060)         │    │ pattern)                                 │
└──────────┬──────────────────────────────┘    └──────────────────────────────────────────┘
           ▼
┌─────────────────────────────────────────┐    ┌──────────────────────────────────────────┐
│ Evidence layer (MINT-02 / MINT-04)      │    │ Folded todo residual                     │
│ GatherVerificationEvidence (PCIV:307)   │    │ GeniusNode boot: configured manifest →   │
│  :319/:329 fail-open branches → closed  │    │ GenesisManifest::Fingerprint() vs pinned │
│  receipt log decode (evmrelay abi) →   │    │ ceremony fingerprint; TrustStartup-       │
│  {token, amount, recipient} compared    │    │ Controller already fails closed on       │
│  to MintV2 outputs at ONE home          │    │ configured-vs-persisted mismatch         │
└─────────────────────────────────────────┘    └──────────────────────────────────────────┘
```

Trace for the primary adversarial case (TRUST-01): attacker publishes a self-certified RegistryUpdate into the DAG (bypassing the element filter) and submits a certificate referencing its CID → certificate path (any of the 9 sites) → `LoadRegistryByCid` **strict variant** → chain-walk verification fails signature/quorum against the genesis-rooted chain → `Check::Reject` → element dropped / certificate refused. Compat twin: pre-upgrade certificate → durable `/cert/<slot>` record exists → grandfather probe skips the new rule → re-validation unchanged; fresh node syncing honest history → strict load returns `kMissingDependency` until ancestor registry deltas sync → `Stalled` → delta-stall + journal retry → converges.

### Recommended Project Structure

No new directories. Modified files only, plus tests under existing trees:

```
src/blockchain/    ValidatorRegistry.{hpp,cpp} (verified loader, cap wiring),
                   Consensus.{hpp,cpp} (grandfather probe, verified-only cache)
src/transaction/   TransactionConsensusHandler.cpp (type rules),
                   TransactionManager.cpp (filter/preflight gates)
src/account/       PublicChainInputValidator.{hpp,cpp} (fail-closed + decode + bind),
                   GeniusNode.cpp (pinned fingerprint read, config plumb)
test/src/blockchain/  (TRUST-01/02 adversarial+compat pairs; consider re-enabling
                       consensus_certificate_test — currently commented out of CMake)
test/src/account/     (MINT-02/04 via transport_factory_ DI seam)
test/src/bridge_e2e/  (receipt-binding adversarial pair; anvil fixtures exist)
```

### Pattern 1: Verified loader with recursion-safe raw core (TRUST-01)

**What:** `LoadRegistryByCid` becomes authorization-verifying; the chain walk needs base registries which today load through the same function — making it strict in-place creates infinite recursion.
**When to use:** the strict registry path.

```cpp
// Shape (names at planner's discretion):
// private, internal-only, documented untrusted:
outcome::result<Registry> LoadRawRegistryByCid( const std::string &cid ) const;   // today's body :785-820
// public, verified — what all certificate-path consumers call:
outcome::result<Registry> LoadVerifiedRegistryByCid( const std::string &cid ) const;
//  1. raw-load the RegistryUpdate at cid
//  2. VerifyUpdateClassified(update, /*enforce_time_window=*/false)   // filter precedent :1467
//  3. its base fetch must use the RAW loader + the verified-CID memo, not this function
//  4. walk prev_registry_hash chain: genesis (empty prev + genesis_authority_ sig, :1591-1610)
//     or any memoized-verified ancestor terminates the walk
//  5. memoize {cid → verified} (bounded, kRegistryCacheMaxEntries-style)
// Source: ValidatorRegistry.cpp:785-820 (raw body), :1551-1836 (verification machinery),
//         :1581 (the internal base fetch that must stay raw)
```

The four *internal* call sites (:1111 `TryCreateAndSubmitBatchProposal`, :1183 `EvaluateBatchSubject`, :1255 `HandleBatchCertificate`) are themselves consensus-critical (epoch advancement) and read the same unverified-by-CID data — the planner should decide whether they switch to the verified loader in this phase; `:1581` (inside verification) must stay raw.

### Pattern 2: Finality-bound grandfather (D-01)

**What:** Before the new registry-authorization check runs at the certificate layer, probe the durable slot record.
**When to use:** every site that would newly reject a certificate for registry-authorization reasons.

```cpp
// Mechanism — finality is observable as durable readback:
// Consensus.hpp:650  "A successful durable readback proves finality"
// Consensus.cpp:4683 same, driving ClearProposalSlot/handler dispatch
// Precedent probe — SubmitCertificate already does exactly this read:
//   auto existing = db_->Get( { key } );               // Consensus.cpp:2735
//   ... existing valid record → lower-hash ordering / skip     :2736-2749
// Grandfather rule:
//   key = GetExpectedCertificateSlotKey( certificate );         // :3571
//   if ( db_->Get( { key } ).has_value() )  → already final here → skip new authz rule
//   else                                    → in-flight/new    → strict verified path
// No persisted markers, no migration pass (D-01). Fresh-node catch-up takes the strict
// path (no local record) and converges via kMissingDependency→Stalled; honest history
// verifies because it IS on the certified chain.
```

### Pattern 3: Per-module verdicts + one bounded-defer helper (D-03)

**What:** Keep each domain's classified verdict enum; add only the missing bounded-hold mechanism.
**When to use:** every new authorization boundary this phase introduces.

```cpp
// Existing verdict enums (do NOT duplicate into a god-enum):
//   ConsensusManager::Check                Approve/Reject/Pending/Stalled   (Consensus.hpp:116)
//   ValidatorRegistry::UpdateVerification  kValid/kMissingDependency/kInvalid (VR.hpp:311)
//   ValidatorRegistry::BatchSubjectDecision Approve/Pending/Reject          (VR.hpp, :1165)
//   IInputValidator::WitnessVerdict        kValid/kNotSynced/kInvalid       (InputValidators.hpp:51)
//   crdt ElementFilterResult               Accept/Reject/Stall              (crdt_data_filter.hpp:54)
// Existing defer machinery:
//   CRDTWorkJournal: MarkSeen/MarkStalled(key,backoff)/MarkDone/RecoverStaleProcessing(
//                    PATTERN, 15s) + ListUnfinished  (Consensus.cpp:4564-4726)
//   ValidationResult{ check, dependencies, retry_after }                    (Consensus.hpp ~:790)
// Bounded-FIFO+eviction precedent to copy for the new helper:
//   evidence_by_claim_ + evidence_order_ + kMaxCachedEvidence=256, oldest-first
//   (PublicChainInputValidator.cpp:270-298) — the D-03 shape minus the TTL.
// Config: LoadNetworkConfig read_seconds(...) — cf. bootstrap_reconnect_*_sec
//   (GeniusNode.cpp:1810-1815); default ~30 min, test override.
```

### Pattern 4: Receipt-fact binding at one home (MINT-04)

**What:** Decode the matching log with evmrelay's abi_decoder into `BridgeEventClaim` fields; compare against the MintV2's own carried facts; never recompute the identity.
**When to use:** MINT-04.

```cpp
// One home for the burn-fact identity (extend, don't duplicate):
// MintTransactionV2::GetSlotID() = "mint-v2:{chain_id}:{token_hex}:{amount}:{dest_addr}:{burn_tx_hash}"
//   (MintTransactionV2.cpp:211-230) — mint outputs (amount/dest/token) + input (burn hash)
// Decode source (data extraction only — no rules in evmrelay):
//   BridgeEventClaim{ token_id_or_nonce, amount, recipient, ... }   (bridge_event.hpp:19-40)
//   abi_decoder full log decode + registered descriptors:
//     BridgeSourceBurned(address,uint256,uint256,uint256,uint256,bytes)   (eth_watch_cli.hpp:139)
//     BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool) (:154)
// Comparison site: the mint validation path that already sees both the evidence and the
// transaction (PublicChainInputValidator::ValidateWitness receives tx + params; the
// evidence slots extend to carry the decoded facts). PCIV.cpp already includes
// <eth/json_rpc.hpp> — adding <eth/abi_decoder.hpp> is the same linkage pattern.
```

### Anti-Patterns to Avoid
- **Filtering in CrdtDatastore** — the persistence layer cannot know registry rules (one-owning-module; cycle risk). Keep `GetLocalDeltaKeyValues` generic; document its contract as untrusted-raw [CITED: milestone ARCHITECTURE Pattern 1, verified current].
- **Making the loader strict in-place** — recursion via `VerifyUpdateClassified`'s own base fetch (:1581). Keep a raw core.
- **A cross-module shared verdict type** — couples domains; the enums exist per module; share the *mechanism*, not the type.
- **Putting the MINT-04 comparison in evmrelay** — business rule in a transport library; the RPC layer never sees the mint proposal.
- **Blanket v1-"mint" type block** — breaks `Migration3_4_0To3_5_0` zero-value fillers (see Pitfall 5).
- **Gating v1 at deserialization** — orphans history; violates MINT-03's own constraint.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|------|
| Registry update-chain verification | New verifier | `VerifyUpdateClassified` + `ValidateCertificate(ForUpdate)` | Already handles genesis root, base link, signatures, quorum, time-window toggle [VERIFIED: :1551-1836] |
| EVM event ABI decode | Manual topic/data slicing | evmrelay `abi_decoder` + `BridgeEventClaim` | Both event versions' descriptors registered; typed fields exist [VERIFIED] |
| Durable retry/backoff for stalled work | New retry queues | `crdt::CRDTWorkJournal` | Proven on the certificate path incl. restart recovery [VERIFIED: Consensus.cpp:4564-4726] |
| Bounded FIFO with oldest-first eviction | New container | Copy the `evidence_by_claim_`/`evidence_order_` pattern | Exactly D-03's backstop shape, already in the file being edited [VERIFIED: PCIV.cpp:270-298] |
| Quorum/signature math | Any re-derivation | `ConsensusAuth` + `TallyVotes`/`DecideQuorum` | v1.1 decision; sig-verified subset feeding slot helpers already fixed a fabricated-votes bug (:2406-2410) |
| Test waits | `sleep_for` | `ASSERT/EXPECT_WAIT_FOR_CONDITION` | House rule; existing macros [VERIFIED: wait_condition.hpp:101-145] |

**Key insight:** every mechanism this phase needs already exists in-tree in at least a sibling form; the work is routing, gating, binding, and proving — not building.

## Common Pitfalls

### Pitfall 1: Verdict memoization pins a verdict that must stay retryable
**What goes wrong:** `HasAcceptedCertificateForSlot` memoizes Approve/Reject keyed by durable-bytes digest (`certificate_verdict_cache_`, Consensus.cpp:1364-1393); the design comment asserts determinism "registry pinned by CID". If the strict registry path returns Reject for a *missing ancestor* (instead of Stalled), the cache pins it forever and a node that synced out of order never converges.
**Why:** the cache predates a verdict that can legitimately change (Stalled→Approve once dependencies sync).
**How to avoid:** map `kMissingDependency` → `Check::Stalled` (never memoized — existing behavior at :1384-1386 returns retryable failure without caching); only deterministic unauthorized → Reject.
**Warning signs:** multi-node tests flaking on sync order; nodes stuck with cached Reject for CIDs that later verify.

### Pitfall 2: Recursion / self-verification loop in the strict loader
`VerifyUpdateClassified` fetches its base registry via `LoadRegistryByCid` (:1581). Making that same function verify recurses. The verification walk must use a private raw loader plus the verified-CID memo (Pattern 1). Warning sign: stack overflow on registry load / test timeouts.

### Pitfall 3: Sync-time cost of chain-walk verification on catch-up
A fresh node validating N historical certificates against M distinct registry CIDs must not replay the chain per certificate. Mitigations (all precedented): verified-CID memo cache (bounded, `kRegistryCacheMaxEntries=8`-style — consider whether 8 is enough for history; the current cache clears wholesale at cap, :2607-2610); `VerifyUpdateClassified` already short-circuits when `prev_registry_cid == cached_registry_id_` (:1569-1589 anchor fast-path); `enforce_time_window=false` for historical loads (filter precedent :1467) so old timestamps don't fail. Warning sign: catch-up sync wall-time regression in multi-node suites.

### Pitfall 4: Grandfather probe TOCTOU / probe placement
The durable-record probe must use the same key derivation as the write path (`GetExpectedCertificateSlotKey`, :3571) and must not be added to paths that run *before* the record exists by design (FilterCertificate on a fresh node is admitting the element that becomes the record — the probe legitimately misses there and the strict path correctly applies). Records are only ever written after ValidateCertificate Approve (SubmitCertificate :2712→:2759), so an attacker cannot pre-seed a durable record to buy grandfathering.

### Pitfall 5: Migration3_4_0To3_5_0 synthesizes v1 zero-value mints
`Migration3_4_0To3_5_0.cpp:369-372` creates `MintTransaction::New(0, ...)` (type string "mint") as nonce fillers on old DBs. A blanket "mint"-type admission block breaks that migration path. Options: exempt zero-amount; or accept that pre-3.5.0 DBs are pre-cutover history (evaluate: does the migration re-run on this network class?); or switch the synthesizer's output type. Planner must pick explicitly — this is a behavior decision, not a detail. `MintFunds` itself already creates only V2 (TransactionManager.cpp:1041-1048), so local new-money issuance is not at risk.

### Pitfall 6: "Every ingress" for v1-mint vs the sync-ordering problem
`FilterTransaction` (TransactionManager.cpp:3772-3830) admits any signature-valid tx — historical v1 mints must keep flowing through it (compat twin). It cannot synchronously distinguish historical (certified) from new (uncertified): the certificate may not have arrived (unordered deltas). D-02 locks "rejected at every ingress (consensus ingress, sync admission, RPC submission)". Two viable shapes: (a) certificate-conditional defer at the sync filter (Stall the delta until the v1 mint's slot certificate arrives or the D-03 deadline expires — but note `Stall()` stalls the WHOLE delta, and tx deltas are higher-volume than cert deltas; the certificate filter already accepts that cost by design, comment :3360-3371); (b) consensus-ingress + local-submission gating plus the documented fact that an uncertified stored v1 mint is inert — its outputs are permanently unspendable because `GeniusInputValidator::ValidateWitness` requires the producer certificate (`kNotSynced`, GeniusInputValidator.cpp:496-510). Research recommendation: (b) as the shipped behavior with (a) as an explicit follow-up if the audit deems stored-but-inert v1 mints unacceptable — but D-02's wording is the user's lock; the planner should either implement (a) or surface the trade-off to the user before planning completes.

### Pitfall 7: The local-chain-id allowlist breaks legitimate routing
`sources of legitimacy for empty/"supergenius" chain ids (verified): `GENIUS_CHAIN_ID` scoped escrow chain ids (`ScopedChainId` → "supergenius" and "supergenius/<private_net>", TransactionManager.hpp:955-962), Blockchain genesis (Blockchain.cpp:1120), and `GeniusInputValidator`'s self-registration for "supergenius" (GeniusInputValidator.cpp:116). MINT-02's fail-closed must be **per transaction type** (mint types require a public-chain source reference; escrow/registration/genesis keep the local path), not a blanket chain-id rejection — otherwise private-network escrow and genesis break. `IsPublicChainMintChainId` (Consensus.cpp:57-70) already classifies ("public" or all-digits) — extend the classification, don't fork it.

### Pitfall 8: Phase 24 rewrites the same function
`GatherVerificationEvidence` gets rewritten again in Phase 24 (confirmation depth, operator-diverse content corroboration). Keep this phase's edits surgical: close the two fail-open branches, add the decode+bind, extend evidence slots. Structure the receipt-decode output (canonical facts) so Phase 24's content-digest corroboration consumes the same decode — one fact, one field.

### Pitfall 9: consensus_certificate_test is not in the build
`test/src/blockchain/CMakeLists.txt:12-18` has the target commented out. The milestone research names it as the seam for #1 tests; the planner must either re-enable it (verify it compiles) or build the TRUST-01 adversarial+compat pairs on `multi_node_finality_fault_test` (real-socket, already references `LoadRegistryByCid` at :1878) and the registry unit tests.

### Pitfall 10: Cross-certificate equivocation is currently log-only
`FilterCertificate` detects two certificates at the same canonical slot minting different transaction hashes and logs `critical` — then proceeds with lower-hash ordering (Consensus.cpp:3405-3417). The durable one-vote-per-slot ledger (`ActiveVoteRecord`, `ACTIVE_VOTE_BASE_PATH_KEY` :1336) binds a voter to one slot locally. TRUST-02's audit should cover: the log-only equivocation response, the batch vote-summing clamp (a validator voting yes on one member cert and no on another nets to approve, HandleBatchCertificate :1319-1354), and REGULAR/ACTIVE insertion of unregistered voters (cap 10/update, unbounded across epochs).

## Code Examples

### Existing fail-open branches to close (MINT-02)
```cpp
// Source: src/account/PublicChainInputValidator.cpp:319-334 (verified current)
if ( source_reference.empty() )
{
    // Nothing to verify against a public chain: the claim is accepted but
    // no endpoint confirmed anything, so every slot stays empty.
    evidence.valid = true;                       // ← FAIL-OPEN #1: becomes per-type reject
    return evidence;
}
const auto chain_id = tx.GetChainId();
if ( chain_id.empty() || chain_id == "supergenius" )
{
    evidence.valid = true;                       // ← FAIL-OPEN #2: becomes type-authorized path
    return evidence;
}
```

### Existing grandfather probe precedent (D-01)
```cpp
// Source: src/blockchain/Consensus.cpp:2735-2749 (SubmitCertificate, verified current)
auto existing = db_->Get( { key } );             // key = canonical slot record
if ( existing.has_value() )
{
    Certificate existing_certificate;
    if ( !existing_certificate.ParseFromArray(...) ||
         !ValidateCertificateKey( existing_certificate, key ) ||
         ValidateCertificate( existing_certificate ) != Check::Approve ) { ... }
    if ( SerializedCertificateHash( existing.value().toString() )
        < SerializedCertificateHash( serialized ) )
    {
        return outcome::success();               // durable record already final
    }
}
```

### Existing bounded-FIFO eviction to model D-03's buffer on
```cpp
// Source: src/account/PublicChainInputValidator.cpp:278-284 (verified current)
while ( evidence_order_.size() > kMaxCachedEvidence )   // kMaxCachedEvidence = 256
{
    evidence_by_claim_.erase( evidence_order_.front() );
    evidence_order_.pop_front();                 // oldest-first under pressure
}
```

### Existing registry-filter three-verdict wiring (the pattern the strict loader joins)
```cpp
// Source: src/blockchain/ValidatorRegistry.cpp:1467-1487 (verified current)
switch ( VerifyUpdateClassified( update, false ) )
{
case UpdateVerification::kValid:            return ElementFilterResult::Accept();
case UpdateVerification::kMissingDependency: return ElementFilterResult::Stall(); // retry via failed-root machinery
case UpdateVerification::kInvalid: default:  return ElementFilterResult::Reject();
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Trust fetched validator-set blobs by content address | Signed validator-set digest chained from genesis (Tendermint `NextValidatorSetHash` model) | ecosystem-standard | SuperGenius equivalent = verify RegistryUpdate chain at CID load, reusing the ceremony/quorum machinery [CITED: milestone FEATURES.md Cluster B] |
| Attestation counts endpoints / URL hashes | Attestation commits to full payload; executor binds effects to exactly that payload (Wormhole/tBTC invariant) | ecosystem-standard | MINT-04 = decode receipt event, bind to mint outputs at one home [CITED: milestone FEATURES.md Cluster C] |
| Binary accept/reject at authorization boundaries | Classified verdicts (valid/missing-dependency/invalid) with bounded defer | in-tree evolution (v3.0/v1.1 era) | Four enums + work journal already exist; D-03 formalizes the bound |

**Deprecated/outdated (in this repo):** none of the target machinery is deprecated; the `TODO - Check if filtering is needed here` at crdt_datastore.cpp:2284 acknowledges the exact gap TRUST-01 closes.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `enforce_time_window=false` is correct for historical/strict loads (filter precedent :1467 uses false; only `CreateUpdateFromCertificate` paths enforce windows) | Pattern 1 | If time windows must hold for historical verification, catch-up of old registries could Reject — compat twin would catch it |
| A2 | Registry update-chain depth since genesis is small enough that memoized chain-walk is cheap on catch-up (registry updates are per-epoch batch events, not per-block) | Pitfall 3 | If depth is large, sync-time cost forces the anchored-delta variant; mechanism unchanged |
| A3 | Pre-cutover v1 mints on live/test networks carry finalized certificates (v3.0 cert machinery is the only consensus version ever deployed — Consensus.cpp:2773-2782 comment) | MINT-03 compat | If v1 history exists without certificates, certificate-conditional sync gating (Pitfall 6 option a) would strand it; option (b) unaffected |
| A4 | Zero-value exemption (or equivalent) suffices to keep Migration3_4_0To3_5_0 working under a v1-mint admission block | Pitfall 5 | If old-DB migration must still create *value-carrying* v1 mints (it does not — verified `New(0, ...)`), the exemption shape changes |
| A5 | ~30-minute D-03 default window is generous relative to observed CRDT dependency lag (no measured lag data consulted this session) | D-03 | Too short wrongly rejects; the config override and the generous default mitigate |
| A6 | The audit document's home (suggested `AgentDocs/` or `.planning/notes/`) is acceptable to the user | TRUST-02 | Cosmetic |

**All other claims in this research were verified against current source this session (file:line cited inline).**

## Open Questions (RESOLVED)

> All five questions were dispositioned during plan creation (7 plans, 3 waves). Each carries its resolution and the plan reference below.

1. **D-02 sync-admission shape** — implement certificate-conditional Stall at `FilterTransaction` (literal "every ingress") or consensus+local-submission gating with documented inertness of stored uncertified v1? What we know: uncertified v1 outputs are unspendable (verified); Stall stalls whole deltas. Recommendation: surface to user in planning; default to the literal D-02 reading with the defer mechanism, sized by the audit.
   - What's unclear: whether stored-but-inert v1 mints are acceptable network state.
   - **(RESOLVED)** Option (a) — certificate-conditional Stall at the sync filter, the literal D-02 reading. Plan **21-06** (Wave 3): FilterTransaction returns `ElementFilterResult`; a value-carrying v1 mint without its slot certificate Stalls (bounded by the D-03 defer window, Reject on expiry); since 21-05 blocks new v1 mints from ever obtaining a certificate, "certificate exists" exactly separates history from new submissions. Whole-delta stall cost accepted per the certificate-filter precedent (Consensus.cpp:3360-3371).
2. **Which internal ValidatorRegistry CID loads go strict in-phase** (:1111/:1183/:1255 are epoch-advancement-critical; :1581 must stay raw). Recommendation: strict for the three, raw core for verification internals.
   - **(RESOLVED)** As recommended. Plan **21-01**, Task 1 steps 5-6: all three internal sites (:1111 TryCreateAndSubmitBatchProposal, :1183 EvaluateBatchSubject, :1255 HandleBatchCertificate) switch to `LoadVerifiedRegistryByCid` preserving each site's existing load-error handling; the :1581 verification-internal base fetch moves to the raw (unverified) core as the recursion guard.
3. **Verified-CID memo cache sizing** — current `kRegistryCacheMaxEntries=8` with clear-all eviction is likely too small for history + clears wholesale; recommend bounded LRU-style with a larger cap; planner sets the number.
   - **(RESOLVED)** Cap 64 with oldest-first single-entry eviction (never clear-all), in both places: the ValidatorRegistry verified-CID memo (`kMaxVerifiedRegistryMemoEntries=64`, plan **21-01** Task 1 step 4) and the ConsensusManager CID cache (`kRegistryCacheMaxEntries` 8→64 + FIFO eviction, plan **21-04** Task 2).
4. **consensus_certificate_test re-enablement** — is it commented out for a reason (build breakage) that this phase fixes, or should tests go elsewhere?
   - **(RESOLVED)** Stays disabled — no re-enablement attempted this phase. TRUST-01 integration pairs ride the existing multi-node apparatus (multi_node_finality_fault_test) and fast unit pairs use the static chain-walk helper seam. Plans **21-01** Task 2 (read_first disposition of Pitfall 9) and **21-04** Task 3 (integration pairs); recorded in 21-VALIDATION.md (config-files row).
5. **Pinned-fingerprint delivery for the folded todo** — config field vs build-time constant vs both; the staging config's `_trust_configuration_notice` already carries a ceremony fingerprint string (example/node_test/sgns_config.json:4) but nothing reads it.
   - **(RESOLVED)** Config field: new sgns_config.json string `genesis_manifest_fingerprint`, parsed in the trust-field block and enforced at manifest assembly via `GenesisManifest::Fingerprint()` (mismatch → FATAL_TRUST_MISMATCH; empty pin → documented unpinned warning). Build-time constant rejected (breaks private networks); the example config carries the COMPUTED fingerprint and the notice text is updated to match. Plan **21-03**, Task 3 (threat T-21-0302 documents the accepted whole-file-replacement residual).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake + CTest | build/test | ✓ | 3.31.4 | — |
| Existing build trees `build/OSX/{Debug,Release,Release-ASan}` | incremental build/test | ✓ | present | — |
| evmrelay submodule (abi_decoder, BridgeEventClaim) | MINT-04 decode | ✓ (in-tree headers; PCIV already links eth/ headers) | current develop | — |
| anvil/Foundry fixtures | bridge_e2e adversarial pair (MINT-04) | ✓ (test/src/bridge_e2e/anvil_fixture.hpp; suite carries `requires_foundry` skip pattern) | current | mock transport via `transport_factory_` DI seam |
| GTest / wait_condition | all tests | ✓ | in-tree | — |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** none.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GTest via CTest (`addtest()` helper, cmake/functions.cmake) |
| Config file | per-directory `test/src/*/CMakeLists.txt` |
| Quick run command | `ctest --test-dir build/OSX/Release -R <target> --output-on-failure` (single target; build first with `cmake --build build/OSX/Release -j`) |
| Full suite command | `ctest --test-dir build/OSX/Release --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TRUST-01 (adversarial) | Forged/self-certified registry CID referenced by a certificate → Reject, never Approve | integration (DAG injection + certificate submission) | `ctest --test-dir build/OSX/Release -R validator_registry` (+ new target) | ❌ Wave 0 (new) — extend `validator_registry_batch_slot_test` or new file; apparatus: multi_node_finality_fault_test pattern |
| TRUST-01 (compat) | Pre-upgrade certificates re-validate after restart; catch-up sync completes unchanged | integration (restart + fresh-node sync) | `ctest --test-dir build/OSX/Release -R multi_node_finality_fault` | ✅ (extend with upgrade-scenario fixture) |
| TRUST-02 | Audit doc + cap wiring + equivocation check | unit (cap enforcement: `validator_registry_promotion_test` pattern) + doc deliverable | `ctest --test-dir build/OSX/Release -R validator_registry_promotion` | ✅ (extend) |
| MINT-02 (adversarial) | Empty source-ref / empty-or-local chain-id mint → reject where today accepts | unit (mock transport via `SetTransportFactory`, PCIV.hpp:204) | `ctest --test-dir build/OSX/Release -R public_chain_input_validator` | ✅ (`public_chain_input_validator_slot_test.cpp`; extend) |
| MINT-02 (compat) | Legitimate local-chain types (escrow scope, genesis, registration) still validate | unit | same target | ✅ (extend) |
| MINT-03 (adversarial) | Newly submitted self-signed v1 mint rejected at consensus ingress (+ sync/local per chosen shape) | unit/integration | new target under `test/src/transaction*` or blockchain | ❌ Wave 0 |
| MINT-03 (compat) | Pre-cutover v1 outputs spendable; fresh node syncs to READY; restart recovers | integration | `multi_node_finality_fault` + migration fixture | ✅ (extend) |
| MINT-04 (adversarial) | Receipt honest but token/amount/destination mismatches mint → reject | unit (mock RPC serving crafted receipts) | extend `public_chain_input_validator` / `bridge_e2e` with transport factory | ✅ seam (extend) |
| MINT-04 (compat) | Honest agreeing endpoints still mint exact amounts | integration | `bridge_e2e` (anvil or mock) | ✅ (extend) |
| Folded todo (adversarial) | Swapped sgns_config trust fields on fresh node → fail closed | unit (boot path with temp config) | extend `burnconfig_policy_e2e_test` / `network_config_precedence_test` style | ✅ seam (extend) |
| Folded todo (compat) | Correct ceremony fingerprint boots; restart from confirmed state unaffected | unit/integration | same | ✅ (extend) |

### Sampling Rate
- **Per task commit:** targeted ctest for the touched area (commands above), changed-file clang-format + clang-tidy per CLAUDE.md gate
- **Per wave merge:** `ctest --test-dir build/OSX/Release --output-on-failure` (full)
- **Phase gate:** full suite green (incl. multi-node suites) before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] TRUST-01 adversarial fixture: DAG-injection of a filter-rejected RegistryUpdate + certificate referencing its CID (new test file or extension; decide consensus_certificate_test re-enable vs multi-node apparatus)
- [ ] MINT-03 v1-mint fixtures (self-signed v1 mint builder exists in bridge_e2e_chainlist_test.cpp:296-341 — reuse the construction pattern)
- [ ] Verify the chosen sync-admission shape's test (certificate-conditional defer, if selected) has a negative control proving the Stall actually resolves

*(Framework and shared fixtures otherwise exist; no new infrastructure required.)*

## Security Domain

> `security_enforcement: true`, ASVS level 1 (config). This is a C++ node/consensus codebase — web-tier ASVS categories map as follows.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
4 | V2 Authentication | yes (peer/message auth) | Existing `ConsensusAuth`/multisig `VerifyPayloadSignature` — reuse, never add a scheme |
| V3 Session Management | no | n/a (no user sessions on this path) |
| V4 Access Control | yes | Admission gating at consensus ingress / sync filter / submission preflight (D-02); type-authorized allowlist (MINT-02) |
| V5 Input Validation | yes | Fail-closed evidence branches (MINT-02); receipt-fact binding (MINT-04); `outcome::result` error propagation, named errors |
| V6 Cryptography | yes (verification only) | Existing ed25519/secp256k1 verify + SHA-256 slot hashes; **never hand-roll**; evmrelay keccak for message ids |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Forged validator registry via CID bypass (finding #1) | Elevation/Spoofing | Verified loader chaining to genesis authority (Pattern 1) + verified-only cache |
| Legacy-type unauthorized issuance (finding #4) | Elevation | Admission gating per ingress + certificate-gated spendability (existing) |
| Empty-identity fail-open (finding #3) | Spoofing | Per-type explicit authorization; no blanket bypass strings |
| Receipt/mint mismatch (finding #5) | Tampering | Exact payload binding at one comparison home (Wormhole invariant) |
| Trust-root swap via mutable config (folded todo) | Elevation | Pinned manifest fingerprint verified at boot; existing FATAL_TRUST_MISMATCH path |
| Verdict-cache poisoning via retryable-reject | Denial of Service | Missing-dependency → Stalled (never memoized) — Pitfall 1 |
| Delta-stall DoS via deferred sync gating | DoS | Bounded defer (D-03 deadline + FIFO eviction) |

## Sources

### Primary (HIGH confidence — read and verified in current develop this session)
- `src/blockchain/ValidatorRegistry.cpp` — :785-820 (raw CID loader), :918-915/948-956 (cap setter), :1111/:1183/:1255/:1581 (internal CID loads), :1450-1487 (registry filter three-verdict wiring), :1551-1836 (`VerifyUpdateClassified`), :2100-2160 (`InsertNewValidators`, REGULAR/ACTIVE insertion, cap enforcement); `ValidatorRegistry.hpp` — :67 (DefaultMaxNewValidatorsPerUpdate=10), :311-328 (`UpdateVerification`)
- `src/blockchain/Consensus.cpp` — :57-70 (`IsPublicChainMintChainId`), :116 `Check` enum (hpp), :1336-1393 (verdict memo cache), :2406-2410 (sig-verified subset comment), :2504/:2516-2577 (`TallyVotes`/`IsBridgeMintSubject`/`DecideQuorum`), :2589-2614 (`LoadRegistryByCidCached`, cap 8, clear-all), :2706-2789 (`SubmitCertificate` incl. durable-record probe :2735-2749 and PutConvergentImmutable :2759), :3290-3372 (`RegisterCertificateFilter`/`FilterCertificate` incl. Stall semantics + equivocation log), :3445-3545 (`ValidateCertificate`, raw load :3513, Stalled :3521), :4311-4366 (`ValidateSubject` — structural only), :4564-4726 (work journal retry), :4675-4700 (durable-readback finality), :4730-4762 (`GetCertificateBySlot` re-validation); `Consensus.hpp` — :39-41 (subject types), :650 (finality contract), :1243-1253 (cache contract)
- `src/crdt/impl/crdt_datastore.cpp` — :2280-2288 (`GetLocalDeltaKeyValues` — `created_by_self=true` filter bypass + TODO)
- `src/account/PublicChainInputValidator.{hpp,cpp}` — :307-490 (`GatherVerificationEvidence`; fail-open :319/:329; receipt check :410-452; URL-hash slots :459-466), :270-298 (bounded FIFO eviction), hpp :111-260 (validator contract, transport factory DI, claim binding)
- `src/account/InputValidators.hpp` — :51-56 (`WitnessVerdict`)
- `src/transaction/TransactionConsensusHandler.cpp` — :513-556 (`ValidateTransactionForConsensus` + TODO :545), :801-820 (`CheckTransactionTypeRules`), :821+ (`ValidateWitnessForConsensus`)
- `src/transaction/TransactionManager.cpp` — :110-127 (`transaction_parsers`: mint/mint-v2/migration share ParseMintTransaction), :918-1048 (`MintFunds` — V2 only), :1939-1980 (`SelectInputValidator` chain routing), :2024-2060 (submission preflight loop), :3772-3830 (`FilterTransaction`)
- `src/account/MintTransactionV2.cpp` — :211-230 (`GetSlotID` canonical burn identity); `MintTransaction.cpp` — :15 (type string "mint")
- `src/migration/Migration3_4_0To3_5_0.cpp` — :369-372 (zero-value v1 mint synthesis)
- `src/account/GeniusInputValidator.cpp` — :443-447 (escrow exception, Phase-26 scope), :476-530 (producer-certificate-gated spendability), :116 ("supergenius" registration)
- `src/account/GeniusNode.cpp` — :496-524 (trust fields from sgns_config.json), :886-960 (TrustStartupController wiring, FATAL_TRUST_MISMATCH), :1621-1815 (`LoadNetworkConfig` read/read_seconds, watermark/reconnect knobs)
- `src/trustedpeer/` — `GenesisManifest.hpp` (Fingerprint/DecodeAndVerify), `TrustStateStore.hpp` (durable confirmed snapshots, error taxonomy), `TrustStartupController.cpp` (:19-46 ConflictingFields)
- `evmrelay/include/eth/` — `bridge_event.hpp` (:19-40 BridgeEventClaim), `abi_decoder.hpp` (:116), `eth_watch_cli.hpp` (:137-154 event descriptors)
- Test infrastructure: `test/src/blockchain/CMakeLists.txt` (consensus_certificate_test commented out :12-18; live targets), `test/testutil/wait_condition.hpp`, `test/src/bridge_e2e/` (anvil fixture, transport-factory mocks), `fuzz/` (3 targets)

### Secondary (MEDIUM confidence — milestone research, cross-corroborated)
- `.planning/research/{ARCHITECTURE,PITFALLS,FEATURES,SUMMARY,STACK}.md` — patterns and external citations (Tendermint valset binding, Wormhole payload binding, BIP height-gated activation); line numbers from these docs were re-verified and corrected where drifted (ValidateCertificate sites are 9, not 4; registry loads in Consensus are 5 direct + 2 cached)
- `.planning/notes/security-assessment-2026-10-response.md` — findings #1/#3/#4/#5/#14 triage text

### Tertiary (LOW confidence)
- None — no WebSearch-derived claims are load-bearing in this research.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all in-tree, verified by direct source read
- Architecture (verified loader / grandfather / ingress map / binding home): HIGH — every mechanism cited at file:line in current develop
- Pitfalls: HIGH for repo mechanics (verified); MEDIUM for the two assumption-shaped items (A1 time-window scope, A3 pre-cutover certificate coverage)
- Folded-todo residual scoping: HIGH for what exists (verified); the decomposition choice is planner discretion

**Research date:** 2026-10-19
**Valid until:** 2026-11-19 (stable repo; re-verify line numbers if develop moves — the two files most likely to drift are Consensus.cpp and ValidatorRegistry.cpp)
