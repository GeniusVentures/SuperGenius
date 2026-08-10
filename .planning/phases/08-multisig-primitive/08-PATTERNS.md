# Phase 8: MultiSig Primitive - Pattern Map

**Mapped:** 2026-07-21
**Files analyzed:** 7 (new: 5, modified: 2)
**Analogs found:** 7 / 7

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|----------------|
| `src/multisig/MultiSig.hpp` | utility (crypto primitive) | request-response (verify) + batch (quorum) | `src/blockchain/ConsensusAuth.hpp` | exact (header-only signing/verify wrapper pattern) |
| `src/multisig/MultiSig.cpp` | utility | batch (dedup/verify loop) | `src/blockchain/ValidatorRegistry.hpp` (`EvaluateSlotQuorumStatic`, `.cpp` impl) | role-match (pure static quorum evaluator) |
| `src/multisig/CMakeLists.txt` | config | — | `src/crypto/CMakeLists.txt` | exact (small internal lib, minimal deps) |
| `src/CMakeLists.txt` (edit) | config | — | itself, existing `add_subdirectory(...)` list | exact |
| `test/src/multisig/CMakeLists.txt` | config | — | `test/src/blockchain/CMakeLists.txt` (no-node `addtest(...)` blocks) | exact |
| `test/src/multisig/multisig_verify_test.cpp` | test | request-response | `test/src/blockchain/validator_registry_slot_quorum_test.cpp` | role-match (pure-static-helper GTest, no node) |
| `test/src/multisig/multisig_quorum_test.cpp` | test | batch | `test/src/blockchain/validator_registry_slot_quorum_test.cpp` | exact (same static-quorum-tally test shape) |
| `test/src/CMakeLists.txt` (edit) | config | — | itself, existing `add_subdirectory(...)` list | exact |

## Pattern Assignments

### `src/multisig/MultiSig.hpp` (utility, header-only)

**Analog:** `src/blockchain/ConsensusAuth.hpp` (full file, 147 lines — read in full, small file)

**File header / include-guard pattern** (lines 1-8):
```cpp
/**
 * @file       ConsensusAuth.hpp
 * @brief      Header-only helpers for consensus signing and validation.
 * @date       2026-02-07
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_CONSENSUS_AUTH_HPP
#define SGNS_CONSENSUS_AUTH_HPP
```
For MultiSig, use `SGNS_MULTISIG_MULTISIG_HPP` (or `SGNS_MULTISIG_HPP`) guard, same Doxygen header block style, `sgns` namespace (or `sgns::multisig` — Claude's Discretion per CONTEXT.md; either is consistent, but note `ConsensusAuth` uses bare `sgns` while `crypto` helpers use `sgns::crypto` — prefer `sgns::multisig` to mirror `sgns::crypto`'s nested-namespace convention for a new focused component).

**Imports pattern** (lines 10-18) — note only 2 of these 5 includes are relevant for MultiSig (no proto, no outcome needed per D-01/D-04 which return plain `bool`/struct, not `outcome::result`):
```cpp
#include <system_error>
#include <vector>

#include "account/GeniusAccount.hpp"
#include "base/hexutil.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"   // DO NOT copy — proto-typed, not needed (D-01)
#include "crypto/hasher.hpp"
#include <gsl/span>
#include "outcome/outcome.hpp"                     // only needed if MultiSig exposes result<> variants
```
MultiSig should include only `<vector>`, `<string>`, `<string_view>`, `<unordered_set>`, `"account/GeniusAccount.hpp"`, and optionally `"crypto/hasher.hpp"` if a canonical-digest helper is added (Pattern 1 note, RESEARCH.md lines 136).

**Verify wrapper pattern — direct precedent** (`ConsensusAuth.hpp` lines 118-143, `ValidateProposal`):
```cpp
inline bool ValidateProposal( const ConsensusProposal &proposal )
{
    if ( proposal.proposer_id().empty() || proposal.signature().empty() || proposal.proposal_id().empty() )
    {
        return false;
    }

    auto signing_bytes = ProposalSigningBytes( proposal );
    if ( signing_bytes.has_error() )
    {
        return false;
    }

    if ( !GeniusAccount::VerifySignature( proposal.proposer_id(), proposal.signature(), signing_bytes.value() ) )
    {
        return false;
    }
    ...
}
```
Generalize to raw bytes (per D-01, no signing-bytes builder needed — payload IS the signing bytes already):
```cpp
inline bool VerifyPayloadSignature( const std::string          &address,
                                     std::string_view            signature,
                                     const std::vector<uint8_t> &payload )
{
    return sgns::GeniusAccount::VerifySignature( address, signature, payload );
}
```

**Exact `GeniusAccount::VerifySignature` signature to call** (`src/account/GeniusAccount.hpp:207-209`):
```cpp
static bool VerifySignature( const std::string          &address,
                             std::string_view            sig,
                             const std::vector<uint8_t> &data );
```
`GeniusAccount::Sign` is a non-static instance method (`src/account/GeniusAccount.hpp:216`: `std::vector<uint8_t> Sign( const std::vector<uint8_t> &data ) const;`) — do NOT expose a free `Sign()` in MultiSig; only wrap verification (per Pitfall 2 in RESEARCH.md).

**Signature-size constraint** (`src/account/GeniusAccount.hpp:376`):
```cpp
static constexpr size_t SIGNATURE_EXP_SIZE = 64; ///< Expected size of the signature in bytes
```
Tests constructing malformed signatures must respect this or `VerifySignature` short-circuits to `false`.

---

### `src/multisig/MultiSig.cpp` / quorum evaluator (utility, batch)

**Analog:** `src/blockchain/ValidatorRegistry.hpp` lines 68-103 (`WeightConfig`), 160-203 (`QuorumThreshold`, `IsQuorum`, `EvaluateSlotQuorum`, `EvaluateSlotQuorumStatic` declarations)

**Two-step threshold/check split pattern** (lines 160-167):
```cpp
/**
 * @brief Computes minimum accumulated weight required for quorum.
 */
uint64_t QuorumThreshold( uint64_t total_weight ) const;
/**
 * @brief Checks whether accumulated weight satisfies quorum.
 */
bool IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const;
```

**Pure-static, non-instance quorum tally precedent** (lines 190-203, doc comment + declaration):
```cpp
/**
 * @brief Pure (stateless) slot-quorum tally for deterministic unit testing.
 *
 * Identical arithmetic to EvaluateSlotQuorum, but takes the WeightConfig
 * explicitly so it can be exercised without a GlobalDB-backed
 * ValidatorRegistry instance. The member function delegates here.
 */
static SlotQuorumResult EvaluateSlotQuorumStatic( const std::vector<sgns::ConsensusVote> &votes,
                                                  const Registry                        &registry,
                                                  const WeightConfig                    &weight_config );
```
MultiSig's `EvaluateQuorum` should mirror this exact "pure static function, all inputs passed explicitly, no hidden state" shape but simplified to unweighted signer-count semantics (see RESEARCH.md Pattern 2 for the full recommended implementation, already vetted against D-03/D-04):
```cpp
struct QuorumResult
{
    bool     has_quorum         = false;
    uint64_t valid_unique_count = 0;
};

QuorumResult EvaluateQuorum( const std::vector<std::string>                          &signer_set,
                             uint64_t                                                 threshold,
                             const std::vector<std::pair<std::string, std::string>>  &collected_signatures,
                             const std::vector<uint8_t>                              &payload )
{
    std::unordered_set<std::string> signer_lookup( signer_set.begin(), signer_set.end() );
    std::unordered_set<std::string> valid_unique_signers;

    for ( const auto &[address, signature] : collected_signatures )
    {
        if ( valid_unique_signers.count( address ) )       { continue; } // D-04 dedup-first
        if ( !signer_lookup.count( address ) )             { continue; } // not authorized
        if ( !VerifyPayloadSignature( address, signature, payload ) ) { continue; } // D-04 skip-invalid
        valid_unique_signers.insert( address );
    }

    return QuorumResult{ valid_unique_signers.size() >= threshold, valid_unique_signers.size() };
}
```
**Critical ordering rule (Pitfall 3, RESEARCH.md lines 214-218):** dedup-by-address check MUST run before signature verification in the loop, not after — this is what makes "same signer appears twice (once valid, once garbage)" resolve correctly to "counted once, quorum unaffected."

---

### `src/multisig/CMakeLists.txt` (config)

**Analog:** `src/crypto/CMakeLists.txt` (full file, 17 lines)
```cmake
add_library(hasher
    hasher.cpp
)
target_link_libraries(hasher
    PRIVATE
    ipfs-lite-cpp::blake2
    twox
    buffer
    sha
    keccak
)
supergenius_install(hasher)

add_subdirectory(keccak)
add_subdirectory(sha)
add_subdirectory(twox)
```
Apply as (only link `sgns_genius_account`, per MSIG-03/anti-pattern — no `crdt_globaldb`, no `ipfs-pubsub`, no `genius_node*`):
```cmake
add_library(multisig
    MultiSig.cpp
)
target_link_libraries(multisig
    PUBLIC
    sgns_genius_account
)
supergenius_install(multisig)
```
If header-only (no non-trivial out-of-line logic needed — `EvaluateQuorum`'s loop is trivial enough to stay `inline` in the `.hpp`), an `INTERFACE` library is also acceptable, mirroring `ConsensusAuth.hpp`'s header-only shape (it has no `.cpp` / no CMake target of its own, just consumed via `#include`). **Recommendation:** since `MultiSig.hpp` needs `GeniusAccount.hpp` (which pulls a heavy dependency graph), prefer a real `STATIC`/`add_library` target with a `.cpp` translation unit so `sgns_genius_account`'s transitive includes are not re-parsed by every consumer of the header — matches `hasher`'s shape, not `ConsensusAuth.hpp`'s (which is already `#include`d only from within `src/blockchain/`, a context that already pays that include cost).

---

### `src/CMakeLists.txt` (edit)

**Current content** (lines 1-16):
```cmake
add_subdirectory(api/transport)
add_subdirectory(account)
add_subdirectory(base)
add_subdirectory(blockchain)
add_subdirectory(outcome)
add_subdirectory(crypto)
add_subdirectory(storage)
add_subdirectory(crdt)
add_subdirectory(subscription)
add_subdirectory(migration)
add_subdirectory(processing)
add_subdirectory(local_secure_storage)
add_subdirectory(singleton)
add_subdirectory(watcher)
add_subdirectory(coinprices)
add_subdirectory(proof)
```
**Change:** insert `add_subdirectory(multisig)` — alphabetically after `migration` or simply appended before `proof`; no strict ordering convention observed in this file, so append at the end of the list (before the `install(...)` block) or insert near `crypto` (its closest sibling by dependency-weight). Either position is safe; append at end for minimal diff noise.

---

### `test/src/multisig/CMakeLists.txt` (config)

**Analog:** `test/src/blockchain/CMakeLists.txt` — no-node `addtest(...)` blocks, specifically the `validator_registry_slot_quorum_test` and `consensus_vote_slot_test` targets (lines 23-40):
```cmake
# Phase 6 (D-01): proto-only slot-hash field tests. Does not require a fully
# wired ConsensusManager — only the regenerated Consensus.pb.h and
# VoteSigningBytes from ConsensusAuth.hpp.
addtest(consensus_vote_slot_test
    consensus_vote_slot_test.cpp
)
target_link_libraries(consensus_vote_slot_test
    blockchain_genesis
)

# Phase 6 (D-06): slot-quorum arithmetic unit tests. Exercises the pure
# static EvaluateSlotQuorumStatic helper directly -- no GlobalDB wiring needed.
addtest(validator_registry_slot_quorum_test
    validator_registry_slot_quorum_test.cpp
)
target_link_libraries(validator_registry_slot_quorum_test
    blockchain_genesis
)
```
Apply as (link only `multisig`, which itself only links `sgns_genius_account` — no `blockchain_genesis`/`genius_node_test` needed since MultiSig has zero blockchain-proto dependency):
```cmake
addtest(multisig_verify_test
    multisig_verify_test.cpp
)
target_link_libraries(multisig_verify_test
    multisig
)

addtest(multisig_quorum_test
    multisig_quorum_test.cpp
)
target_link_libraries(multisig_quorum_test
    multisig
)
```
`addtest(...)` macro itself (`cmake/functions.cmake:8-27`) already wires `GTest::gtest_main`/`gmock_main`, xunit output, and `add_test()` registration — no need to repeat that in the new CMakeLists.txt.

---

### `test/src/multisig/multisig_verify_test.cpp` / `multisig_quorum_test.cpp` (test)

**Analog:** `test/src/blockchain/validator_registry_slot_quorum_test.cpp` (lines 1-60 read)

**Includes + namespace-scoped helpers pattern** (lines 1-16):
```cpp
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"

namespace
{
    using sgns::ConsensusVote;
    using sgns::validator::ValidatorEntry;
    using sgns::ValidatorRegistry;
    ...
```
For MultiSig tests, replace proto includes with `"multisig/MultiSig.hpp"` only — no proto dependency at all (MSIG-03 requirement, structurally verified by the absence of any `.pb.h`/`crdt`/`genius_node` include or link):
```cpp
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "multisig/MultiSig.hpp"

namespace
{
    using sgns::multisig::VerifyPayloadSignature;   // adjust to actual chosen namespace
    using sgns::multisig::EvaluateQuorum;
    using sgns::multisig::QuorumResult;
    ...
}
```
**Test-data builder helper pattern** (lines 19-32, `MakeRegistry`) — mirror this shape for a `MakeCollectedSignatures(...)` test helper that builds `std::vector<std::pair<std::string,std::string>>` fixtures, and (per Pitfall 2) use a **signing callback** rather than a full `GeniusAccount` instance to produce test signatures — inject `std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>`, matching the DI pattern at `src/account/GeniusAccount.cpp:766-772` (`UTXOManager` constructor takes a `Sign` lambda) — do NOT instantiate `GeniusAccount` in these tests (it requires an `EthereumKeyGenerator`/keypair, which would reintroduce a node-adjacent dependency MSIG-03 explicitly forbids).

---

## Shared Patterns

### Header-only vs. thin-.cpp small internal library shape
**Source:** `src/blockchain/ConsensusAuth.hpp` (header-only, `inline` free functions in `namespace sgns`), `src/crypto/CMakeLists.txt` + `hasher.cpp` (small `.cpp`-backed lib)
**Apply to:** `src/multisig/MultiSig.hpp` (+ optional `.cpp`), `src/multisig/CMakeLists.txt`

### Signature verification delegation (never reimplement ECDSA)
**Source:** `src/account/GeniusAccount.hpp:207-209` / `GeniusAccount.cpp:824-838`
```cpp
static bool VerifySignature( const std::string          &address,
                             std::string_view            sig,
                             const std::vector<uint8_t> &data );
```
**Apply to:** `MultiSig::VerifyPayloadSignature` — call this directly, do not re-derive byte-order/double-SHA256 logic.

### Pure-static, dependency-free evaluator function shape
**Source:** `src/blockchain/ValidatorRegistry.hpp:190-203` (`EvaluateSlotQuorumStatic`)
**Apply to:** `MultiSig::EvaluateQuorum` — all inputs passed explicitly (signer_set, threshold, collected_signatures, payload), no instance state, no GlobalDB/CRDT reference anywhere in the signature or body.

### No-node CTest target registration
**Source:** `test/src/blockchain/CMakeLists.txt` lines 26-40 (`consensus_vote_slot_test`, `validator_registry_slot_quorum_test`) + `cmake/functions.cmake:8-27` (`addtest` macro)
**Apply to:** `test/src/multisig/CMakeLists.txt` — link only the new `multisig` target (which itself links only `sgns_genius_account`), never `blockchain_genesis`, `genius_node`, or `genius_node_test` (those pull CRDT/network transitively, violating MSIG-03).

### Doxygen header + `@param`/`@return` doc-comment style
**Source:** `src/blockchain/ConsensusAuth.hpp:1-6` (file header), `:22-31`, `:112-117` (function docs), `src/account/GeniusAccount.hpp:200-206` (`VerifySignature` doc)
**Apply to:** All new public functions/types in `MultiSig.hpp`.

## No Analog Found

None — every new/modified file has at least a role-match analog in the codebase (see table above). `src/multisig/MultiSig.cpp`'s exact "unweighted N-of-M with dedup + silent-skip-invalid" combination is itself new logic (RESEARCH.md confirms this is "the only genuinely new code" in the phase), but its *shape* (pure static function mirroring `EvaluateSlotQuorumStatic`) has a strong precedent, so it is not listed as a no-analog case.

## Metadata

**Analog search scope:** `src/blockchain/`, `src/account/`, `src/crypto/`, `test/src/blockchain/`, top-level `src/CMakeLists.txt`, `test/src/CMakeLists.txt`, `cmake/functions.cmake`
**Files scanned:** `ConsensusAuth.hpp` (full), `ValidatorRegistry.hpp` (partial, lines 1-220), `GeniusAccount.hpp` (partial, lines 190-230, 376), `hasher.hpp` (full), `src/crypto/CMakeLists.txt` (full), `src/blockchain/CMakeLists.txt` (full), `src/account/CMakeLists.txt` (full), `test/src/blockchain/CMakeLists.txt` (full), `validator_registry_slot_quorum_test.cpp` (partial, lines 1-60), `cmake/functions.cmake` (lines 1-40), `src/CMakeLists.txt` (full), `test/src/CMakeLists.txt` (full)
**Pattern extraction date:** 2026-07-21
