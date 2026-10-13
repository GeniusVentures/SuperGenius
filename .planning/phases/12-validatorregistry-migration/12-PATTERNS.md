# Phase 12: ValidatorRegistry Migration - Pattern Map

**Mapped:** 2026-07-27
**Files analyzed:** 2 (1 modified source file, 1 modified build file)
**Analogs found:** 2 / 2

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|-----------------|---------------|
| `src/blockchain/ValidatorRegistry.cpp` (call sites at `:621-625`, `:1387-1406`) | service (signature verify/sign call sites within a larger CRDT service class) | request-response (verify-payload-against-signature) | `src/securecrdt/SecureCrdt.cpp` (`AddSignature`, lines 95-134) | exact — same "call `multisig::VerifyPayloadSignature`, branch on bool, log + return failure/success" shape |
| `src/blockchain/impl/CMakeLists.txt` (`blockchain_genesis` target) | config (CMake link list) | n/a | `src/securecrdt/CMakeLists.txt` | exact — same "add `multisig` to an existing target's `target_link_libraries`" shape |

Note: no production code anywhere in the repo currently calls `sgns::multisig::` free functions except `src/securecrdt/SecureCrdt.cpp`. This is therefore the only real consumer analog available (confirmed via `grep -rln "multisig::" src/ test/`). `src/blockchain/Consensus.cpp` has its own unrelated `ConsensusManager::EvaluateQuorum` member function (weighted, not `multisig::EvaluateQuorum`) — it is NOT a multisig consumer and must not be used as a pattern source; it is listed here only to document why it was excluded.

## Pattern Assignments

### `src/blockchain/ValidatorRegistry.cpp` — verify side (genesis path, `VerifyUpdate`, currently `:1387-1406`)

**Analog:** `src/securecrdt/SecureCrdt.cpp:95-134` (`SecureCrdt::AddSignature`)

**Imports pattern** (`src/securecrdt/SecureCrdt.cpp:8-27`, only the relevant line shown):
```cpp
#include "multisig/MultiSig.hpp"
```
`ValidatorRegistry.cpp`'s current include block (`:7-28`) has no multisig include; add `#include "multisig/MultiSig.hpp"` alongside the existing `#include "account/GeniusAccount.hpp"` at line 21. Do not remove the `GeniusAccount.hpp` include — it is still needed elsewhere in the file (e.g. `account_->Sign(data)` at `:623`, and other non-genesis-path uses outside this phase's scope).

**Core verify pattern** (`src/securecrdt/SecureCrdt.cpp:114-120`):
```cpp
const std::vector<uint8_t> payload = current_value.value().toVector();
if ( !multisig::VerifyPayloadSignature( signer_address, signature, payload ) )
{
    logger_->error( "{}: invalid signature rejected locally key={} signer={}", __func__,
                    base_key.GetKey(), signer_address );
    return outcome::failure( Error::INVALID_SIGNATURE );
}
```

**Current code being replaced** (`src/blockchain/ValidatorRegistry.cpp:1396-1398`):
```cpp
if ( GeniusAccount::VerifySignature( signature.validator_id(),
                                     signature.signature(),
                                     signing_bytes.value() ) )
```
becomes (same surrounding `for`/`if (signature.validator_id() != genesis_authority_) continue;`/logging/`return true`/`return false` structure at `:1387-1406` untouched):
```cpp
if ( multisig::VerifyPayloadSignature( signature.validator_id(),
                                       signature.signature(),
                                       signing_bytes.value() ) )
```
Signature shapes line up 1:1 with the analog: `GeniusAccount::VerifySignature(address, signature, payload)` is exactly what `multisig::VerifyPayloadSignature` delegates to internally (`src/multisig/MultiSig.cpp:15-20`: `return sgns::GeniusAccount::VerifySignature(address, signature, payload);`), so this is a pure rename/namespace substitution — no adapter needed, confirming the CONTEXT.md D-01/discretion note that no byte-layout adapter is required. `signing_bytes.value()` is already a `std::vector<uint8_t>` (from `ComputeUpdateSigningBytes`, `:1329-1346`), matching `VerifyPayloadSignature`'s `const std::vector<uint8_t> &payload` parameter directly.

### `src/blockchain/ValidatorRegistry.cpp` — sign side (`StoreGenesisRegistry`, `:621-625`)

**D-01 scope note:** the signing call itself (`sign( signing_bytes.value() )` at `:623`) is a caller-supplied `sign` callback (see `:603-607`, `!sign` check), not a direct `GeniusAccount::Sign`/`account_->Sign` call in this snippet — CONTEXT.md's own text says "`StoreGenesisRegistry`'s signing counterpart" is in scope, but the actual call site at `:621-625` invokes the injected `sign` callback, not `GeniusAccount` or `multisig` directly. Confirm during planning whether `sign`'s implementation (wherever it is constructed/injected) is what needs to change to a `multisig`-based signer, or whether this call site needs no change at all since it already delegates via callback. No analog substitution is needed here unless the callback's construction site is found to hard-code `GeniusAccount::Sign` in a way `multisig` should intermediate — `multisig::MultiSig.hpp` exposes no `Sign`/signing primitive at all (only `VerifyPayloadSignature`/`EvaluateQuorum`), so there is likely nothing to change on the signing side; this call site may turn out to be already-compliant or a no-op for this phase.

## Shared Patterns

### MultiSig verify-and-branch (the only pattern needed for this phase)
**Source:** `src/securecrdt/SecureCrdt.cpp:114-120` (also mirrored at `:243`: `if ( address.empty() || !multisig::VerifyPayloadSignature( address, signature, payload ) )`)
**Apply to:** `src/blockchain/ValidatorRegistry.cpp:1396-1398` only (per D-01, the sole in-scope verify call site).
```cpp
if ( !multisig::VerifyPayloadSignature( address_or_id, signature, payload_bytes ) )
{
    logger_->error( "{}: <context-specific message>", __func__ );
    return <failure-in-caller's-idiom>;
}
```
`ValidatorRegistry.cpp` uses `bool`-returning `VerifyUpdate` (not `outcome::result`), so keep the existing `return true`/`return false` idiom at `:1400-1405` rather than adopting `SecureCrdt`'s `outcome::failure(Error::...)` idiom — only the verify-call expression changes, not the surrounding control flow or return type.

### Build wiring: adding `multisig` to an existing target
**Source:** `src/securecrdt/CMakeLists.txt:4-8`
```cmake
target_link_libraries(securecrdt
    PUBLIC
    crdt_globaldb
    multisig
)
```
**Apply to:** `src/blockchain/impl/CMakeLists.txt:16-31` (`blockchain_genesis` target). Current link list (`:16-31`) has no `multisig`/`securecrdt`/`trustedpeer` entry. Per D-06 and the Claude's Discretion note, add `multisig` directly (not `securecrdt`) since D-03 excludes any `SecureCrdt` API usage — a direct link is the honest dependency:
```cmake
target_link_libraries(blockchain_genesis
    PUBLIC
    outcome
    crdt_globaldb
    sgns_version
    buffer
    hasher
    hexutil
    logger
    sgns_genius_account
    ipfs-pubsub
    multisig
    ValidatorRegistryProto
    ConsensusProto
    PRIVATE
    SGBlockchainProto
)
```
`multisig`'s own `target_link_libraries` (`src/multisig/CMakeLists.txt:4-6`) is `PUBLIC sgns_genius_account` — already present in `blockchain_genesis`'s link list, so no transitive surprises.

## No Analog Found

None — both files in scope have a strong (exact-shape) analog.

## Metadata

**Analog search scope:** `src/multisig/`, `src/securecrdt/`, `src/blockchain/`, `src/trustedpeer/`, `src/account/`, `test/src/trustedpeer/`, `test/src/multisig/` (grepped for `multisig::`, `VerifyPayloadSignature`, `EvaluateQuorum`)
**Files scanned:** `MultiSig.hpp`, `MultiSig.cpp`, `SecureCrdt.hpp`, `SecureCrdt.cpp`, `Consensus.hpp`, `Consensus.cpp` (ruled out — unrelated member function, not a multisig consumer), `ValidatorRegistry.cpp` (target file), `blockchain/impl/CMakeLists.txt`, `securecrdt/CMakeLists.txt`, `multisig/CMakeLists.txt`
**Pattern extraction date:** 2026-07-27
