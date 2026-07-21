# Phase 8: MultiSig Primitive - Research

**Researched:** 2026-07-21
**Domain:** C++ crypto/consensus primitive (signing-bytes, signature verification, N-of-M quorum)
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** The primitive operates on raw, already-serialized bytes (opaque `std::vector<uint8_t>`/`std::string` payload), not a typed envelope like `ConsensusAuth`'s `ConsensusSubject`. Callers own their own payload serialization/codec and hand the primitive already-canonical bytes to sign/verify.
- **D-02:** Signers are identified by account address string, consistent with `GeniusAccount::Sign`/`GeniusAccount::VerifySignature` (`src/account/GeniusAccount.hpp:207,216`). No separate raw-public-key identity path for this phase.
- **D-03:** Quorum evaluation is a pure, stateless function: `EvaluateQuorum(signer_set, threshold, collected_signatures) -> bool` (or equivalent result type), not a stateful accumulator object. Callers re-invoke it each time with the current signature set read from CRDT.
- **D-04:** Quorum evaluation deduplicates by signer identity (keep at most one valid signature per signer), silently skips any signature that fails verification (does not reject the whole batch), and counts quorum against the remaining valid-unique signer count.

### Claude's Discretion
- Exact C++ types/signatures for the primitive's public API (function names, whether quorum threshold is expressed as a raw count or a fraction, error/result type shape) — following existing codebase conventions (`snake_case_`, `std::shared_ptr` factory pattern where applicable, Doxygen `@param` docs).
- Where in the source tree the new component lives (e.g. `src/multisig/` vs `src/blockchain/` vs `src/crdt/`).

### Deferred Ideas (OUT OF SCOPE)
- Raw-public-key signer identity (not just account-address string) — may be needed in Phase 10 if `TrustedPeerRegistry` genesis seeding needs to bootstrap peers before they have registered `GeniusAccount` addresses. Revisit during Phase 10 discussion if it becomes a blocker.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| MSIG-01 | Component computes canonical signing-bytes for an arbitrary payload and verifies signatures against it, reusing `ConsensusAuth`'s SHA-256/`VerifySignature` primitives | See "Signing-Bytes Builder" pattern below — `sha2_256` + `GeniusAccount::VerifySignature` are directly reusable; `ProposalSigningBytes`-style proto helpers are NOT reusable (proto-typed), a new raw-bytes wrapper is needed |
| MSIG-02 | N-of-M quorum evaluation given a signer set and threshold, no hardcoded N | See "Quorum Evaluation" pattern below, modeled on `ValidatorRegistry::IsQuorum`/`QuorumThreshold` shape but simplified to unweighted signer-count semantics per D-04 |
| MSIG-03 | Usable independently of CRDT (importable/testable without a running node) | Header-only or small static-lib pattern (`ConsensusAuth.hpp` precedent) with zero CRDT/`GlobalDB`/network includes; test precedent in `test/src/blockchain/` shows pure-static-helper tests requiring no node (`validator_registry_slot_quorum_test`, `consensus_vote_slot_test`) |
</phase_requirements>

## Summary

The new MultiSig primitive is a small, dependency-light C++ component reusing exactly two existing building blocks: `sgns::crypto::sha2_256` (`src/crypto/hasher.hpp:19`) for canonical hashing and `sgns::GeniusAccount::VerifySignature` (`src/account/GeniusAccount.hpp:207`, static) for ECDSA verification against an address string. `ConsensusAuth.hpp`'s existing signing-bytes builders (`ProposalSigningBytes`, `VoteSigningBytes`, `VoteBundleSigningBytes`) are NOT directly reusable because each hard-codes a specific protobuf message type and clears its `signature` field before serializing — they are not generic over raw bytes. Per D-01 the new primitive receives already-canonical raw bytes directly from the caller, so it does not need a "signing bytes builder" at all in the proto sense; it only needs a thin function that (a) optionally SHA-256-hashes the payload for a canonical id/digest, and (b) delegates verification/signing to `GeniusAccount`. `GeniusAccount::Sign` is a non-static instance method (`src/account/GeniusAccount.hpp:216`), so a full sign-then-verify round trip in this standalone primitive requires either injecting a signer callback (matching the existing `UTXOManager` DI pattern at `GeniusAccount.cpp:766-772`) or only exposing verification (leaving signing to callers holding a `GeniusAccount`). Given MSIG's node-independence requirement, expose sign as an optional injected callback, not a hard dependency on `GeniusAccount`.

For quorum, `ValidatorRegistry`'s `IsQuorum`/`QuorumThreshold`/`WeightConfig` (`src/blockchain/ValidatorRegistry.hpp:68-167`) is the closest architectural precedent — it already separates "compute threshold from total" from "check accumulated against threshold" as two small pure functions, and has parallel static-only test variants (`EvaluateSlotQuorumStatic`) for CRDT/node-free unit testing. The new primitive should mirror this shape but simplify to unweighted per-signer counting: N-of-M is a raw threshold count against `signer_set.size()`, not a weighted sum. This keeps Phase 12's later migration path clean since `ValidatorRegistry` can eventually delegate its now-unweighted-quorum-shaped checks to this primitive while keeping its own weight-summing logic separate.

**Primary recommendation:** Create a new standalone `src/multisig/` directory holding a header-only (or header + small `.cpp`) `MultiSig` component with two free functions/static methods: `VerifyPayloadSignature(address, signature, payload) -> bool` (thin wrapper delegating to `GeniusAccount::VerifySignature`, no proto involvement) and `EvaluateQuorum(signer_set, threshold, collected_signatures) -> QuorumResult` (dedup-by-signer, skip-invalid-silently, per D-04). Link only against `sgns_genius_account` (for `GeniusAccount::VerifySignature`) — no `crdt_globaldb`, no `ipfs-pubsub` link needed at this layer. Add a matching `test/src/multisig/` CTest target following the `validator_registry_slot_quorum_test` no-node pattern.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Canonical signing-bytes for arbitrary payload | MultiSig primitive (new, `src/multisig/`) | — | Pure function of caller-supplied bytes; no proto/CRDT knowledge needed per D-01 |
| Signature verification | MultiSig primitive (thin wrapper) | Account/crypto (`GeniusAccount`, `secp256k1`) | Actual ECDSA math lives in `GeniusAccount::VerifySignature`; MultiSig only forwards |
| N-of-M quorum evaluation | MultiSig primitive (new) | — | Pure stateless function per D-03; no CRDT/network/DB dependency |
| Signature collection / storage over time | Out of scope (Phase 9, SecureCRDT) | CRDT `GlobalDB` | D-03 explicitly pushes state management to the CRDT-backed caller |
| Signing (private-key operation) | Account tier (`GeniusAccount::Sign`) | MultiSig (optional injected callback for tests) | `Sign` requires a live keypair instance; MultiSig stays node-independent by accepting signing as an optional callback, not a hard dependency |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `sgns_genius_account` (internal) | current tree | `GeniusAccount::VerifySignature` (static) / `GeniusAccount::Sign` | Already the project's canonical secp256k1 signer/verifier — MSIG-01 explicitly requires reuse `[VERIFIED: src/account/GeniusAccount.hpp:207,216; impl GeniusAccount.cpp:790,845]` |
| `hasher` (internal, `src/crypto/hasher.hpp`) | current tree | `sgns::crypto::sha2_256(gsl::span<const uint8_t>)` | Canonical SHA-256 primitive already used by `ConsensusAuth::ComputeProposalId` `[VERIFIED: src/crypto/hasher.hpp:19,21-24]` |
| C++17 / GTest / CTest | project-standard | Unit testing | Matches every other `test/src/blockchain/*_test.cpp` target `[VERIFIED: test/src/blockchain/CMakeLists.txt]` |

No new external (third-party) packages are required for this phase — everything needed already exists in-tree. **Package Legitimacy Audit is not applicable** (no new external packages installed).

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Reusing `GeniusAccount::VerifySignature` | Hand-rolled secp256k1 verify call | Rejected — would duplicate the exact byte-order quirks (`GeniusAccount.cpp:824-826` reverses each 32-byte scalar) and the double-SHA256 (`GeniusAccount.cpp:829-833`) signing convention; MSIG-01 requires reuse anyway |
| Weighted quorum (`ValidatorRegistry`-style `WeightConfig`) | Unweighted signer-count quorum | D-04/CONTEXT.md scope this phase to plain N-of-M signer counting; weighting is a `ValidatorRegistry`-specific concern deferred to Phase 12 |

## Architecture Patterns

### System Architecture Diagram

```
Caller (Phase 9 ISignedCRDTData impl, or unit test)
        │
        │  payload: std::vector<uint8_t>  (already-canonical bytes, D-01)
        ▼
┌───────────────────────────────────────────────┐
│  MultiSig primitive (src/multisig/, new)       │
│                                                  │
│  VerifyPayloadSignature(address, sig, payload)  │──► GeniusAccount::VerifySignature (static)
│         │                                       │        │
│         ▼                                       │        ▼
│  bool valid                                     │   secp256k1_ecdsa_verify + double-SHA256
│                                                  │
│  EvaluateQuorum(signer_set, threshold,          │
│                 collected_signatures)           │
│         │                                       │
│         ├─ dedup by signer address (D-04)       │
│         ├─ verify each sig via                  │
│         │  VerifyPayloadSignature, skip invalid │
│         │  silently (D-04)                      │
│         └─ count valid-unique >= threshold?     │
│                     │                            │
│                     ▼                            │
│              QuorumResult{ has_quorum, count }   │
└───────────────────────────────────────────────┘
        │
        ▼
Caller decides: apply CRDT write / reject / accumulate more signatures (state stays in caller, D-03)
```

No CRDT, no `GlobalDB`, no network/pubsub appears anywhere in this diagram — that boundary is the phase's core success criterion (MSIG-03).

### Recommended Project Structure
```
src/multisig/
├── CMakeLists.txt        # new target, e.g. add_library(multisig ...) or header-only INTERFACE lib
├── MultiSig.hpp          # public API: VerifyPayloadSignature, EvaluateQuorum, QuorumResult, SignerSet/ThresholdSpec types
└── MultiSig.cpp          # only if non-trivial logic needs out-of-line definitions (dedup loop, etc.) — otherwise header-only like ConsensusAuth.hpp

test/src/multisig/
├── CMakeLists.txt        # addtest(multisig_verify_test ...), addtest(multisig_quorum_test ...)
├── multisig_verify_test.cpp
└── multisig_quorum_test.cpp
```

Precedent for this exact shape: `src/crypto/` (small focused internal libs: `hasher`, `keccak`, `sha`, `twox`, each its own subdirectory + `CMakeLists.txt`) shows the project's established pattern for small, dependency-minimal internal components living in their own top-level `src/<name>/` directory rather than being folded into `src/blockchain/` or `src/account/`. `src/multisig/` following this exact pattern is consistent with codebase conventions and keeps MSIG-03's node-independence explicit at the directory-boundary level.

### Pattern 1: Thin signature-verification wrapper (not proto-typed signing-bytes builder)
**What:** A free function taking `(address, signature, payload)` and delegating straight to `GeniusAccount::VerifySignature`, with no proto message type in the signature.
**When to use:** Whenever the caller already has canonical bytes (per D-01) — this is the *only* mode this phase supports.
**Example:**
```cpp
// Source: pattern derived from src/blockchain/ConsensusAuth.hpp ValidateProposal
// (src/blockchain/ConsensusAuth.hpp:118-141), generalized to raw bytes per D-01.
// GeniusAccount::VerifySignature signature verified at src/account/GeniusAccount.hpp:207-209:
//   static bool VerifySignature(const std::string &address, std::string_view sig, const std::vector<uint8_t> &data);
inline bool VerifyPayloadSignature( const std::string          &address,
                                     std::string_view            signature,
                                     const std::vector<uint8_t> &payload )
{
    return sgns::GeniusAccount::VerifySignature( address, signature, payload );
}
```
Note: `GeniusAccount::VerifySignature` already performs the double-SHA256 + secp256k1 verify internally (`GeniusAccount.cpp:824-838`) — no separate hashing step is needed before calling it. If the primitive also needs a caller-facing "canonical digest" (e.g. for logging/dedup keys, not for the signature check itself), use `sgns::crypto::sha2_256` directly on the payload, matching `ConsensusAuth::ComputeProposalId`'s pattern (`ConsensusAuth.hpp:104-111`).

### Pattern 2: Pure static quorum evaluator, mirroring `ValidatorRegistry`'s split of Threshold vs. IsQuorum
**What:** Two-step shape — compute/accept a threshold, then check accumulated count against it — but unweighted (raw count) not weight-summed.
**When to use:** MSIG-02's N-of-M evaluation.
**Example:**
```cpp
// Source: pattern derived from src/blockchain/ValidatorRegistry.hpp:160-167
//   uint64_t QuorumThreshold(uint64_t total_weight) const;
//   bool IsQuorum(uint64_t accumulated_weight, uint64_t total_weight) const;
// Simplified to unweighted signer-count semantics for MultiSig (D-03, D-04).
struct QuorumResult
{
    bool     has_quorum         = false;
    uint64_t valid_unique_count = 0;
};

// signer_set: authorized signer addresses (M). threshold: required count (N, no hardcoded N).
// collected_signatures: pairs of (address, signature) gathered so far by the caller.
QuorumResult EvaluateQuorum( const std::vector<std::string>                                &signer_set,
                             uint64_t                                                       threshold,
                             const std::vector<std::pair<std::string, std::string>>        &collected_signatures,
                             const std::vector<uint8_t>                                    &payload )
{
    std::unordered_set<std::string> signer_lookup( signer_set.begin(), signer_set.end() );
    std::unordered_set<std::string> valid_unique_signers;

    for ( const auto &[address, signature] : collected_signatures )
    {
        if ( valid_unique_signers.count( address ) )
        {
            continue; // D-04: dedup by signer identity
        }
        if ( !signer_lookup.count( address ) )
        {
            continue; // not an authorized signer for this set
        }
        if ( !VerifyPayloadSignature( address, signature, payload ) )
        {
            continue; // D-04: skip invalid signatures silently, do not reject batch
        }
        valid_unique_signers.insert( address );
    }

    return QuorumResult{ valid_unique_signers.size() >= threshold, valid_unique_signers.size() };
}
```
This mirrors `ValidatorRegistry::EvaluateSlotQuorumStatic`'s established "pure static, no GlobalDB/node instance needed" test-ability shape (`ValidatorRegistry.hpp:186-203`), just with count-based rather than weight-based arithmetic.

### Anti-Patterns to Avoid
- **Reusing `ProposalSigningBytes`/`VoteSigningBytes` for arbitrary payloads:** These are hard-typed to specific protobuf messages (`ConsensusProposal`, `ConsensusVote`, `ConsensusVoteBundle`) and clear a `signature` field before serializing — there is no generic "clear signature field" concept for raw `std::vector<uint8_t>` payloads. Do not attempt to template/generalize these; write a new, independent raw-bytes function instead.
- **Making `EvaluateQuorum` a class holding accumulated state:** D-03 explicitly requires a pure stateless function. A stateful accumulator class would violate MSIG-03's no-CRDT-dependency intent by tempting future callers to store signature state inside the primitive itself instead of in CRDT (Phase 9's responsibility).
- **Hard-linking to `crdt_globaldb`, `ipfs-pubsub`, or `genius_node`/`genius_node_test`:** Any of these transitively pulls in node/network machinery, violating MSIG-03. Link only against `sgns_genius_account` (for `GeniusAccount::VerifySignature`) — this library's own `CMakeLists.txt` (`src/account/CMakeLists.txt`) does link `crdt_globaldb` and `ipfs-pubsub` itself, but that is an existing project-wide dependency shape for `GeniusAccount`, not something the new MultiSig library needs to reference directly beyond calling the static method — `[ASSUMED — verify at implementation time whether including `GeniusAccount.hpp` transitively drags in headers that slow build/test link times; if so, consider whether `GeniusAccount::VerifySignature` could be exposed via a smaller-surface forward-declared interface]`.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| ECDSA signature verification | Custom secp256k1 verify call | `GeniusAccount::VerifySignature` (`GeniusAccount.hpp:207`) | Already handles the project's specific byte-order reversal and double-SHA256 signing convention (`GeniusAccount.cpp:824-838`) — reimplementing risks subtle incompatibility with existing signed data |
| SHA-256 hashing | `<openssl/sha.h>` or another hash lib | `sgns::crypto::sha2_256` (`src/crypto/hasher.hpp:19`) | Already the project's canonical SHA-256 entrypoint, used identically by `ConsensusAuth::ComputeProposalId` |
| Address/public-key parsing | Custom hex-decode + secp256k1 pubkey parse | None needed at MultiSig layer — `GeniusAccount::VerifySignature` already does this internally | Keeps MultiSig a thin, dependency-light wrapper; parsing logic stays encapsulated in `GeniusAccount` |

**Key insight:** This phase is explicitly a "reuse, don't reimplement" phase — MSIG-01 names the exact primitives (`ConsensusAuth`'s SHA-256/`VerifySignature`) to reuse. The only genuinely new code is (a) a thin raw-bytes wrapper around `GeniusAccount::VerifySignature`, and (b) the unweighted quorum-evaluation loop — everything else is composition of existing, already-tested primitives.

## Common Pitfalls

### Pitfall 1: Assuming `ConsensusAuth.hpp`'s builders generalize to raw bytes
**What goes wrong:** Attempting to template `ProposalSigningBytes` or reuse its "clear signature field" pattern for a `std::vector<uint8_t>` payload.
**Why it happens:** The builders look generic (same shape: copy, clear signature, serialize) but the "clear signature field" step is a protobuf-specific operation with no meaning for opaque bytes.
**How to avoid:** Per D-01, treat the payload as already-canonical — no signing-bytes construction step is needed at all in MultiSig; verification calls `GeniusAccount::VerifySignature(address, sig, payload)` directly on the caller-supplied bytes.
**Warning signs:** Any code path in the new component that imports `Consensus.pb.h` or references `ConsensusProposal`/`ConsensusVote` types — those types have no place in a payload-agnostic MultiSig primitive.

### Pitfall 2: Treating `GeniusAccount::Sign` as available without an account instance
**What goes wrong:** Designing the public API around `Sign(payload) -> signature` as if it were a free function.
**Why it happens:** `VerifySignature` is `static` (no instance needed) but `Sign` (`GeniusAccount.hpp:216`) is a non-static member requiring a constructed `GeniusAccount` with a live keypair — asymmetric API surface is easy to miss.
**How to avoid:** Keep MultiSig's public surface to verification + quorum only (both stateless, both node-independent). If a "helper to produce test signatures" is needed for unit tests, accept a signing callback (`std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>`) injected by the test, matching the existing DI pattern at `GeniusAccount.cpp:766-772` (`UTXOManager` constructor takes a `Sign` lambda).
**Warning signs:** Unit tests instantiating a full `GeniusAccount` (which requires an `EthereumKeyGenerator`/keypair and possibly secure storage) just to get a signature for a quorum test — this creates an unnecessary node-adjacent dependency in tests that are supposed to prove node-independence (MSIG-03).

### Pitfall 3: Silent-skip-invalid (D-04) implemented as "skip and continue" without dedup-first ordering
**What goes wrong:** Verifying every signature before deduping, or deduping by (address, signature) pair instead of by address alone — this could let a signer submit multiple *valid* signatures of different byte content (if such exist) and inflate the unique count, or could waste verification cycles on already-satisfied signers.
**Why it happens:** D-04 combines two rules (dedup by identity, skip-invalid) that must compose in a specific order to be both correct and efficient.
**How to avoid:** Check dedup-by-address *before* running verification (skip re-verifying an address once it already has a valid signature counted) — see Pattern 2's loop order (`if already counted -> continue` runs before the signature check).
**Warning signs:** Test cases where the same signer appears twice in `collected_signatures` (once valid, once with a tampered/garbage signature) — the result must still count that signer exactly once and must not flip to false due to the garbage entry.

## Code Examples

### Verified API signatures (exact, from source)
```cpp
// src/account/GeniusAccount.hpp:207-209
static bool VerifySignature( const std::string          &address,
                             std::string_view            sig,
                             const std::vector<uint8_t> &data );

// src/account/GeniusAccount.hpp:216
std::vector<uint8_t> Sign( const std::vector<uint8_t> &data ) const;   // NON-static, requires instance

// src/crypto/hasher.hpp:19,21-24
[[nodiscard]] base::Hash256 sha2_256( gsl::span<const uint8_t> buffer );
[[nodiscard]] inline base::Hash256 sha2_256( const void *data, size_t size );

// src/blockchain/ValidatorRegistry.hpp:160,167 — quorum shape precedent
uint64_t QuorumThreshold( uint64_t total_weight ) const;
bool     IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const;
```
Source: `[VERIFIED: read directly from src/account/GeniusAccount.hpp, src/account/GeniusAccount.cpp, src/crypto/hasher.hpp, src/blockchain/ValidatorRegistry.hpp on 2026-07-21]`

### Signature-size / address-format constraints to respect
```cpp
// src/account/GeniusAccount.hpp:376
static constexpr size_t SIGNATURE_EXP_SIZE = 64; ///< Expected size of the signature in bytes

// GeniusAccount.cpp:790-804: address is hex-decoded to an uncompressed secp256k1 pubkey;
// signature bytes are two 32-byte scalars stored least-significant-byte-first (project convention,
// reversed internally vs. libsecp256k1's big-endian expectation).
```
MultiSig's `EvaluateQuorum`/`VerifyPayloadSignature` do not need to duplicate this logic — just forward the caller's `signature` (as `std::string`/`std::string_view`, matching `VerifySignature`'s parameter type) and `address` unchanged. Tests constructing signatures manually (e.g. malformed-signature test cases) must respect `SIGNATURE_EXP_SIZE == 64` or `VerifySignature` will short-circuit to `false` at the size check (`GeniusAccount.cpp:791-797`) — useful for the "tampered signature rejected" success criterion.

## Package Legitimacy Audit

Not applicable — this phase introduces no new external/third-party packages. All dependencies (`sgns_genius_account`, `hasher`) are existing in-tree internal libraries.

## Environment Availability

Not applicable — no external tools/services beyond the existing CMake/GTest/CTest build toolchain already used throughout the repo (verified present via existing `test/src/blockchain/CMakeLists.txt` and `cmake/functions.cmake`'s `addtest`/`addtest_part` macros, which this phase's tests will reuse directly, no new tooling needed).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GTest + GMock, driven via CTest (`add_test` inside `addtest()`, `cmake/functions.cmake:8-25`) |
| Config file | No dedicated per-test config; target registration lives in each directory's `CMakeLists.txt` (e.g. `test/src/blockchain/CMakeLists.txt`) |
| Quick run command | `ctest -R multisig` (once targets are named `multisig_*_test`) |
| Full suite command | `ctest` (from build dir) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| MSIG-01 | Valid signature over payload verifies true; tampered payload/signature verifies false | unit | `ctest -R multisig_verify_test` | ❌ Wave 0 — new file |
| MSIG-02 | N-of-M quorum boundary cases (N, N-1, all M) report correct has_quorum | unit | `ctest -R multisig_quorum_test` | ❌ Wave 0 — new file |
| MSIG-03 | Both test targets build/link without `crdt_globaldb`/`genius_node`/`genius_node_test`/pubsub — verified structurally by CMakeLists.txt link list, not a runtime assertion | build/link check | `cmake --build . --target multisig_verify_test multisig_quorum_test` (inspect link deps) | ❌ Wave 0 — new file |

### Sampling Rate
- **Per task commit:** `ctest -R multisig`
- **Per wave merge:** `ctest` (full suite)
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `src/multisig/CMakeLists.txt`, `src/multisig/MultiSig.hpp` (+ optional `.cpp`) — new library target
- [ ] `test/src/multisig/CMakeLists.txt`, `test/src/multisig/multisig_verify_test.cpp`, `test/src/multisig/multisig_quorum_test.cpp` — new test targets, following the `addtest(...)` + `target_link_libraries(... sgns_genius_account)` pattern from `test/src/blockchain/CMakeLists.txt`'s no-node targets (e.g. `validator_registry_slot_quorum_test`, `consensus_vote_slot_test`)
- [ ] `src/CMakeLists.txt` needs `add_subdirectory(multisig)` inserted — `[VERIFIED: src/CMakeLists.txt:1-16, currently lists account/base/blockchain/outcome/crypto/storage/crdt/subscription/migration/processing/local_secure_storage/singleton/watcher/coinprices/proof]`
- [ ] `test/src/CMakeLists.txt` needs `add_subdirectory(multisig)` inserted — `[VERIFIED: test/src/CMakeLists.txt:1,8 list add_subdirectory(account) and add_subdirectory(blockchain)]`

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Including `GeniusAccount.hpp` from a new lightweight `src/multisig/` header will not pull in heavy transitive includes that slow builds or accidentally reintroduce node/CRDT dependencies | Anti-Patterns to Avoid | Low — worst case is a build-time nuisance; MSIG-03's node-independence is about link/runtime dependencies (CRDT/network), not header weight. Verify with a minimal compile-only smoke test in Wave 0 |
| (removed — resolved) | `src/CMakeLists.txt` and `test/src/CMakeLists.txt` insertion points were located and verified directly; no longer an open assumption | — | — |

## Open Questions

1. **Should `EvaluateQuorum` take `threshold` as a raw count (uint64_t N) or should it also support a fraction/percentage form?**
   - What we know: CONTEXT.md D-03/MSIG-02 says "no hardcoded N" — i.e., N is a runtime parameter, not that it must support fractional/percentage thresholds.
   - What's unclear: Whether Phase 12's future `ValidatorRegistry` migration will need percentage-based thresholds (it currently uses `quorum_numerator`/`quorum_denominator` weighted fractions) exposed at this primitive's layer.
   - Recommendation: Keep MSIG's `EvaluateQuorum` to a raw integer threshold (`uint64_t threshold` against `signer_set.size()` as M) for this phase — matches D-03/D-04's plain N-of-M framing exactly and avoids scope creep. Defer any fraction-based API to Phase 12 when `ValidatorRegistry`'s actual migration needs become concrete.

2. **(Resolved)** `src/multisig/` is registered by adding `add_subdirectory(multisig)` to `src/CMakeLists.txt` (alongside lines 1-16); the test directory is registered by adding `add_subdirectory(multisig)` to `test/src/CMakeLists.txt` (alongside lines 1,8).

## Sources

### Primary (HIGH confidence)
- `src/blockchain/ConsensusAuth.hpp` (full file read) — signing-bytes builder pattern, `ComputeProposalId`, `ValidateProposal`
- `src/account/GeniusAccount.hpp:190-230,376` — `VerifySignature`/`Sign` declarations, `SIGNATURE_EXP_SIZE`
- `src/account/GeniusAccount.cpp:766-880` — `VerifySignature`/`Sign` implementations, byte-order/double-SHA256 convention, DI callback pattern (`UTXOManager` constructor)
- `src/blockchain/ValidatorRegistry.hpp:60-270` — `WeightConfig`, `QuorumThreshold`, `IsQuorum`, `EvaluateSlotQuorum`/`EvaluateSlotQuorumStatic`, `ComputeUpdateSigningBytes`/`VerifyUpdate` declarations
- `src/crypto/hasher.hpp:19-24` — `sha2_256` signatures
- `src/blockchain/CMakeLists.txt`, `src/blockchain/impl/CMakeLists.txt`, `src/account/CMakeLists.txt`, `src/crypto/CMakeLists.txt` — build target patterns for small internal libs
- `test/src/blockchain/CMakeLists.txt` — CTest target conventions, no-node pure-static-helper test precedent (`validator_registry_slot_quorum_test`, `consensus_vote_slot_test`, `validator_registry_promotion_test`)
- `cmake/functions.cmake:8-38` — `addtest`/`addtest_part` macro definitions
- `.planning/phases/08-multisig-primitive/08-CONTEXT.md`, `.planning/REQUIREMENTS.md`, `.planning/STATE.md` — locked decisions, requirement IDs, project history

### Secondary (MEDIUM confidence)
- None used — all findings verified directly against source files in this repository.

### Tertiary (LOW confidence)
- None.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all APIs read directly from source, exact line numbers cited
- Architecture: HIGH — directly modeled on two existing, in-tree precedents (`ConsensusAuth.hpp`, `ValidatorRegistry`)
- Pitfalls: HIGH — derived from direct code reading of asymmetric static/instance API surface and D-04's ordering requirements, not speculation

**Research date:** 2026-07-21
**Valid until:** Stable — no external/third-party dependency, so no natural staleness window; re-verify only if `GeniusAccount`/`ConsensusAuth`/`ValidatorRegistry` signatures change before planning executes.
