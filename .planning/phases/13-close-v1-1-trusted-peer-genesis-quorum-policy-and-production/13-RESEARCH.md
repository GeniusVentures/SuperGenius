# Phase 13: Close v1.1 trusted-peer genesis, quorum-policy, and production integration gaps - Research

**Researched:** 2026-08-11
**Domain:** authenticated trust bootstrap, versioned quorum policy, content-addressed SecureCrdt governance, crash-safe local trust state, and node/account lifecycle integration
**Confidence:** HIGH for the existing-code diagnosis and required invariants; MEDIUM for the recommended new canonical codec, operator UI, and local-state schema

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

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

### Deferred Ideas (OUT OF SCOPE)

No new product capabilities were deferred from the discussion.

### Reviewed Todos (not folded)
- **bridge_race fixture — not all 11 nodes mint within the 90s race window (post-fix)** — unrelated bridge-test timing issue; outside Phase 13's trust-policy boundary.
- **Bridge Startup Wiring + Mock RPC Endpoints** — unrelated bridge/RPC integration work; outside Phase 13 and still tracked separately.
</user_constraints>

<phase_requirements>
## Phase Requirements

No ROADMAP requirement IDs are mapped to Phase 13. The following decision and folded-todo IDs are mandatory planning inputs. [VERIFIED: 13-CONTEXT.md]

| ID | Description | Research Support |
|----|-------------|------------------|
| D-01..D-04 | One-shot, manually reviewed, ephemeral-key trusted-peer genesis with a persisted canonical fingerprint | Canonical Genesis Manifest, Genesis Command Boundary, Startup State Machine, and threat model below. [VERIFIED: 13-CONTEXT.md] |
| D-05..D-08 | Persisted versioned policy; exact membership and burn floors; current-policy successor authorization | Versioned Trust Policy and Atomic Activation patterns below. [VERIFIED: 13-CONTEXT.md] |
| D-09..D-12 | Direct authenticated, explicit-approval, content-addressed SecureCrdt candidates | Signed Candidate Approval Record pattern below. [VERIFIED: 13-CONTEXT.md] |
| D-13..D-16 | Restricted first boot, persisted restart authority, rollback rejection, node-scoped lifetime | Startup State Machine, TrustStateStore, and Node-Scoped Ownership patterns below. [VERIFIED: 13-CONTEXT.md] |
| BOOT-01 | Document manual collection, verification, canonicalization, review, and placeholder status | Operator runbook and genesis CLI acceptance criteria below. [VERIFIED: folded todo] |
| BOOT-02 | Canonical authenticated genesis manifest bound to network, bootstrapper, peers, policies, and version | Canonical Genesis Manifest pattern below. [VERIFIED: folded todo] |
| BOOT-03 | Confirm genesis through production SecureCrdt and persist it | Genesis command and first-boot E2E map below. [VERIFIED: folded todo] |
| BOOT-04 | Restart and rollback safety | Persist-before-publish activation and rollback tests below; D-14 supersedes the todo's generic “fail closed on any JSON conflict” wording. [VERIFIED: 13-CONTEXT.md; folded todo] |
| POLICY-01 | Thresholds are signed policy state, never post-confirmation JSON authority | TrustPolicyState data model below. [VERIFIED: folded todo] |
| VALID-01 | Non-empty unique valid signers and complete threshold bounds/floors | Validation Matrix and code example below. [VERIFIED: folded todo] |
| TEST-01 | Tamper, mismatch, rollback, restart, production genesis, and live BurnConfig coverage | Validation Architecture below. [VERIFIED: folded todo] |
</phase_requirements>

## Summary

The existing Phase 8-11 components are useful foundations, but their current shape cannot simply be “wired harder.” `SecureCrdt` stores one mutable value at a logical base key and stores signatures beneath that key; a new proposal overwrites the prior candidate, the remote value filter validates structure but not proposer membership, and the process-global registry can be replaced by another node instance in the same process. `TrustedPeerRegistry` immediately caches mutable-config peers before genesis confirmation, and `BurnConfig` can seed from that unconfirmed cache. [VERIFIED: `src/securecrdt/SecureCrdt.cpp`, `src/securecrdt/SecureCrdtRegistry.hpp`, `src/trustedpeer/TrustedPeerRegistry.cpp`, `src/account/BurnConfig.cpp`]

Phase 13 should add a candidate-oriented SecureCrdt path alongside the legacy Phase 9 API, make policy registrations instance-scoped, and represent every proposal/approval as one self-contained signed CRDT element under a version-and-content-hash key. That removes the unauthenticated-base-value ordering problem: the first retained element is already a verified current-peer approval, while subsequent approvals carry the same canonical candidate bytes and signature. [ASSUMED]

Confirmed state should be a node-local, network-scoped `TrustStateStore` backed by the repository's existing synchronous RocksDB adapter. Activation must serialize under one transition mutex, re-read the persisted head, re-derive quorum under the current policy, commit the new snapshot durably, then publish it to in-memory caches. This ordering makes restart recovery choose the durable winner and makes a concurrent loser stale. [VERIFIED: `src/storage/rocksdb/rocksdb.cpp`; ASSUMED for the new store/ordering]

**Primary recommendation:** Build Phase 13 around three explicit boundaries—`CanonicalTrustCodec`, candidate-oriented `SecureCrdt`, and crash-safe `TrustStateStore`—then make `GeniusNode` a state-machine orchestrator whose policy services outlive account selection. [ASSUMED]

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Manual genesis review and explicit candidate approval | Local operator CLI/UI | Node service | The user action is local, while signing/submission must call the node's canonical policy service; no unauthenticated remote admin API is allowed. [VERIFIED: D-03, D-11] |
| Candidate authentication, content addressing, signature/quorum evaluation | API / Backend (node core) | CRDT / Storage transport | SecureCrdt owns validation and GlobalDB carries only accepted signed records. [VERIFIED: D-09..D-12; codebase grep] |
| Confirmed policy and burn head persistence | Database / Storage | Node core cache | Local synchronous storage is the restart authority; caches are projections of durable state. [VERIFIED: D-02, D-14, D-15] |
| Network propagation | CRDT / GlobalDB | PubSub/Graphsync | The phase must reuse the existing CRDT topic and must not add a proposal RPC/pubsub lifecycle. [VERIFIED: D-09; PROJECT.md] |
| Restricted-first-boot and economic-operation gate | GeniusNode lifecycle | TransactionManager | GeniusNode knows startup state; TransactionManager must refuse BurnConfig-dependent work until confirmed policy and burn state are ready. [VERIFIED: D-13, D-16] |
| Account switching | GeniusNode account layer | TransactionManager | Only account-bound services are recreated; policy services and callbacks remain attached to the retained GlobalDB. [VERIFIED: D-16; `GeniusNode::SelectAccount`] |

## Standard Stack

No new external package is needed or recommended. The phase should use repository-resident targets already linked by the affected components. [VERIFIED: codebase grep]

### Core

| Library / Component | Version | Purpose | Why Standard |
|---------------------|---------|---------|--------------|
| C++ | C++17 | Canonical models, state machines, lifecycle, and validation | The project is C++17 and the affected modules already use `outcome::result`, shared ownership, atomics, and mutexes. [VERIFIED: codebase map and source] |
| `SecureCrdt` + `GlobalDB` | repository current | Candidate/approval transport and remote filters/callbacks | Locked decision D-09 requires CRDT as the only transport. [VERIFIED: 13-CONTEXT.md] |
| `multisig` + `GeniusSigner` | repository current | Canonical signature verification and ephemeral/current-peer signing | Existing TPR/Burn paths already use these exact signature bytes and verification functions. [VERIFIED: `MultiSig.cpp`, `GeniusSigner.cpp`] |
| `storage::rocksdb` | repository current; synchronous writes enabled | Local node-scoped confirmed trust state | `rocksdb::create()` sets `WriteOptions.sync = true`, and batch commits use that same write option. [VERIFIED: `src/storage/rocksdb/rocksdb.cpp:22-64`, `rocksdb_batch.cpp:29-37`] |

### Supporting

| Library / Component | Version | Purpose | When to Use |
|---------------------|---------|---------|-------------|
| `crypto::sha256` / existing SHA-256 stack | repository current | Candidate hashes and genesis fingerprint | Use one named helper over canonical bytes; do not mix `std::hash`, CID encoding, and SHA APIs. [VERIFIED: existing SHA-256 implementations; ASSUMED recommendation] |
| Boost.Program_options | repository current | One-shot CLI parsing | The repository already uses it for command executables; keep private key material out of argv. [VERIFIED: `example/crdt_globaldb/globaldb_app.cpp`; ASSUMED recommendation] |
| GoogleTest / CTest | repository current | Focused unit/integration/E2E coverage | Existing v1.1 tests use `addtest` and CTest targets. [VERIFIED: `test/src/*/CMakeLists.txt`] |
| OpenSSL cleanse API | environment 3.6.2 | Best-effort zeroization of in-memory key buffers | OpenSSL documents that `OPENSSL_cleanse` is intended to avoid compiler-elided clearing, while warning that storage wear-leveling prevents reliable file overwrite. [CITED: https://docs.openssl.org/3.2/man3/OPENSSL_malloc/] |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Dedicated synchronous RocksDB trust store | Atomic temp-file + `fsync` + `rename` | POSIX rename is atomic, but cross-platform durable replacement and directory syncing add platform-specific code; the existing RocksDB wrapper already performs synchronous atomic batch writes. [CITED: https://pubs.opengroup.org/onlinepubs/9799919799/functions/rename.html; VERIFIED: codebase grep] |
| Signed approval record containing candidate bytes | Unsigned base candidate plus separate signature child | The latter creates an arrival-order gap where an unauthenticated value is retained or a valid signature arrives before its base. [VERIFIED: current filter behavior; ASSUMED design conclusion] |
| Instance-scoped `SecureCrdtRegistry` | Keep static process-global registry | The static map lets multiple `GeniusNode` instances replace each other's signer sources, which conflicts with node-scoped ownership and in-process E2E tests. [VERIFIED: `SecureCrdtRegistry.hpp`; milestone audit] |
| Local terminal/admin command | New remote administration RPC | A remote mutation surface expands authentication and authorization scope and violates the locked no-new-RPC boundary. [VERIFIED: D-09, discretion constraint] |

**Installation:** none. [VERIFIED: codebase grep]

## Architecture Patterns

### System Architecture Diagram

```text
FIRST BOOT
reviewed genesis manifest + ephemeral key file
        │ validate/canonicalize/show fingerprint/explicit confirmation
        ▼
one-shot sgns-trust-genesis command
        │ signs canonical genesis core, writes signed approval record
        ▼
GlobalDB / existing CRDT topic ───────────────► other fresh nodes
        │ remote filter: parse key+record, hash match, bootstrap signature match
        ▼
node-scoped policy callback
        │ quorum? (genesis bootstrapper threshold = 1)
        ▼
TrustStateStore durable commit ──► in-memory policy cache ──► Burn genesis gate
        │                                                   │
        │ restart                                           └─► economic ops enabled only
        ▼                                                       after required confirmed state
load + verify persisted chain/head
        ├─ network mismatch ─► fail startup
        ├─ JSON conflict ────► critical diagnostic; ignore JSON trust copies
        └─ valid head ───────► sync CRDT; reject older/fork/non-descendant candidates

LATER POLICY/BURN CHANGE
local operator proposes/approves
        │ candidate core = domain + network + type + version + prev hash
        │                  + authorizing-policy hash + payload
        ▼
<root>/candidate/v<version>/<sha256(core)>/approval/<signer>
        │ value = core + signature(core)
        ├─ invalid / unauthorized ─► remote filter rejects, callback never surfaces
        └─ valid ──────────────────► candidate inbox; never auto-sign
                                      │ explicit local approvals
                                      ▼
                                  activation mutex
                                      │ head still matches? quorum under current policy?
                                      ├─ no ─► stale/alert
                                      └─ yes ► durable commit, then cache publish
```

The diagram is the recommended design, derived from the locked decisions and current filter/callback APIs. [ASSUMED]

### Recommended Project Structure

```text
src/
├── securecrdt/
│   ├── SecureCrdtCandidate.hpp/.cpp    # signed candidate approval records and key codec
│   ├── SecureCrdtRegistry.hpp          # instance-scoped policy registration
│   └── SecureCrdt.hpp/.cpp             # candidate propose/approve/query/filter API
├── trustedpeer/
│   ├── CanonicalTrustCodec.hpp/.cpp    # domain-separated canonical byte encoding + hashes
│   ├── GenesisManifest.hpp/.cpp        # reviewed bootstrap input/fingerprint
│   ├── QuorumPolicy.hpp/.cpp           # peers + both thresholds + versions/hashes
│   ├── TrustStateStore.hpp/.cpp        # synchronous RocksDB snapshot/head persistence
│   ├── TrustedPeerRegistry.hpp/.cpp    # candidate inbox, activation, policy cache
│   └── genesis_tool/                   # one-shot executable, thin over reusable ceremony service
├── account/
│   ├── BurnConfig.hpp/.cpp             # burn candidate lifecycle + shared node-scoped cache
│   └── GeniusNode.hpp/.cpp             # restricted boot and lifetime orchestration
└── ...
test/src/
├── securecrdt/                         # candidate addressing/authentication/concurrency
├── trustedpeer/                        # manifest, policy, store, genesis, rollback
├── account/                            # live BurnConfig/PayEscrow
├── startup/                            # first boot/restart/tamper
└── multiaccount/                       # SelectAccount policy/callback lifetime
```

This structure keeps new trust logic out of the already-large `GeniusNode.cpp` and `TransactionManager.cpp`, matching the codebase concern that changes to those files should be thin orchestration. [VERIFIED: codebase CONCERNS.md; ASSUMED mapping]

### Pattern 1: Canonical Genesis Manifest

**What:** Define one map-free, domain-separated canonical byte codec with explicit field order, fixed-width big-endian integers, length-prefixed byte strings, sorted peer addresses, and an encoding version. Hash exactly those bytes with SHA-256. [ASSUMED]

**Required genesis fields:** codec/domain version, network ID, ephemeral bootstrapper public key, policy version, sorted unique initial peers, membership threshold, BurnConfig threshold, and the initial burn basis-points value if the implementation uses it to define when economic operations become enabled. The first seven are locked by D-02; including the default burn value is recommended to make BURN-03 restart behavior unambiguous. [VERIFIED: D-02; ASSUMED for initial burn value inclusion]

**Why not default protobuf serialization:** protobuf is already in the repository, but this phase needs a byte-level identity stable across builds; an explicit small codec avoids relying on serializer ordering behavior and makes golden-vector tests straightforward. [VERIFIED: Protobuf is in the stack; ASSUMED recommendation]

**Example:**

```cpp
// Source: recommended Phase 13 pattern derived from D-02/D-04/D-08.
std::vector<uint8_t> CanonicalGenesisBytes( const GenesisManifest &m )
{
    CanonicalWriter w;
    w.Bytes( "SGNS_TRUST_GENESIS_V1" );
    w.U16BE( m.network_id );
    w.BytesWithU32Length( HexToFixed64( m.bootstrapper_public_key ) );
    w.U64BE( m.policy.version );
    w.U32BE( static_cast<uint32_t>( m.policy.peers.size() ) );
    for ( const auto &peer : SortedUniqueValidatedPeers( m.policy.peers ) )
        w.BytesWithU32Length( HexToFixed64( peer ) );
    w.U64BE( m.policy.membership_threshold );
    w.U64BE( m.policy.burn_threshold );
    w.U64BE( m.initial_burn_basis_points );
    return std::move( w ).Take();
}
```

Do not reuse the current newline-delimited peer list or decimal-only BurnConfig bytes as the successor-policy signing format; neither includes version, predecessor, network, or policy authorization context. [VERIFIED: `TrustedPeerRegistry.cpp`, `BurnConfig.cpp`; ASSUMED conclusion]

### Pattern 2: Signed Candidate Approval Record

**What:** Add a candidate-oriented SecureCrdt API without removing the legacy `ProposeValue`/`AddSignature` API in the same task. Each accepted CRDT element is independently authenticated and carries the exact candidate core bytes it approves. [ASSUMED]

```text
trusted-policy/candidate/v2/<64-hex-content-hash>/approval/<128-hex-signer>
burn-config/candidate/v4/<64-hex-content-hash>/approval/<128-hex-signer>
```

```cpp
struct CandidateCore {
    uint16_t network_id;
    CandidateKind kind;                 // trust-policy or burn-config
    uint64_t version;
    Hash256 expected_previous_hash;
    Hash256 authorizing_policy_hash;
    std::vector<uint8_t> payload;
};

struct CandidateApprovalRecord {
    std::vector<uint8_t> canonical_candidate_bytes;
    std::vector<uint8_t> signature;      // signer is also bound by the key path
};
```

For a trust-policy successor, `expected_previous_hash` and `authorizing_policy_hash` both identify the currently confirmed policy. For BurnConfig, `expected_previous_hash` identifies the current confirmed burn state and `authorizing_policy_hash` identifies the current trust policy; a policy transition makes an unfinished burn candidate stale rather than letting a different signer set finish it. [ASSUMED]

The local and remote gates must perform the same sequence: bounded decode; strict key parsing; content-hash/key match; network/type/version/previous-state checks; resolve the authorizing current policy; signer membership check; canonical signature verification; then persist. The current code checks cryptographic validity but does not reject a valid signature from a non-member until quorum evaluation, so membership must move into both candidate write gates. [VERIFIED: `SecureCrdt::AddSignature`, `FilterSecureCrdtUpdate`, `ReadIfQuorum`; ASSUMED new sequence]

### Pattern 3: Versioned Trust Policy

**What:** Replace the independent mutable TPR threshold and BurnConfig threshold members with one confirmed `QuorumPolicyState` that includes the full trusted-peer set and both thresholds. A membership or threshold proposal is a successor for the whole policy, so activation is atomic. [ASSUMED]

```cpp
struct QuorumPolicyState {
    uint64_t version;
    Hash256 previous_hash;
    std::vector<std::string> peers;      // sorted, non-empty, unique, valid
    uint64_t membership_threshold;
    uint64_t burn_threshold;
};

uint64_t MembershipFloor( uint64_t m ) { return m / 2 + 1; }
uint64_t BurnFloor( uint64_t m ) { return m - m / 3; } // ceil(2m/3), no 2*m overflow
```

Validate `1 <= threshold <= M` before floor checks. The existing `ceil(0.51*M)` helper is not the locked formula and accepts zero-of-zero and thresholds above `M`; replace it with policy-specific validation and regression vectors for `M = 1, 2, 3, 4, 100, 101`. [VERIFIED: D-06/D-07; `QuorumThresholdValidation.hpp`; milestone audit]

### Pattern 4: Persist Before Publishing

**What:** Serialize all activation attempts through one node-scoped mutex and treat the durable head as the compare-and-swap source of truth. [ASSUMED]

```cpp
outcome::result<Activation> PolicyEngine::TryActivate( const CandidateId &id )
{
    std::lock_guard lock( transition_mutex_ );
    BOOST_OUTCOME_TRY( auto persisted, store_->LoadAndVerify() );
    BOOST_OUTCOME_TRY( auto candidate, candidates_->ReadValidated( id ) );

    if ( candidate.version != persisted.policy.version + 1 ||
         candidate.expected_previous_hash != persisted.policy_hash ||
         candidate.authorizing_policy_hash != persisted.policy_hash )
        return Activation::Stale;

    BOOST_OUTCOME_TRY( VerifyQuorumUnder( candidate, persisted.policy ) );
    BOOST_OUTCOME_TRY( store_->CommitSuccessor( persisted, candidate ) ); // sync=true
    PublishCacheFrom( store_->LoadAndVerify().value() );
    return Activation::Activated;
}
```

If two candidates reach quorum concurrently, only the one that commits while the persisted predecessor still matches can win. A crash after the commit but before cache publication is recovered by reloading; publishing before durable commit would permit a restart rollback and is forbidden. [ASSUMED]

Persist enough evidence to revalidate the head independently of mutable JSON: canonical genesis bytes/fingerprint and bootstrap signature, the policy transition chain (or an equivalent verifiable checkpoint chain), the current burn head and its quorum proof, and monotonic version/hash heads. [ASSUMED]

### Pattern 5: Restricted Startup State Machine

**What:** Separate network readiness from policy/economic readiness. [VERIFIED: D-13]

| Startup case | Authority | Allowed | Forbidden |
|--------------|-----------|---------|-----------|
| Fresh, no local trust state | Reviewed genesis manifest only for matching/filtering bootstrap genesis | Network, CRDT sync, genesis observation/confirmation | Policy approvals, configured peers as quorum, BurnConfig-dependent transactions. [VERIFIED: D-01, D-13] |
| Fresh, valid genesis arrives | Bootstrapper signature over exact canonical manifest/core | Durable genesis confirmation, then deterministic initial BurnConfig bootstrap | Any non-genesis auto-sign. [VERIFIED: D-03, D-11; ASSUMED sequencing] |
| Restart, valid persisted state | Persisted verified policy/burn heads | Normal sync, approvals, economic operations | JSON override of peers/bootstrapper/thresholds. [VERIFIED: D-14] |
| Restart, JSON trust conflict | Persisted state | Continue with critical diagnostic | Re-bootstrap or policy replacement from JSON. [VERIFIED: D-14] |
| Restart, network-ID mismatch | None | Diagnostic only | Startup. [VERIFIED: D-14] |
| Restart, old/fork CRDT data | Persisted last-known-good | Retain current operation; alert | Cache/state rollback or erasure. [VERIFIED: D-15] |

The current `TrustedPeerRegistry::New` violates the fresh-node row because it caches configured peers immediately, and `BurnConfig::TrySeedGenesisIfEligible` uses that cache. Fresh construction must instead expose no active signer set until genesis confirmation. [VERIFIED: `TrustedPeerRegistry.cpp:138-158`, `BurnConfig.cpp:198-237`]

### Pattern 6: Node-Scoped Ownership

**What:** Construct the instance-scoped registry, SecureCrdt, TrustStateStore, TrustedPeerRegistry, and BurnConfig once after the retained GlobalDB exists; do not reset them in `ShutdownAccountBoundServices(true)` during `SelectAccount()`. Destroy them only during full node/GlobalDB teardown. [VERIFIED: D-16; current `ShutdownAccountBoundServices`]

Avoid accumulating expired `BurnConfig::RefreshCallback` lambdas on every account selection. Prefer a node-scoped shared burn cache/provider that each new `TransactionManager` reads atomically, or return a removable subscription token and unregister it when the old manager stops. The current callback vector only appends weak callbacks and has no removal API. [VERIFIED: `BurnConfig::RegisterRefreshCallback`, `TransactionManager::New`; ASSUMED recommendation]

### Pattern 7: One-Shot Genesis Command Boundary

**What:** Put validation/canonicalization/sign/submit/confirm logic in a reusable `GenesisCeremony` service and keep the executable thin. Import the ephemeral key into `GeniusSigner` through the existing in-memory `EthereumKeyGenerator(std::string_view)` constructor; do not call `GeniusAccount::NewFromPrivateKey`, because that path writes key material to secure storage and the account index. [VERIFIED: `EthereumKeyGenerator.cpp:26-40`, `GeniusAccount.cpp:320-333`, `:491-540`; ASSUMED new factory]

Recommended command behavior: read the private key from a mode-restricted file or protected stdin, never argv/environment; derive and compare the bootstrapper public key; show canonical ordered peers, thresholds, network, and fingerprint; require an explicit typed confirmation; submit through candidate SecureCrdt; wait for local confirmed-state verification; cleanse in-memory buffers; unlink the key file on success; retain it on failure with a critical instruction. [ASSUMED]

Document that reliable physical secure deletion is not guaranteed on copy-on-write or wear-leveling storage; deletion plus memory cleansing reduces exposure but does not prove media erasure. [CITED: https://docs.openssl.org/3.2/man3/OPENSSL_malloc/]

### Anti-Patterns to Avoid

- **Self-authorizing policy:** Never resolve signatures from the peers/thresholds inside the proposed payload. [VERIFIED: D-08]
- **Mutable “final” CRDT marker:** The reader must re-derive quorum and compare to the durable predecessor; do not trust a replicated finalized flag. [VERIFIED: existing SecureCrdt contract; D-15]
- **Unsigned candidate base value:** It conflicts with D-10 and creates a filter ordering race. [VERIFIED: current filter behavior; ASSUMED conclusion]
- **Account-owned BurnConfig or registry:** `SelectAccount()` currently destroys and reconstructs them, losing callback registration. [VERIFIED: milestone audit; `GeniusNode.cpp`]
- **JSON fallback after confirmation:** Missing or conflicting CRDT/config data must not erase or redefine persisted last-known-good state. [VERIFIED: D-14/D-15]
- **Floating-point quorum math:** Use integer formulas and test boundary sizes. [ASSUMED]
- **Logging secrets or full approval material:** Log hashes, versions, signer addresses, and outcomes; never private keys. [VERIFIED: D-03/D-04; ASSUMED logging detail]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Signature encoding/verification | New crypto or signature format | `GeniusSigner` and `multisig::VerifyPayloadSignature` | Existing TPR/Burn signatures already use the project's double-SHA-256 secp256k1 convention. [VERIFIED: source] |
| Durable key-value transaction | Ad-hoc JSON overwrite | Existing `storage::rocksdb` synchronous batch | It already provides sync writes and atomic batch commit. [VERIFIED: source] |
| Proposal networking | RPC/pubsub protocol | Existing GlobalDB CRDT topic/filter/callback path | Locked by D-09. [VERIFIED: 13-CONTEXT.md] |
| Quorum counting | Custom loops that count duplicates/outsiders | Existing `multisig::MultiSig` after a validated policy snapshot is selected | It already deduplicates and verifies authorized signatures; policy selection remains Phase 13 logic. [VERIFIED: `src/multisig`] |
| Key/address validation | Length-only checks | `GeniusAccount::IsValidPublicKey` / `base::IsHexAddress` plus canonical lowercase normalization | Existing code exposes the address validator used by TPR. [VERIFIED: source] |
| Secret zeroization primitive | Plain `memset` | `OPENSSL_cleanse`/clear-free for owned buffers | OpenSSL documents compiler-resistant clearing. [CITED: https://docs.openssl.org/3.2/man3/OPENSSL_malloc/] |

**Key insight:** The phase should hand-build only the domain model and canonical byte layout required by the locked decisions; crypto, replicated transport, local durable transactions, and test framework already exist. [ASSUMED]

## Runtime State Inventory

| Category | Items Found | Action Required |
|----------|-------------|-----------------|
| Stored data | Existing GlobalDB keys use single-slot `trusted-peer-registry`, `burn-config`, and `sig/<address>` children; production has no caller that confirms TPR genesis, but local/test/dev stores may contain old-format values. [VERIFIED: source and milestone audit] | Do not auto-promote legacy single-slot data into confirmed trust. Introduce versioned candidate keys and a separate network-scoped local TrustStateStore; execution should inventory existing keys and treat old unconfirmed data as legacy/stale. [ASSUMED] |
| Live service config | `sgns_config.json` contains the four mutable trust fields; the example contains placeholder addresses. No external UI/database-held trust config was found in repository search. [VERIFIED: codebase grep] | On fresh boot accept reviewed manifest input; after confirmation treat the four fields as diagnostics only, emit a critical mismatch, and prefer a manifest path/fingerprint reference. [VERIFIED: D-14; ASSUMED config shape] |
| OS-registered state | No launchd/systemd/Task Scheduler/pm2 registration embedding these trust field names was found in the repository. Actual deployment hosts were not inspected. [VERIFIED: repository grep] | No repository task; deployment runbook should verify service arguments do not pass the ephemeral key. [ASSUMED] |
| Secrets/env vars | No trust/bootstrap private-key environment variable is present in repository scripts/config. Current account private keys may fall back to plaintext JSON storage on unsupported platforms, but the genesis command must not use account persistence. [VERIFIED: codebase grep and CONCERNS.md] | Add an ephemeral key-file/stdin path with no argv/env/log exposure; cleanse memory and delete only after verified confirmation. [ASSUMED] |
| Build artifacts | Existing test binaries encode the old SecureCrdt layout; no installed production genesis tool exists. [VERIFIED: build/test inventory] | Rebuild affected libraries/tests and add the one-shot executable target. No package reinstall is required. [VERIFIED: environment audit; ASSUMED target] |

## Common Pitfalls

### Pitfall 1: Candidate authentication depends on CRDT element arrival order
**What goes wrong:** A base value arrives before its signature and is retained/surfaced, or a signature arrives first and is rejected because the base does not exist. [VERIFIED: current `FilterSecureCrdtUpdate`]
**Why it happens:** Candidate and proposer approval are separate keys. [VERIFIED: current layout]
**How to avoid:** Make the atomic replicated unit a signed approval record that contains the canonical candidate bytes. [ASSUMED]
**Warning signs:** Tests need sleeps/retries to make value arrive before signature, or filters accept an unsigned candidate. [ASSUMED]

### Pitfall 2: Successor signatures are checked under the proposed policy
**What goes wrong:** A malicious candidate replaces the signer set with attacker keys and authorizes itself. [VERIFIED: D-08 threat]
**Why it happens:** The signer-set callback reads candidate payload rather than confirmed persisted state. [ASSUMED]
**How to avoid:** Resolve by `authorizing_policy_hash`, require it to equal the persisted current policy hash, and evaluate with that persisted snapshot. [ASSUMED]
**Warning signs:** Tests can confirm a membership change using only signatures from newly added peers. [ASSUMED]

### Pitfall 3: Burn candidates survive a membership transition
**What goes wrong:** Signatures gathered under an old trusted-peer set later activate after policy membership changes. [ASSUMED]
**Why it happens:** Burn candidates bind only their previous burn value, not their authorizing policy. [ASSUMED]
**How to avoid:** Include and validate `authorizing_policy_hash`; make old-policy unfinished burn candidates stale. [ASSUMED]
**Warning signs:** A burn candidate can reach quorum using a mixture of old/new policy signers. [ASSUMED]

### Pitfall 4: Cache changes before durable state
**What goes wrong:** A crash exposes a newer policy during the process lifetime but restarts from an older head. [ASSUMED]
**Why it happens:** Callback code updates atomics before a synchronous store commit. [VERIFIED: current cache-first style lacks persistence]
**How to avoid:** Under one transition mutex, commit and verify the durable snapshot before publishing caches/callbacks. [ASSUMED]
**Warning signs:** Crash-injection tests observe a version decrease. [ASSUMED]

### Pitfall 5: Threshold validation copies the old 51% helper
**What goes wrong:** Zero-of-zero, threshold greater than signer count, or the wrong floor is accepted; `ceil(0.51*M)` is stricter than `floor(M/2)+1` for some odd `M` such as 101. [VERIFIED: formula and current helper]
**Why it happens:** Phase 11's locally configured generic floor is reused after D-06/D-07 introduced exact policy-specific floors. [VERIFIED: phase history]
**How to avoid:** Validate unique peers and bounds first, then exact integer membership/burn floors. [VERIFIED: D-06/D-07]
**Warning signs:** No tests at `M=0`, `M=1`, odd/even boundaries, and `threshold=M+1`. [ASSUMED]

### Pitfall 6: Policy services remain in account teardown
**What goes wrong:** `SelectAccount()` resets policy owners, the retained GlobalDB rejects duplicate callback patterns, and live BurnConfig refresh stops. [VERIFIED: audit and source]
**Why it happens:** `ShutdownAccountBoundServices(true)` calls `ResetQuorumMembers()`. [VERIFIED: source]
**How to avoid:** Split account teardown from node policy teardown and register policy filters/callbacks once. [VERIFIED: D-16; ASSUMED method split]
**Warning signs:** object addresses/registration counts change after account selection. [ASSUMED]

### Pitfall 7: Private key enters persistent account machinery
**What goes wrong:** The supposedly ephemeral bootstrap key is saved to secure storage/account index or printed in shell history/logs. [VERIFIED: `GeniusAccount::NewFromPrivateKey` persistence; example logs private key]
**Why it happens:** Reusing the normal node account factory or passing `--private-key=<hex>`. [ASSUMED]
**How to avoid:** Use an in-memory `GeniusSigner` import and a protected key file/stdin; never use `GeniusNode::FromPrivateKey` for genesis. [ASSUMED]
**Warning signs:** New account files appear after the command or process listings contain the key. [ASSUMED]

### Pitfall 8: Tests conflate sandbox/network failure with trust logic
**What goes wrong:** Listener creation fails with “Operation not permitted,” causing null fixtures and misleading trust-test failures. [VERIFIED: research baseline run]
**Why it happens:** Existing SecureCrdt fixtures start real local GossipPubSub listeners. [VERIFIED: `securecrdt_test_node.hpp`]
**How to avoid:** Run network-backed tests with local-listener permission and keep pure codec/policy/store tests network-free. [ASSUMED]
**Warning signs:** All component tests fail during fixture construction before assertions. [VERIFIED: baseline run]

## Code Examples

### Complete policy validation

```cpp
// Source: D-04, D-06, D-07, VALID-01; recommended exact implementation shape.
outcome::result<void> ValidatePolicy( const QuorumPolicyState &p )
{
    if ( p.peers.empty() ) return PolicyError::EmptySignerSet;

    std::unordered_set<std::string> seen;
    for ( const auto &peer : p.peers ) {
        if ( !GeniusAccount::IsValidPublicKey( peer ) ) return PolicyError::MalformedSigner;
        if ( !seen.insert( peer ).second ) return PolicyError::DuplicateSigner;
    }

    const uint64_t m = p.peers.size();
    if ( p.membership_threshold < 1 || p.membership_threshold > m ||
         p.membership_threshold < m / 2 + 1 )
        return PolicyError::UnsafeMembershipThreshold;

    const uint64_t burn_floor = m - m / 3;
    if ( p.burn_threshold < 1 || p.burn_threshold > m ||
         p.burn_threshold < burn_floor )
        return PolicyError::UnsafeBurnThreshold;

    return outcome::success();
}
```

### Candidate local/remote gate

```cpp
// Source: D-08..D-12; recommended shared gate for local Put and remote filter.
outcome::result<ValidatedApproval> ValidateApproval(
    const CandidateKey &key, const CandidateApprovalRecord &record,
    const ConfirmedTrustSnapshot &confirmed )
{
    BOOST_OUTCOME_TRY( auto core, CanonicalTrustCodec::DecodeCandidate( record.canonical_candidate_bytes ) );
    if ( Sha256( record.canonical_candidate_bytes ) != key.content_hash )
        return CandidateError::HashMismatch;
    if ( core.version != key.version || core.network_id != confirmed.network_id )
        return CandidateError::ContextMismatch;
    BOOST_OUTCOME_TRY( auto authorizer, confirmed.PolicyByHash( core.authorizing_policy_hash ) );
    if ( authorizer.hash != confirmed.current_policy_hash )
        return CandidateError::StaleAuthorizer;
    if ( !Contains( authorizer.peers, key.signer ) )
        return CandidateError::UnauthorizedSigner;
    if ( !multisig::VerifyPayloadSignature(
             key.signer, record.signature, record.canonical_candidate_bytes ) )
        return CandidateError::InvalidSignature;
    return ValidatedApproval{ core, key.signer, record.signature };
}
```

### Node/account lifetime split

```cpp
// Source: D-16; recommended ownership split.
outcome::result<void> GeniusNode::SelectAccount( std::string_view address )
{
    BOOST_OUTCOME_TRY( ShutdownAccountBoundServicesOnly() );
    BOOST_OUTCOME_TRY( SwitchAccount( address ) );
    // secure_crdt_, trust_state_store_, trusted_peer_registry_, burn_config_
    // and their GlobalDB callbacks remain unchanged.
    return StartAccountBoundServices( burn_config_->SharedConfirmedCache() );
}
```

## State of the Art

| Old Approach | Current Phase 13 Approach | When Changed | Impact |
|--------------|---------------------------|--------------|--------|
| One mutable candidate at a base key | Content-addressed version/hash candidate approvals | Phase 13 | Concurrent candidates coexist and signatures cannot be mixed across content. [VERIFIED: D-12; ASSUMED implementation] |
| Structurally valid values retained from arbitrary peers | First retained element is authenticated by a current trusted peer | Phase 13 | Closes the audited unauthorized candidate ingress. [VERIFIED: D-10] |
| `ceil(0.51*M)` generic threshold | Exact strict-majority and two-thirds floors plus bounds | Phase 13 | Implements D-06/D-07 and eliminates zero/oversized policies. [VERIFIED: D-06/D-07] |
| JSON peers/bootstrapper/thresholds remain live | JSON trusted only before first confirmation; durable signed state after | Phase 13 | Config drift cannot silently redefine a restarted node. [VERIFIED: D-01/D-14] |
| Account-scoped TPR/BurnConfig construction | Node/GlobalDB-scoped policy services | Phase 13 | Account selection no longer loses policy callbacks/cache. [VERIFIED: D-16] |

**Deprecated/outdated:**
- `SecureCrdt::ProposeValue(base_key)` for TPR/BurnConfig production mutations: retain only for compatibility/tests until migrated, but Phase 13 governance must use content-addressed candidate APIs. [ASSUMED]
- `TrustedPeerRegistry` pre-confirmation configured-peer cache as a signer source: forbidden by D-13. [VERIFIED: D-13]
- `BurnConfig` constructor auto-seeding from an unconfirmed TPR cache: move behind confirmed genesis and restrict it to the deterministic initial value only. [VERIFIED: current code and D-13; ASSUMED fix]
- Mutable JSON quorum thresholds after confirmation: diagnostic copies only. [VERIFIED: D-05/D-14]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | A self-contained signed approval record is the best candidate CRDT unit. | Architecture Patterns | A different atomic multi-element GlobalDB design may satisfy D-10, but must prove ordering-independent filter behavior. |
| A2 | Use an explicit map-free binary codec rather than protobuf serialization for identity bytes. | Canonical Genesis Manifest | A project-approved canonical serializer may already exist outside searched paths; planner should reuse it if it provides byte-stability guarantees. |
| A3 | Use a dedicated synchronous RocksDB trust store under the network-scoped node path. | Persist Before Publishing | The project may prefer a different protected node-local database, but it must be separate from mutable config and crash-safe. |
| A4 | Bind BurnConfig candidates to both prior burn hash and authorizing policy hash. | Signed Candidate Approval Record | Omitting the policy hash risks old/new signer-set mixing across membership transitions. |
| A5 | Include initial burn basis points in canonical genesis identity and gate economics on a confirmed initial BurnConfig state. | Genesis Manifest / Startup | D-02 names quorum-policy values but not the burn value; the planner must reconcile this with BURN-03 and D-13. |
| A6 | The local operator surface should be the existing terminal/local command style. | Genesis/approval UI | Another authenticated local-only mechanism is allowed by discretion. |
| A7 | Suggested candidate byte/count caps require product confirmation. | Security / pitfalls | No project-wide maximum trusted-peer count is specified; picking a cap without confirmation could reject a legitimate future policy. |
| A8 | OS permissions protect the node-local TrustStateStore from wholesale replacement. | Security Domain | Without a TPM/off-host monotonic anchor, an attacker able to restore the complete local disk to an older valid snapshot can defeat a purely local high-water mark. |

## Open Questions

1. **What exactly makes the initial BurnConfig state “confirmed”?**
   - What we know: BURN-03 requires 100 basis points by default; D-13 blocks BurnConfig-dependent operations until genesis is confirmed; Phase 11 allowed genesis-only automatic behavior. [VERIFIED: requirements and prior context]
   - What's unclear: D-03 explicitly names submission of the TPR genesis record, not a BurnConfig genesis record. [VERIFIED: D-03]
   - Recommendation: Include initial burn value 100 in the genesis manifest, then allow trusted-peer nodes to auto-publish only that version-1 candidate after TPR genesis is durable; enable economic operations only after its burn quorum confirms. Never auto-sign later versions. [ASSUMED]

2. **What resource bounds apply to manifests/candidates?**
   - What we know: D-04 requires structural rejection, and unbounded peer/candidate bytes are a CRDT DoS risk. [VERIFIED: D-04; ASSUMED risk]
   - What's unclear: No maximum trusted-peer count or candidate size is specified. [VERIFIED: context/project search]
   - Recommendation: Decide and document `MAX_TRUSTED_PEERS`, `MAX_CANDIDATE_BYTES`, and maximum active candidates per predecessor before implementation; add exact boundary tests. [ASSUMED]

3. **How strong must local rollback protection be against a host administrator?**
   - What we know: Signed chains prevent forged successors and the local high-water mark rejects older CRDT data while intact. [ASSUMED]
   - What's unclear: A host-level attacker who can replace the entire trust database with an older valid snapshot cannot be detected by local state alone. [ASSUMED]
   - Recommendation: Treat wholesale disk rollback as outside v1.1 unless the user requires a TPM/OS-keystore/off-host anchor; document the boundary in the operator runbook. [ASSUMED]

4. **Where should the one-shot tool obtain networking configuration?**
   - What we know: `GlobalDB` construction requires pubsub, scheduler, graphsync, and database-path setup; an existing example demonstrates that composition. [VERIFIED: `globaldb_app.cpp`, `securecrdt_test_node.hpp`]
   - What's unclear: There is no production reusable factory for a minimal SecureCrdt client. [VERIFIED: codebase grep]
   - Recommendation: Extract/reuse a narrow node network/GlobalDB composition helper rather than instantiate a full `GeniusNode` with the ephemeral key. [ASSUMED]

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|-------------|-----------|---------|----------|
| CMake | Build/test orchestration | ✓ | 3.31.4 | Project minimum from codebase map is 3.22+. [VERIFIED: environment probe and STACK.md] |
| CTest | Focused validation | ✓ | 3.31.4 | Direct test binaries. [VERIFIED: environment probe] |
| Ninja | Optional build executor | ✓ | 1.13.0 | Current Release tree uses Unix Makefiles. [VERIFIED: environment probe/CMakeCache] |
| Apple Clang | C++17 build | ✓ | 16.0.0 | GCC/MSVC are project-supported targets but not probed here. [VERIFIED: environment probe; STACK.md] |
| OpenSSL CLI/library | Hashing/zeroization support | ✓ | 3.6.2 | Existing project hash implementation for fingerprinting; cleanse still needs linked OpenSSL API. [VERIFIED: environment probe/codebase] |
| Existing Release test tree | Baseline tests | ✓ | `build/OSX/Release` | Reconfigure if new targets are not discovered. [VERIFIED: filesystem/CTest probe] |
| Local listener permission | Network-backed fixtures | ✓ outside sandbox; ✗ inside restricted sandbox | — | Run pure unit tests in sandbox and network E2E with approved local listener access. [VERIFIED: baseline runs] |

**Missing dependencies with no fallback:** none identified. [VERIFIED: environment probe]

**Missing dependencies with fallback:** restricted sandbox cannot open fixture listeners; approved unrestricted local execution passed the existing focused baseline. [VERIFIED: test runs]

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | GoogleTest through project `addtest`; CTest 3.31.4. [VERIFIED: CMake/test probe] |
| Config file | `test/src/CMakeLists.txt` plus subsystem `CMakeLists.txt`. [VERIFIED: codebase] |
| Quick run command | `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt_candidate|genesis_manifest|quorum_policy|trust_state_store|trustedpeerregistry|burnconfig'` [ASSUMED target names] |
| Full phase command | `ctest --test-dir build/OSX/Release --output-on-failure -R 'securecrdt|trustedpeer|burnconfig|account_management|node_startup|startup|multi_account'` [VERIFIED existing names; ASSUMED new coverage] |
| Baseline | Existing focused suite excluding `multi_account_test`: 11/11 passed in 91.45 seconds when local listeners were permitted. [VERIFIED: research run] |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| D-02, D-04, BOOT-02 | Peer order canonicalizes; duplicate/empty/malformed rejected; golden fingerprint stable; network/bootstrapper/threshold/version tamper changes fingerprint | unit | `.../genesis_manifest_test` | ❌ Wave 0 |
| D-03, BOOT-01, BOOT-03 | Tool derives matching bootstrapper, requires confirmation, submits via SecureCrdt, verifies, deletes key only on success, never persists/logs it | integration | `.../trust_genesis_tool_test` | ❌ Wave 0 |
| D-05..D-08, POLICY-01, VALID-01 | Exact threshold bounds/floors, successor version/hash, current-policy authorization, no self-authorization | unit | `.../quorum_policy_test` | ❌ Wave 0 |
| D-09..D-12 | Authorized proposal retained; outsider rejected; approvals bind exact candidate; concurrent candidates coexist; mixed signatures fail | integration | `.../securecrdt_candidate_test` | ❌ Wave 0 |
| D-12 | First of two quorum candidates wins atomically; loser becomes permanently stale | concurrency integration | `.../securecrdt_candidate_race_test` | ❌ Wave 0 |
| D-14, D-15, BOOT-04 | Snapshot persists/reloads; corrupt/older/fork state rejected; failed commit does not advance cache; valid restart ignores JSON conflict | unit + startup integration | `.../trust_state_store_test`; `.../trust_restart_test` | ❌ Wave 0 |
| D-13 | Fresh node networks/syncs but has empty active policy and economic operations fail closed until confirmation | startup E2E | `.../trust_first_boot_e2e_test` | ❌ Wave 0 |
| D-10, TEST-01 | Altered peers, bootstrapper replacement, lowered/oversized thresholds, inconsistent manifests are rejected/isolated | multi-node E2E | `.../trust_tamper_e2e_test` | ❌ Wave 0 |
| D-11 | Receiving a candidate surfaces it but never changes approval count until local operator action | integration | `.../operator_approval_test` | ❌ Wave 0 |
| BURN-01..03, TEST-01 | Confirmed live burn update reaches `PayEscrow`; pre-genesis burn/economic path blocked; stale policy candidate cannot update burn | account integration | `.../burnconfig_policy_e2e_test` | ❌ Wave 0 |
| D-16, BURN-02 | `SelectAccount()` preserves exact policy objects/registration count; new TransactionManager consumes live burn cache; update after switch still affects `PayEscrow` | multiaccount E2E | `.../policy_lifetime_multi_account_test` | ❌ Wave 0 |

### Required Scenario Matrix

| Scenario | Expected assertion |
|----------|--------------------|
| Successful first boot | TPR genesis candidate verifies under reviewed bootstrapper, durable fingerprint/head written, active policy becomes available. [VERIFIED: D-01..D-04] |
| Restart | Same persisted fingerprint/version/hashes load without needing mutable trust fields. [VERIFIED: D-14] |
| Altered JSON peer list/bootstrapper/threshold | Critical diagnostic; persisted state remains active; network ID mismatch alone fails startup. [VERIFIED: D-14] |
| Manifest/candidate byte tamper | Hash/key or signature validation rejects before retention/callback. [VERIFIED: D-10; ASSUMED mechanism] |
| Rollback/missing CRDT head | Operational alert; last-known-good cache/store remains unchanged. [VERIFIED: D-15] |
| Fork/non-descendant | Candidate cannot activate even with signatures if predecessor/authorizing policy hash differs. [VERIFIED: D-08/D-15] |
| Concurrent candidates | Both are visible; exactly one durable successor; loser stays stale after restart. [VERIFIED: D-12] |
| Operator approval | Candidate reception never signs; explicit local proposal contributes exactly one approval; repeat approval deduplicates. [VERIFIED: D-11] |
| Live BurnConfig | Confirmed burn successor updates the node-scoped cache and changes actual `PayEscrow` burn output. [VERIFIED: BURN-01/BURN-02; audit gap] |
| SelectAccount | Policy object/callback identities remain; account-bound manager changes; post-switch burn update is observed. [VERIFIED: D-16] |

### Sampling Rate

- **Per task commit:** build affected target(s), then run the smallest listed unit/integration binary; pure codec/policy/store tests should finish under 30 seconds. [ASSUMED]
- **Per wave merge:** run the focused phase regex with local listener permission. [ASSUMED]
- **Phase gate:** run the full phase regex, repeat the account-switch/policy-lifetime E2E at least 5 times, then run the repository's broader relevant suite; no HIGH security finding may remain. [VERIFIED: security config; ASSUMED repetition]

### Wave 0 Gaps

- [ ] `test/src/trustedpeer/genesis_manifest_test.cpp` — canonical vectors and validation.
- [ ] `test/src/trustedpeer/quorum_policy_test.cpp` — exact formula/bounds/version authorization.
- [ ] `test/src/trustedpeer/trust_state_store_test.cpp` — durable load/commit/corruption/crash injection.
- [ ] `test/src/securecrdt/securecrdt_candidate_test.cpp` — candidate auth/addressing/quorum.
- [ ] `test/src/securecrdt/securecrdt_candidate_race_test.cpp` — simultaneous quorum winner.
- [ ] `test/src/trustedpeer/trust_genesis_tool_test.cpp` — one-shot key lifecycle and submit/confirm.
- [ ] `test/src/startup/trust_first_boot_e2e_test.cpp` and `trust_restart_test.cpp` — boot/restart gates.
- [ ] `test/src/startup/trust_tamper_e2e_test.cpp` — altered config/manifest/rollback/fork.
- [ ] `test/src/trustedpeer/operator_approval_test.cpp` — explicit approval only.
- [ ] `test/src/account/burnconfig_policy_e2e_test.cpp` — actual PayEscrow effect.
- [ ] `test/src/multiaccount/policy_lifetime_multi_account_test.cpp` — node-scoped callbacks/cache.
- [ ] Add each target to the existing subsystem `CMakeLists.txt`; no framework install is required. [VERIFIED: test infrastructure]

## Security Domain

Security enforcement is enabled at ASVS L1 and blocks on HIGH severity. [VERIFIED: `.planning/config.json`]

### Trust Boundaries and Assets

| Boundary / Asset | Threat | Required control |
|------------------|--------|------------------|
| Reviewed genesis input → one-shot tool | Tampered peer/policy/bootstrapper input | Canonical validation, displayed fingerprint and ordered list, explicit confirmation, derived-key match. [VERIFIED: D-01..D-04] |
| Ephemeral private key → signer memory | Disclosure via argv/log/storage/core dump | Protected file/stdin, no account persistence, no secret logging, best-effort cleanse/delete, failure-safe retention instructions. [VERIFIED: D-03; ASSUMED controls] |
| Remote CRDT peer → local datastore/filter | Unauthorized proposal, malformed bytes, resource exhaustion | Bounded strict decode, content hash match, current-peer authorization, signature verification before retention. [VERIFIED: D-10; ASSUMED bounds] |
| Candidate approvals → activation | Replay, mix-and-match, self-authorization, fork | Domain/network/type/version/prev/auth-policy binding and exact-byte signatures. [VERIFIED: D-08..D-12] |
| Local mutable config → restart | Trust-root replacement | Persisted signed state wins; conflicts log critical; network mismatch fails. [VERIFIED: D-14] |
| CRDT state → persisted head | Rollback/fork/race | Transition mutex, durable compare-and-swap, monotonic version and predecessor check, persist-before-publish. [VERIFIED: D-15; ASSUMED mechanism] |
| Account selection → node policy services | Callback loss/use-after-free | Node-scoped ownership, weak callbacks or removable subscriptions, full teardown only with GlobalDB. [VERIFIED: D-16] |

### Concrete STRIDE Threat Model for PLAN.md

| ID | Component / Boundary | STRIDE | Threat | Mitigation | Verification |
|----|----------------------|--------|--------|------------|--------------|
| T13-01 | Genesis manifest | Spoofing/Tampering | Attacker replaces peers/bootstrapper/thresholds before first boot | Canonical fingerprint shown for operator review; bootstrapper public key derived from private key; genesis signature exact-byte verification. [VERIFIED: D-01..D-04] | Golden/tamper/inconsistent-manifest tests. |
| T13-02 | Ephemeral key handling | Information Disclosure | Key leaks through command line, logs, account storage, or residual memory/file | File/stdin only; in-memory signer; forbid `GeniusAccount::NewFromPrivateKey`; cleanse buffers; unlink after confirmed success; no key logging. [ASSUMED] | Process args/log/storage scan; success/failure deletion tests. |
| T13-03 | Remote candidate ingress | Spoofing/DoS | Arbitrary peer injects structurally valid candidate | Signed approval record; signer must be in current persisted policy before local/remote Put acceptance; bounded decode. [VERIFIED: D-10; ASSUMED record] | Outsider and malformed/oversize rejection tests. |
| T13-04 | Approval storage | Tampering | Signature from candidate A is counted for candidate B | Content hash in path; canonical bytes in each approval record; signature verifies those exact bytes; strict path/record match. [VERIFIED: D-11/D-12] | Cross-candidate signature swap test. |
| T13-05 | Policy transition | Elevation of Privilege | Proposed signer set authorizes itself or lowers threshold | Current persisted policy resolves signer set/threshold; exact D-06/D-07 floors and bounds; successor hash/version linkage. [VERIFIED: D-06..D-08] | Self-authorization/lowered/oversized threshold tests. |
| T13-06 | Concurrent activation | Tampering/Repudiation | Two candidate callbacks both become effective | One transition mutex and durable predecessor CAS; structured audit logs include type/version/hash/signers/outcome. [ASSUMED] | Barrier-driven two-candidate race and restart test. |
| T13-07 | Restart/sync | Tampering | Older/missing/fork CRDT data replaces local head | Persisted LKG remains authoritative; reject non-descendant; alert without clearing cache/store. [VERIFIED: D-15] | Rollback/missing/fork tests. |
| T13-08 | Mutable config | Tampering | Post-genesis JSON silently changes trust | Ignore conflicting trust fields after load of valid persisted state; critical diagnostic; fail only on network mismatch. [VERIFIED: D-14] | Restart with each altered field. |
| T13-09 | Burn candidate across policy change | Elevation/Tampering | Old signers finish an economic change after membership changes | Bind burn candidate to authorizing policy hash; stale it when current policy changes. [ASSUMED] | Old/new signer mix test. |
| T13-10 | Account switch | Denial of Service | Callback remains bound to destroyed BurnConfig and replacement registration fails | Do not destroy policy services on `SelectAccount`; shared cache/removable consumer subscription. [VERIFIED: audit/D-16] | Repeated account switch + live update test. |
| T13-11 | Local trust store | Tampering/Rollback | State corruption or partial write advances/erases head | Synchronous RocksDB batch, internal hashes/proofs, load-and-verify, never erase LKG on candidate error. [VERIFIED: RocksDB behavior; ASSUMED store] | Fault injection/corruption tests. |
| T13-12 | Trusted proposer | Denial of Service | A compromised trusted peer floods valid signed candidates | Strict byte/peer bounds, current-predecessor inbox indexing, stale isolation, operator-visible rate/volume alerts. Exact caps require decision. [ASSUMED] | Boundary/flood test after caps are chosen. |

### Applicable ASVS Categories

OWASP ASVS is web-focused, but the configured gate requires its control categories to be considered; ASVS itself describes use as a security-control verification baseline. [CITED: https://owasp.org/www-project-application-security-verification-standard/]

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | yes (machine/operator signatures, not web login) | Current-peer public-key membership plus exact-byte `GeniusSigner` signature verification. [VERIFIED: phase model] |
| V3 Session Management | no | No remote administrative session is introduced. [VERIFIED: locked boundary] |
| V4 Access Control | yes | Current confirmed policy authorizes propose/approve; local operator action is required. [VERIFIED: D-08/D-10/D-11] |
| V5 Input Validation | yes | Canonical bounded parser, peer/key/version/hash/threshold validation, reject before retention. [VERIFIED: D-04/VALID-01; ASSUMED bounds] |
| V6 Cryptography | yes | Existing SHA-256/secp256k1/multisig primitives only; no new cryptography. [VERIFIED: source] |
| V7 Error Handling and Logging | yes | Critical diagnostics for config mismatch/rollback/fork; no private-key logging. [VERIFIED: D-14/D-15; ASSUMED log fields] |
| V8/V14 Data Protection | yes | Ephemeral key protection/retention rules and node-state filesystem permissions. [CITED: https://github.com/OWASP/ASVS/blob/master/5.0/en/0x23-V14-Data-Protection.md] |
| V12 Files and Resources | yes | Key/manifest path canonicalization, regular-file/no-symlink policy, ownership/mode checks. [ASSUMED] |

### Known Threat Patterns for This Stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Regex/key-path confusion | Tampering | Parse hierarchical segments exactly; do not derive signer from an unchecked “last segment.” [VERIFIED: current `LastKeySegment` behavior; ASSUMED mitigation] |
| Static registry cross-node collision | Tampering/DoS | Instance-scoped registry owned by one SecureCrdt/GlobalDB. [VERIFIED: audit] |
| Weak callback expiration | DoS | Node-scoped owner or explicit unregister token; assert registration success. [VERIFIED: callback API/audit] |
| Integer threshold edge cases | Elevation/DoS | Bound signer count, check `1..M`, use overflow-safe integer formulas. [VERIFIED: D-06/D-07] |
| Secret in process arguments | Information Disclosure | File descriptor/stdin or restricted file; never argv/URI. ASVS data-protection guidance likewise treats URLs/query strings as inappropriate for secrets. [CITED: https://github.com/OWASP/ASVS/blob/master/5.0/en/0x23-V14-Data-Protection.md] |

## Package Legitimacy Audit

Not applicable: Phase 13 should install no external packages. It extends existing repository targets and already-linked dependencies. [VERIFIED: codebase dependency audit]

## Project Constraints (from AGENTS.md)

No `AGENTS.md`, `.codex/skills/`, or `.agents/skills/` exists in this workspace, so there are no additional project-local directives or skills to copy into the plan. [VERIFIED: filesystem probe]

## Sources

### Primary (HIGH confidence)
- `.planning/phases/13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production/13-CONTEXT.md` — all locked D-01..D-16 decisions, discretion, and scope. [VERIFIED]
- `.planning/v1.1-MILESTONE-AUDIT.md` — production genesis, ingress, callback-lifetime, and mutable-config gap evidence. [VERIFIED]
- `.planning/todos/pending/2026-08-10-secure-trusted-peer-genesis-configuration.md` — folded BOOT/POLICY/VALID/TEST requirements. [VERIFIED]
- `src/securecrdt/SecureCrdt.hpp/.cpp`, `SecureCrdtRegistry.hpp`, `QuorumThresholdValidation.hpp` — current key layout, filter, quorum, and global registration behavior. [VERIFIED]
- `src/trustedpeer/TrustedPeerRegistry.hpp/.cpp` — current pre-confirmation cache, bootstrap signer, and confirm path. [VERIFIED]
- `src/account/BurnConfig.hpp/.cpp`, `GeniusNode.hpp/.cpp`, `TransactionManager.cpp` — current auto-seed, callback, startup, teardown, account switch, and consumer lifetime. [VERIFIED]
- `src/storage/rocksdb/rocksdb.cpp`, `rocksdb_batch.cpp` — synchronous local writes and atomic batch support. [VERIFIED]
- `src/account/GeniusSigner.cpp`, `GeniusAccount.cpp`, `ProofSystem/EthereumKeyGenerator.cpp` — signing/import/persistence boundary. [VERIFIED]
- https://docs.openssl.org/3.2/man3/OPENSSL_malloc/ — cleanse semantics and storage-erasure limitations. [CITED]
- https://pubs.opengroup.org/onlinepubs/9799919799/functions/rename.html — atomic rename behavior considered for the persistence alternative. [CITED]
- https://owasp.org/www-project-application-security-verification-standard/ — ASVS purpose/current reference format. [CITED]

### Secondary (MEDIUM confidence)
- `.planning/codebase/STACK.md`, `ARCHITECTURE.md`, `CONCERNS.md` — project stack/convention context; these maps predate the Phase 8-13 code and were cross-checked against current source. [VERIFIED]
- Existing Release CTest inventory and focused baseline execution on 2026-08-11. [VERIFIED]

### Tertiary (LOW confidence)
- None. Unverified design choices are explicitly tagged `[ASSUMED]` and listed in the Assumptions Log.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new dependency; current source and environment were inspected.
- Existing architecture/gaps: HIGH — direct source and milestone audit agree.
- Recommended candidate encoding/layout: MEDIUM — satisfies the locked invariants, but it is a new Phase 13 design requiring plan review.
- Persistence approach: MEDIUM-HIGH — existing synchronous RocksDB behavior is verified; the new state schema is not implemented.
- Pitfalls/threat model: HIGH for identified current gaps; MEDIUM for resource caps and host-level rollback boundary.

**Research date:** 2026-08-11
**Valid until:** 2026-09-10 (30 days; codebase-specific design)
