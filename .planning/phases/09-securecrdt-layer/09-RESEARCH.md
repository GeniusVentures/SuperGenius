# Phase 9: SecureCRDT Layer - Research

**Researched:** 2026-07-23
**Domain:** C++ CRDT-backed quorum-signed data layer, built on GlobalDB + MultiSig
**Confidence:** HIGH (all claims verified by direct file reads, no external libraries involved)

## Summary

This phase adds a thin, mandatory wrapper around `GlobalDB` that enforces quorum-verified
signatures for a static set of registered keys, using CRDT puts and filter callbacks as the
sole transport (no new networking). The implementation has three parts: (1) an
`ISignedCRDTData` interface each registered value type implements (codec + `Verify()` +
`Apply()`); (2) a static, code-declared `SecureCrdtRegistry` mapping key patterns to
{signer-set-source callback, quorum threshold, `ISignedCRDTData` factory} — modeled directly
on `IInputValidator::Register/Get/UnregisterIf` (`src/account/InputValidators.hpp`); (3) a
`SecureCrdt` wrapper class that is the ONLY sanctioned way to write to a registered key —
it looks up the registry entry for the key, reads the existing value + all `sig/<addr>`
entries via `GlobalDB::Get`/`QueryKeyValues`, calls `multisig::EvaluateQuorum`, and only calls
`GlobalDB::Put` when the write action itself (proposing a new value, or adding one more
signature) is locally valid — never trusting the remote-only `RegisterElementFilter` path
alone, because `GetDeltaFromNode` in `crdt_datastore.cpp` only invokes filters
`if (!created_by_self)`.

**Primary recommendation:** Build `SecureCrdt` as a stateless-per-call service object (like
`ValidatorRegistry`'s filter machinery, but generalized) that (a) registers one
`GlobalDB::RegisterElementFilter` pattern per registered base-key prefix to veto malformed/
unauthorized remote deltas, and (b) exposes `ProposeValue()` / `AddSignature()` /
`ReadIfQuorum()` methods that ALL local callers must use instead of `GlobalDB::Put`/`Get`
directly. Do not build a generic `SignedCrdtValue<T>` template — mirror `ValidatorRegistry`'s
per-type class style, as already decided in CONTEXT.md.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Payload codec + verify/apply per registered type | Application (`ISignedCRDTData` implementer) | — | Each data type (TrustedPeerRegistry, BurnConfig, ValidatorRegistry-migrated) owns its own protobuf schema and semantics; SecureCRDT must stay data-type-agnostic. |
| Static topic/key → policy registry | Application startup / static registration | — | Mirrors `IInputValidator::Register` — a process-wide, code-declared table resolved once at startup, no dynamic runtime mutation. |
| Quorum/signature evaluation | Domain logic (`multisig::EvaluateQuorum`) | — | Already shipped (Phase 8), pure and CRDT-agnostic; SecureCRDT is its first consumer. |
| CRDT read/write transport | Storage/CRDT (`GlobalDB`) | — | GlobalDB owns Put/Get/QueryKeyValues/filter registration; SecureCRDT never bypasses it and never adds new pubsub/RPC. |
| Local pre-merge enforcement (remote deltas) | CRDT filter callback (`RegisterElementFilter`) | — | Synchronous veto point for data arriving from peers; runs on the DAG-sync worker thread inline. |
| Local pre-merge enforcement (local writes) | SecureCRDT wrapper (new) | — | `RegisterElementFilter` does NOT run for locally-authored `Put` calls (`created_by_self` check in `crdt_datastore.cpp`); the wrapper is the only enforcement point for local writes. |
| Trust re-derivation on read | Reader-side (`SecureCrdt::ReadIfQuorum`) | Local RocksDB cache (D-05, perf only) | No "final" marker is ever written (D-04); every reader re-verifies signatures + quorum independently. |

## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Partial signatures during collection stored in CRDT itself (each signer's signature its own CRDT entry), not in-memory.
- **D-02:** Value stored at `base_key` directly; signatures at `base_key.ChildString("sig").ChildString(signer_address)`.
- **D-03:** All writes to registered keys MUST go through a SecureCRDT wrapper API (never raw `GlobalDB::Put`) that validates quorum/signatures locally before Put — since GlobalDB's filter callback only runs on remote-originated deltas, not local writes.
- **D-04:** No "final" CRDT write — every reader independently re-derives trust by reading value + all sig/* entries + running `MultiSig::EvaluateQuorum`. No node "declares" finality.
- **D-05:** Optional local-only RocksDB cache memoizing "quorum already verified for base_key at value/CID X" — pure performance optimization, never replicated, invalidated when new sig/* entries appear.

### Claude's Discretion
- Exact registry API shape for declaring a {topic/key pattern, signer-set source, quorum rule, `ISignedCRDTData` type} entry — base on `ValidatorRegistry::RegisterFilter`'s shape but generalized, per milestone decision to keep `ISignedCRDTData` interface-based per-type classes (not a generic template).
- Exact serialization format per registered value — protobuf per type via `.SerializeToString()` into a `base::Buffer`, following `ValidatorRegistry`'s pattern — left to planner, each `ISignedCRDTData` implementer owns its own payload codec.
- Where the local RocksDB cache (D-05) lives and its exact key scheme — planner's discretion; must NOT be part of the CRDT-replicated data path.

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope. (Carried forward from Phase 8: raw-public-key signer identity, still deferred to Phase 10 if genesis-time seeding needs it.)

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SCRDT-01 | `ISignedCRDTData` interface: payload codec, `Verify()`, `Apply()` | See "Standard Stack" / "Code Examples" — model on `ISignedCRDTData` abstract class shown below; each implementer wraps a protobuf message the same way `ValidatorRegistry` wraps `validator::Registry`. |
| SCRDT-02 | Static registry: topic/key pattern → {signer-set source, quorum rule, type} | Modeled on `IInputValidator::Register/Get/UnregisterIf` (`src/account/InputValidators.hpp` L86-116); signer-set source abstracted as an injectable callback (see Open Questions / Pitfall on TrustedPeerRegistry not existing yet). |
| SCRDT-03 | Registered-key writes require quorum; under-signed writes rejected locally before applied | Enforced by `SecureCrdt` wrapper (D-03) — never raw `GlobalDB::Put`; wrapper calls `multisig::EvaluateQuorum` before any `Put`. |
| SCRDT-04 | Propose/sign/quorum via CRDT puts + filter callbacks only, no new networking/RPC | Achieved by reusing `GlobalDB::Put`/`Get`/`QueryKeyValues`/`RegisterElementFilter`/`RegisterNewElementCallback` exclusively — see "Architecture Patterns". |

## Standard Stack

### Core
| Component | Location | Purpose | Why Standard |
|-----------|----------|---------|--------------|
| `GlobalDB` | `src/crdt/globaldb/globaldb.hpp` | CRDT-replicated key/value transport (Put/Get/Query/filter callbacks) | Existing sole CRDT facade in the codebase; SCRDT-04 forbids new transport. |
| `multisig::MultiSig` | `src/multisig/MultiSig.hpp` (Phase 8, shipped) | `VerifyPayloadSignature`, `EvaluateQuorum` | Purpose-built primitive for exactly this; CRDT-agnostic per MSIG-03. |
| `HierarchicalKey` | `src/crdt/hierarchical_key.hpp` / `impl/hierarchical_key.cpp` | Key derivation for `base_key` and `base_key/sig/<addr>` | Only key-derivation mechanism in the codebase; string-concatenation based, no wildcard support beyond regex on the resulting string. |
| protobuf (existing dependency) | per registered type, e.g. `securecrdt.proto` | Payload codec for each `ISignedCRDTData` implementer | Matches `ValidatorRegistry`'s existing `.SerializeToString()` into `base::Buffer` pattern — no new serialization library needed. |

### Supporting
| Component | Purpose | When to Use |
|-----------|---------|-------------|
| `RocksDB` (`storage::rocksdb`, already linked via `crdt_globaldb`/`ipfs-lite-cpp::ipfs_datastore_rocksdb`) | D-05 optional local quorum-verified cache | Only if the planner chooses to implement the perf cache in this phase; otherwise defer — D-05 says "MAY", not required for SCRDT-01..04. |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Interface-based per-type `ISignedCRDTData` classes | Generic `SignedCrdtValue<T>` template | Rejected at milestone level (STATE.md Key Decisions) — matches `ValidatorRegistry`'s existing per-type style, avoids template-heavy API surface for 3 future consumers (TrustedPeerRegistry, BurnConfig, ValidatorRegistry-migration) with very different payload shapes. |
| In-CRDT partial-signature storage (D-01) | In-memory batch tracking like `ValidatorRegistry`'s `pending_certificate_subjects_by_base_` | Rejected — doesn't survive restarts, requires live connection to proposer; explicitly ruled out by D-01. |

**No new package installation required.** This phase adds no third-party dependency —
everything is built from already-linked in-tree components (`crdt_globaldb`, `multisig`,
protobuf via `add_proto_library`). Package Legitimacy Audit is therefore N/A.

## Package Legitimacy Audit

N/A — this phase introduces zero new external packages. All building blocks
(`crdt_globaldb`, `multisig`, protobuf codegen via `add_proto_library`) are already
present and linked in the codebase (see Build Wiring below).

## Architecture Patterns

### System Architecture Diagram

```
                         ┌───────────────────────────────┐
                         │   Application caller (e.g.     │
                         │   TrustedPeerRegistry, Phase10) │
                         └───────────────┬────────────────┘
                                         │ ProposeValue(key, payload)
                                         │ AddSignature(key, addr, sig)
                                         │ ReadIfQuorum(key) -> value?
                                         ▼
                         ┌───────────────────────────────────────────┐
                         │        SecureCrdt (new, this phase)        │
                         │  - looks up SecureCrdtRegistry entry        │
                         │    for key pattern                         │
                         │  - fetches signer-set + threshold via       │
                         │    injected "signer-set source" callback    │
                         │  - LOCAL pre-write check:                   │
                         │    EvaluateQuorum() over existing sig/*      │
                         │    entries + the new signature being added  │
                         │  - calls GlobalDB::Put ONLY if the write     │
                         │    itself is individually valid              │
                         │    (a proposal, or one more valid sig)       │
                         └───────┬─────────────────────┬───────────────┘
                                 │ Put(base_key,...)    │ Put(base_key/sig/addr,...)
                                 │ Get/QueryKeyValues    │
                                 ▼                       ▼
                         ┌─────────────────────────────────────────────┐
                         │                 GlobalDB                     │
                         │  - Put/Get/QueryKeyValues (local + remote)    │
                         │  - RegisterElementFilter(pattern, cb)         │
                         │    -> runs ONLY for remote-originated deltas  │
                         │       (crdt_datastore.cpp: !created_by_self)  │
                         │  - RegisterNewElementCallback(pattern, cb)    │
                         │    -> post-accept notification (local+remote)│
                         └───────┬───────────────────────────────────────┘
                                 │ pubsub / DAG-sync (existing, unmodified)
                                 ▼
                         ┌─────────────────────────────────────────────┐
                         │              Remote peers                    │
                         │  Each peer's SecureCrdt filter callback       │
                         │  re-verifies incoming value/sig deltas        │
                         │  BEFORE merge (veto = return empty vector)    │
                         └─────────────────────────────────────────────┘

READ PATH (any node, at any time):
  SecureCrdt::ReadIfQuorum(base_key)
    -> GlobalDB::Get(base_key)                     [current value]
    -> GlobalDB::QueryKeyValues(base_key/sig prefix) [all signer entries]
    -> multisig::EvaluateQuorum(signer_set, threshold, sigs, value_bytes)
    -> (optional) consult/update local RocksDB cache keyed by (base_key, value CID) -- D-05
    -> return value only if has_quorum == true
```

### Recommended Project Structure
```
src/securecrdt/
├── ISignedCRDTData.hpp        # SCRDT-01 interface
├── SecureCrdtRegistry.hpp     # SCRDT-02 static registry (header-only, like InputValidators.hpp)
├── SecureCrdtRegistry.cpp     # if a .cpp is needed for logging/impl
├── SecureCrdt.hpp             # SCRDT-03/04 wrapper: ProposeValue/AddSignature/ReadIfQuorum
├── SecureCrdt.cpp
├── proto/securecrdt.proto     # optional: shared signing-payload framing message (see below)
└── CMakeLists.txt

test/src/securecrdt/
├── securecrdt_registry_test.cpp   # registry registration/lookup, no GlobalDB needed
├── securecrdt_quorum_gate_test.cpp # unsigned/under-signed write rejected (SCRDT-04 automated test)
└── CMakeLists.txt
```

### Pattern 1: Static Registry (SCRDT-02), modeled on `IInputValidator`
**What:** A static, process-wide map from key-pattern string to a policy entry
`{signer_set_source, threshold, make_instance}`.
**When to use:** At startup, each `ISignedCRDTData`-implementing module calls
`SecureCrdtRegistry::Register(pattern, entry)` once (e.g., in a static initializer or an
explicit `Init()` call from `main`/`GeniusNode` construction — check how `ValidatorRegistry`
itself gets wired up at node-construction time for the calling convention to mirror).
**Example (source pattern to generalize):**
```cpp
// Source: src/account/InputValidators.hpp L84-116 (existing precedent)
class IInputValidator
{
public:
    using ValidatorPtr = const IInputValidator *;
    static void Register( const std::string &chain_id, ValidatorPtr validator )
    {
        registry()[chain_id] = validator;
    }
    static void UnregisterIf( const std::string &chain_id, ValidatorPtr expected )
    {
        auto it = registry().find( chain_id );
        if ( it != registry().end() && it->second == expected ) { registry().erase( it ); }
    }
    static ValidatorPtr Get( const std::string &chain_id )
    {
        auto it = registry().find( chain_id );
        return it != registry().end() ? it->second : nullptr;
    }
private:
    static std::unordered_map<std::string, ValidatorPtr> &registry()
    {
        static std::unordered_map<std::string, ValidatorPtr> map;
        return map;
    }
};
```
**Recommended generalization for `SecureCrdtRegistry`:**
```cpp
// New code, src/securecrdt/SecureCrdtRegistry.hpp
struct SignerSetSnapshot
{
    std::vector<std::string> signer_set;
    uint64_t                 threshold = 0;
};

/// Injectable — Phase 9 has no TrustedPeerRegistry yet (that's Phase 10).
/// A test/dev implementation can be a fixed-list lambda; Phase 10 will supply
/// TrustedPeerRegistry-backed implementation without SecureCRDT changing.
using SignerSetSource = std::function<outcome::result<SignerSetSnapshot>( const std::string &base_key )>;

struct SecureCrdtRegistryEntry
{
    std::string      key_pattern;      // e.g. "/gnus-burn-config" (regex-compatible, see Pitfall 1)
    SignerSetSource  signer_set_source;
    std::function<std::shared_ptr<ISignedCRDTData>()> make_instance;
};

class SecureCrdtRegistry
{
public:
    static void Register( const std::string &key_pattern, SecureCrdtRegistryEntry entry );
    static void UnregisterIf( const std::string &key_pattern, /* compare token */ );
    static const SecureCrdtRegistryEntry *Resolve( const std::string &key ); // longest-prefix match
private:
    static std::unordered_map<std::string, SecureCrdtRegistryEntry> &registry();
};
```
This directly answers the "signer-set source" open question in the phase brief: abstract it
as `SignerSetSource` (an `std::function`), so Phase 9's own tests inject a fixed-list lambda,
and Phase 10 later injects a `TrustedPeerRegistry`-backed lookup with zero API change.

### Pattern 2: Filter registration + local-write gate (SCRDT-03/04), modeled on `ValidatorRegistry::RegisterFilter`
**What:** One `RegisterElementFilter` per registered base-key prefix (remote-delta veto) PLUS
a wrapper method that performs the identical check before any local `Put`.
**Example (source pattern to generalize, verified at exact lines):**
```cpp
// Source: src/blockchain/ValidatorRegistry.cpp L1231-1284 (verified)
bool ValidatorRegistry::RegisterFilter()
{
    const std::string pattern           = "/?" + std::string( RegistryKey() );
    auto              weak_self         = weak_from_this();
    const bool        filter_registered = db_->RegisterElementFilter(
        pattern,
        [weak_self]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
        {
            if ( auto strong = weak_self.lock() )
            {
                return strong->FilterRegistryUpdate( element );
            }
            return std::nullopt;
        } );
    const bool callback_registered = db_->RegisterNewElementCallback(
        pattern,
        [weak_self]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
        {
            if ( auto strong = weak_self.lock() )
            {
                strong->RegistryUpdateReceived( std::move( new_data ), cid );
            }
        } );
    db_->AddListenTopic( std::string( ValidatorTopic() ) );
    return filter_registered && callback_registered;
}

std::optional<std::vector<crdt::pb::Element>> ValidatorRegistry::FilterRegistryUpdate(
    const crdt::pb::Element &element )
{
    // element.key() / element.value() -- pb::Element has key (string), id (string), value (bytes)
    // Source: src/crdt/proto/delta.proto L10-15
    auto decoded_update = DeserializeRegistryUpdate( /* element.value() bytes */ );
    if ( decoded_update.has_error() ) { return std::vector<crdt::pb::Element>{}; } // REJECT
    if ( !VerifyUpdate( decoded_update.value(), false ) ) { return std::vector<crdt::pb::Element>{}; } // REJECT
    return std::nullopt; // ACCEPT (keep element, allow merge)
}
```
**Critical veto semantics (verified `src/crdt/impl/crdt_data_filter.cpp` L90-160):** returning
`std::nullopt` = element kept; returning ANY vector (even empty) = element rejected/deleted
from the delta BEFORE merge. This runs synchronously on the DAG-sync worker thread and only
for remote-originated deltas (`crdt_datastore.cpp` `GetDeltaFromNode`, `!created_by_self`
check) — hence D-03's requirement that the wrapper re-implement the identical check for local
`Put` calls, since the filter never fires for them.

**Regex/pattern convention (verified):** `ValidatorRegistry` uses a single pattern
`"/?" + RegistryKey()` (i.e. `"/?gnus-validator-registry"`) for BOTH the value key and,
implicitly, any deeper children — because in that design the whole registry blob lives at one
key, no `sig/*` children exist. **This phase is different: `base_key` and
`base_key/sig/<addr>` are separate CRDT entries that both need filter coverage.** There is no
evidence in the codebase of a single filter pattern matching a key AND arbitrary children in
one registration — `RegisterElementFilter`/`RegisterNewElementCallback` take one `pattern`
string matched against `element.key()`. **Recommendation:** register TWO filter patterns per
`SecureCrdtRegistryEntry` — one for the base value key (e.g. `"/?" + base_key`) and one for
the signature sub-tree (e.g. `"/?" + base_key + "/sig/.*"` if the underlying filter matching
supports regex wildcards, which needs verification — see Open Questions/Pitfalls below,
because the exact matching engine behind `RegisterElementFilter`'s `pattern` argument was not
located in this research pass and should be confirmed by reading
`src/crdt/crdt_data_filter.cpp` in full during planning, not just L90-160).

### Pattern 3: Read-path quorum re-derivation (D-04), no template precedent — new code
```cpp
// New code, src/securecrdt/SecureCrdt.cpp — sketch
outcome::result<std::optional<base::Buffer>> SecureCrdt::ReadIfQuorum( const HierarchicalKey &base_key )
{
    auto entry = SecureCrdtRegistry::Resolve( base_key.GetKey() );
    if ( !entry ) return outcome::failure( Error::UNREGISTERED_KEY );

    OUTCOME_TRY( auto value, db_->Get( base_key ) );

    auto sig_prefix = base_key.ChildString( "sig" ).GetKey();
    OUTCOME_TRY( auto sig_rows, db_->QueryKeyValues( sig_prefix ) ); // one row per signer

    std::vector<std::pair<std::string, std::string>> collected_signatures;
    for ( auto &row : sig_rows ) { /* address = last path segment of row key; signature = row value */ }

    OUTCOME_TRY( auto signer_snapshot, entry->signer_set_source( base_key.GetKey() ) );
    auto result = multisig::EvaluateQuorum( signer_snapshot.signer_set,
                                            signer_snapshot.threshold,
                                            collected_signatures,
                                            std::vector<uint8_t>( value.begin(), value.end() ) );
    if ( !result.has_quorum ) return outcome::success( std::nullopt );
    return outcome::success( std::optional<base::Buffer>{ value } );
}
```

### Anti-Patterns to Avoid
- **Trusting `RegisterElementFilter` alone:** it never runs for locally-authored writes
  (verified: `crdt_datastore.cpp` `GetDeltaFromNode`, filter only invoked
  `if (!created_by_self)`). Any code path that calls `GlobalDB::Put` directly on a registered
  key bypasses ALL enforcement — this is exactly what D-03 forbids.
- **Writing a "final" marker key:** rejected by D-04. Do not add a `base_key/finalized` or
  similar CRDT entry — quorum is always re-derived from `value + sig/*` at read time.
- **In-memory partial-signature tracking:** rejected by D-01 (unlike `ValidatorRegistry`'s
  `pending_certificate_subjects_by_base_`, an in-memory map). Every signature must be its own
  CRDT `Put`.
- **Generic `SignedCrdtValue<T>` template:** rejected at milestone level — use interface-based
  per-type classes implementing `ISignedCRDTData`.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Signature verification over a payload | Custom ECDSA/secp256k1 wrapper | `multisig::VerifyPayloadSignature` (Phase 8, shipped) | Already delegates to `GeniusAccount::VerifySignature`; reinventing risks subtle crypto bugs. |
| N-of-M quorum counting/dedup | Custom loop over signature vector | `multisig::EvaluateQuorum` (Phase 8, shipped) | Handles dedup-before-verify ordering correctly (a malicious duplicate entry with a garbage signature cannot suppress a signer's valid earlier entry) — subtle and already tested. |
| CRDT key derivation | String concatenation ad hoc | `HierarchicalKey::ChildString` | Ensures consistent `/`-prefixing/no-trailing-slash normalization already implemented in `impl/hierarchical_key.cpp`. |
| New propose/sign RPC | A new pubsub topic / gRPC endpoint for signature exchange | `GlobalDB::Put` of a `sig/<addr>` sub-key + existing pubsub/DAG-sync replication | SCRDT-04 explicitly forbids new networking; CRDT already replicates any `Put`. |

**Key insight:** everything this phase needs (crypto, quorum math, key derivation, transport)
already exists in the codebase from Phase 8 and pre-existing CRDT infrastructure. The ONLY
new logic is: (1) the interface contract (`ISignedCRDTData`), (2) the static registry
(`SecureCrdtRegistry`), and (3) the local-write enforcement gate (`SecureCrdt` wrapper) that
makes GlobalDB's existing filter mechanism apply symmetrically to local writes.

## Common Pitfalls

### Pitfall 1: Filter pattern matching engine is unconfirmed for wildcard/child matching
**What goes wrong:** Assuming `RegisterElementFilter("/?base_key/sig/.*", cb)` works like a
regex when the actual implementation might do plain prefix/exact string matching only.
**Why it happens:** `ValidatorRegistry`'s only precedent (`"/?" + RegistryKey()`) never needs
to match children, since its whole payload lives at one key — this phase is the first
consumer needing to match both a base key and an unbounded set of `sig/<addr>` children.
**How to avoid:** Before finalizing the filter-registration design, read
`src/crdt/crdt_data_filter.cpp` in full (this research only inspected L90-160, the veto
semantics, not the pattern-matching implementation itself) to confirm whether `pattern` is a
literal prefix, a full regex, or something else (e.g., std::regex vs. simple `starts_with`).
**Warning signs:** If it's plain prefix matching, `"/?" + base_key` (no `/sig/.*` needed) may
already match `base_key/sig/<addr>` as a substring-prefix match depending on delimiter
handling — verify with a concrete unit test using an in-process `GlobalDB` before writing
production filter-registration code.

### Pitfall 2: TrustedPeerRegistry doesn't exist yet — signer-set source must be injectable
**What goes wrong:** Hard-coding `SecureCrdtRegistry` entries to call into
`TrustedPeerRegistry` (Phase 10) creates a forward dependency that doesn't compile/link in
Phase 9.
**Why it happens:** SCRDT-02 requires each registry entry to declare a "signer-set source",
but the only real signer-set provider (`TrustedPeerRegistry`) is scoped to Phase 10.
**How to avoid:** Define `SignerSetSource` as an `std::function<...>` (see Pattern 1 above).
Phase 9's own tests inject a fixed-list lambda; Phase 10 supplies the real implementation
without touching `SecureCrdtRegistry`'s shape.
**Warning signs:** Any `#include "TrustedPeerRegistry.hpp"` inside `src/securecrdt/` during
this phase is a scope violation per CONTEXT.md's phase boundary ("does NOT implement
TrustedPeerRegistry").

### Pitfall 3: Real-`GlobalDB` unit tests require a full pubsub/libp2p node
**What goes wrong:** Assuming a lightweight in-memory `GlobalDB` fixture exists for pure unit
tests (like `multisig`'s zero-CRDT-dependency tests).
**Why it happens:** The only real `GlobalDB` instantiation found in `test/src/` is
`test/src/crdt/globaldb_integration.cpp`'s `TestNodeCollection::addNode`, which starts a real
`GossipPubSub` (`pubsub->Start(0, {}, listenIp, {})`), a real `libp2p::Scheduler`, a real
`graphsyncnetwork`, and runs an `io_context` on its own thread — no in-memory-only fixture
exists. `ValidatorRegistry`'s own tests (`test/src/blockchain/validator_registry_*_test.cpp`)
avoid instantiating `GlobalDB` at all — they test policy logic (`VerifyUpdate`, quorum,
promotion) directly, not through a live datastore.
**How to avoid:** For SCRDT-03/04's required automated test ("unsigned/under-signed write
rejected... verified by an automated test"), plan for ONE of two approaches: (a) a single-node
`GlobalDB` instance (no peer connection needed — `SecureCrdt::ProposeValue`/`AddSignature`/
`ReadIfQuorum` can be exercised entirely via local `Put`/`Get`/`QueryKeyValues` against one
node, reusing `TestNodeCollection::addNode`'s setup but skipping `connectNodes()`), or (b) a
pure logic-level test that calls `SecureCrdt`'s local-write-gate method directly with an
injected/fake datastore interface, avoiding `GlobalDB` entirely (would require extracting an
interface from `GlobalDB` first, which is a larger change likely out of scope). **Recommend
(a)**: single unconnected node is the minimal-diff path and matches the existing integration
test's node-construction code almost verbatim, and per-test setup cost (~seconds to start
libp2p host) is acceptable for a handful of SecureCRDT tests.

### Pitfall 4: `GlobalDB::Get` existence — resolved, it DOES exist
**What goes wrong:** Assuming reads only happen via filter/new-element callbacks (push-only).
**Resolution:** `GlobalDB::Get(const HierarchicalKey &key) -> outcome::result<Buffer>` exists
at `src/crdt/globaldb/globaldb.hpp` L111-115 (verified) — a direct pull-based read is
available and is exactly what `SecureCrdt::ReadIfQuorum` should use, alongside
`QueryKeyValues(std::string_view keyPrefix)` (L124-128, verified) for enumerating all
`sig/<addr>` rows under a base key's `sig/` sub-tree.

## Code Examples

### GlobalDB API surface relevant to this phase (all verified, `src/crdt/globaldb/globaldb.hpp`)
```cpp
// Source: src/crdt/globaldb/globaldb.hpp (line numbers verified against current file)
using DataPair                        = std::pair<HierarchicalKey, Buffer>;                          // L70
using GlobalDBFilterCallback          = CrdtDatastore::CRDTElementFilterCallback;                     // L72
using GlobalDBNewElementCallback      = CrdtDatastore::CRDTNewElementCallback;                        // L73
using GlobalDBDeletedElementCallback  = CrdtDatastore::CRDTDeletedElementCallback;                    // L74

outcome::result<CID> Put( const HierarchicalKey &key, const Buffer &value,
                          const std::unordered_set<std::string> &topics );                            // L98-100
outcome::result<CID> Put( const std::vector<DataPair> &data_vector,
                          const std::unordered_set<std::string> &topics );                            // L108-109 (batch)
outcome::result<Buffer> Get( const HierarchicalKey &key );                                             // L111-115 (EXISTS)
outcome::result<CID> Remove( const HierarchicalKey &key, const std::unordered_set<std::string> &topics ); // L122
outcome::result<QueryResult> QueryKeyValues( std::string_view keyPrefix );                              // L124-128
outcome::result<QueryResult> QueryKeyValues( const std::string &prefix_base, const std::string &middle_part,
                                             const std::string &remainder_prefix );                    // L137-139 (wildcard middle)

outcome::result<void> AddBroadcastTopic( const std::string &topicName );                               // L152
void                  AddTopicName( const std::string &topicName );                                    // L153
void                  AddListenTopic( const std::string &topicName );                                  // L154

bool RegisterElementFilter( const std::string &pattern, GlobalDBFilterCallback filter );               // L167
bool RegisterNewElementCallback( const std::string &pattern, GlobalDBNewElementCallback callback );    // L174
bool RegisterDeletedElementCallback( const std::string &pattern, GlobalDBDeletedElementCallback cb );  // L181
void UnregisterElementFilter( const std::string &pattern );                                            // L186
void UnregisterNewElementCallback( const std::string &pattern );                                       // L193
void UnregisterDeletedElementCallback( const std::string &pattern );                                   // L198

outcome::result<std::unordered_set<std::string>> GetMonitoredTopics() const;                           // L244
```

**Underlying type aliases (verified, `src/crdt/crdt_datastore.hpp` / `crdt_data_filter.hpp` / `crdt_callback_manager.hpp`):**
```cpp
// src/crdt/crdt_data_filter.hpp L30
using ElementFilterCallback = std::function<std::optional<std::vector<pb::Element>>( const pb::Element & )>;
// src/crdt/crdt_callback_manager.hpp L27-28, L39
using NewDataPair     = std::pair<std::string, base::Buffer>;
using NewDataCallback = std::function<void( NewDataPair new_data, std::string cid )>;
using DeletedDataCallback = std::function<void( std::string deleted_key, std::string cid )>;
```

**`pb::Element` fields (verified, `src/crdt/proto/delta.proto` L10-15):**
```protobuf
message Element {
  string key = 1;   // full key path this delta element targets
  string id = 2;    // must combine with key to form a unique identifier
  bytes value = 3;  // raw payload bytes (what your codec/Verify() operates on)
}
```

### HierarchicalKey usage (verified, `src/crdt/hierarchical_key.hpp` + `impl/hierarchical_key.cpp`)
```cpp
HierarchicalKey base_key( "gnus-burn-config" );              // normalized to "/gnus-burn-config"
HierarchicalKey sig_key = base_key.ChildString( "sig" ).ChildString( signer_address );
// sig_key.GetKey() == "/gnus-burn-config/sig/<signer_address>"
bool is_top = base_key.IsTopLevel();                          // true (single path segment)
std::vector<std::string> parts = sig_key.GetList();            // {"gnus-burn-config", "sig", "<addr>"}
```

### MultiSig usage (verified, `src/multisig/MultiSig.hpp`, Phase 8 shipped)
```cpp
bool ok = sgns::multisig::VerifyPayloadSignature( address, signature, payload_bytes );

sgns::multisig::QuorumResult result = sgns::multisig::EvaluateQuorum(
    signer_set,            // std::vector<std::string>
    threshold,             // uint64_t
    collected_signatures,  // std::vector<std::pair<std::string,std::string>> (address, signature)
    payload_bytes );       // std::vector<uint8_t>
// result.has_quorum, result.valid_unique_count
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `ValidatorRegistry`'s bespoke `VerifyUpdate` (not built on `MultiSig.hpp`) | `SecureCrdt` wired directly to `multisig::EvaluateQuorum`/`VerifyPayloadSignature` | This phase (first real consumer of `MultiSig.hpp`) | `ValidatorRegistry` itself is NOT touched this phase (Phase 12's job) — but the new `SecureCrdt` pattern is the template Phase 12 will later migrate `ValidatorRegistry` onto. |
| In-memory pending-signature batch tracking (`ValidatorRegistry`'s `pending_certificate_subjects_by_base_`) | CRDT-native per-signer entries (`base_key/sig/<addr>`) | This phase (D-01) | Signature collection survives restarts and requires no live connection between proposer and signers. |

**Deprecated/outdated:** none — this is greenfield code, not a migration of existing behavior
(migration of `ValidatorRegistry` itself is explicitly Phase 12, out of scope here).

## Runtime State Inventory

Not applicable — this is a greenfield addition (`src/securecrdt/`), not a rename/refactor/
migration phase. No existing stored data, service config, OS-registered state, secrets, or
build artifacts reference names being changed.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `RegisterElementFilter`'s `pattern` argument matching semantics (regex vs. prefix vs. exact) beyond what was directly read in `crdt_data_filter.cpp` L90-160 | Pitfall 1 / Pattern 2 | If it's not regex-capable, a single filter registration cannot cover both `base_key` and all `base_key/sig/*` children — planner must budget for either two separate registrations or confirming a wildcard convention before implementation. **Not yet verified — full `crdt_data_filter.cpp` read is needed before finalizing task breakdown.** |
| A2 | No existing in-memory-only/lightweight `GlobalDB` test fixture beyond the full-node `TestNodeCollection` in `globaldb_integration.cpp` | Pitfall 3 | If a lighter fixture exists elsewhere (not found by this research's targeted grep), planner may over-budget test setup complexity/time. |
| A3 | Node-construction call site where `ValidatorRegistry::RegisterFilter()`-equivalent registration would need to be invoked for `SecureCrdtRegistry` entries (i.e., where in `GeniusNode`/startup code such static registration hooks are wired) was not located in this research pass | Pattern 1 | Planner must grep for where `ValidatorRegistry` itself gets constructed/wired to `GlobalDB` (search `RegisterFilter()` call site) to mirror the wiring point exactly. |

## Open Questions

1. **(RESOLVED) Filter pattern-matching engine for `RegisterElementFilter`**
   - Confirmed by direct read of `src/crdt/impl/crdt_data_filter.cpp`:
     - `RegisterElementFilter(pattern, filter)` (L20-25) stores `FilterCallbackEntry{pattern, std::regex(pattern), filter}` — `pattern` IS compiled as a real `std::regex`, not a prefix/literal check.
     - Matching (L108) uses `std::regex_match(element.key(), entry->regex)` — full-string match against the element's key.
   - **Conclusion: ONE filter registration per registered base_key suffices**, using a pattern like `"/?base_key(/sig/.*)?"` to cover both the value entry and all `sig/<address>` children in a single `RegisterElementFilter` call. No need for two registrations.
   - Planner action: `SecureCrdt`'s filter-registration task should build this combined regex per registered key (mirroring `ValidatorRegistry::RegisterFilter`'s `"/?" + RegistryKey()` pattern, extended with the optional `(/sig/.*)?` suffix).

2. **(RESOLVED) Node-construction wiring point for static registry initialization**
   - Confirmed: `ValidatorRegistry::RegisterFilter()` is called from `ValidatorRegistry`'s own factory/constructor path (`ValidatorRegistry.cpp` L165: `if (!instance->RegisterFilter())` inside what appears to be a `New(...)`-style factory, called once per instance construction — not via static init order).
   - **Conclusion:** `SecureCrdt`'s registry should follow the same pattern — filter registration happens explicitly inside the component's own factory/constructor when it's instantiated (at whatever point in `GeniusNode`/node startup constructs it), not via C++ static initialization order across translation units. Since Phase 9 doesn't touch `GeniusNode` wiring yet (that's implicit until Phase 10/11 actually construct a `SecureCrdt` instance in a real node), the planner should design `SecureCrdt`'s constructor/factory to self-register its filters the same way `ValidatorRegistry` does, so future phases just need to construct it.

## Environment Availability

Skipped — no external tool/service/runtime dependencies beyond what's already linked in the
build (protobuf codegen, RocksDB, libp2p/pubsub — all already present via `crdt_globaldb` and
existing CMake wiring). This is a pure C++ code-addition phase.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest (`addtest(...)` CMake macro, same as `multisig`/`blockchain` test dirs) |
| Config file | `test/src/multisig/CMakeLists.txt` is the closest precedent (no GlobalDB dependency); a new `test/src/securecrdt/CMakeLists.txt` following the same `addtest(...)` + `target_link_libraries(... securecrdt ...)` shape is needed |
| Quick run command | `ctest -R securecrdt` (once targets are named `securecrdt_*_test`) |
| Full suite command | `ctest` (project-wide, matches existing convention — see recent commit "Enabling Ctest on OSX") |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| SCRDT-01 | `ISignedCRDTData` interface compiles/links with a concrete test implementer | unit | `ctest -R securecrdt_interface_test` | ❌ Wave 0 |
| SCRDT-02 | Registry resolves topic/key pattern → policy entry at "startup" | unit | `ctest -R securecrdt_registry_test` | ❌ Wave 0 |
| SCRDT-03 | Under-signed write rejected locally, never applied, before quorum reached | unit (single-node `GlobalDB`, see Pitfall 3) | `ctest -R securecrdt_quorum_gate_test` | ❌ Wave 0 |
| SCRDT-04 | Propose+sign+quorum sequence via CRDT puts/filter callbacks only | unit/integration (single-node `GlobalDB`) | `ctest -R securecrdt_propose_sign_quorum_test` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest -R securecrdt` (fast subset)
- **Per wave merge:** full `ctest` run
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `test/src/securecrdt/CMakeLists.txt` — new, following `test/src/multisig/CMakeLists.txt` pattern for logic-only tests, plus the single-node `GlobalDB` fixture pattern from `test/src/crdt/globaldb_integration.cpp` for SCRDT-03/04's tests.
- [ ] `src/securecrdt/CMakeLists.txt` — new library target, linking `crdt_globaldb` + `multisig` (see Build Wiring below).
- [ ] Root `src/CMakeLists.txt` and `test/src/CMakeLists.txt` need `add_subdirectory(securecrdt)` added (currently absent — verified both files list `multisig` but not `securecrdt`).

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V2 Authentication | no | Not applicable — this is peer-to-peer node authorization, not user auth. |
| V3 Session Management | no | Not applicable. |
| V4 Access Control | yes | Quorum-of-signatures IS the access-control mechanism for registered CRDT keys — enforced by `SecureCrdt` wrapper (D-03), never bypassable via raw `GlobalDB::Put`. |
| V5 Input Validation | yes | Codec deserialization (`ISignedCRDTData::Verify()`) must reject malformed protobuf the same way `ValidatorRegistry::FilterRegistryUpdate` does (parse-failure → reject, verified pattern L1263-1284). |
| V6 Cryptography | yes | Never hand-roll — delegate entirely to `multisig::VerifyPayloadSignature` (which itself delegates to `GeniusAccount::VerifySignature`, Phase 8 shipped and audited). |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Bypassing the SecureCRDT gate via a direct `GlobalDB::Put` call elsewhere in the codebase | Tampering / Elevation of Privilege | D-03's core mandate: enforce via code review / architecture that NO caller ever calls `GlobalDB::Put` on a registered key pattern directly — only through `SecureCrdt::ProposeValue`/`AddSignature`. Consider a debug-only assertion in `SecureCrdt` that logs (or in test builds, fails) if a registered key is observed being written outside the wrapper (detectable via the filter callback's `RegistryUpdateReceived`-style post-accept hook, comparing writer identity if available). |
| Duplicate-signature padding to fake quorum | Tampering | Already mitigated by `multisig::EvaluateQuorum`'s dedup-before-verify-per-address logic (Phase 8, shipped) — SecureCRDT must not re-implement counting itself, always delegate. |
| Replaying an old valid signature against a new proposed value at the same `base_key` | Tampering | Signatures must be verified against the CURRENT value's canonical bytes (`payload` parameter to `EvaluateQuorum`), not just "does this address have any valid signature stored" — `ReadIfQuorum`'s sketch above passes the current value's bytes as payload each time, which correctly invalidates stale signatures when the value changes. |
| Malformed/oversized payload causing crash in a third-party `ISignedCRDTData` implementer's codec | Denial of Service | `Verify()`/deserialization must run in a `try`/outcome-result-guarded path, mirroring `DeserializeRegistryUpdate`'s error-return-on-parse-failure pattern rather than throwing. |

## Sources

### Primary (HIGH confidence — direct file reads, this session)
- `src/crdt/globaldb/globaldb.hpp` — full API surface (Put/Get/Remove/QueryKeyValues/filter registration/topics)
- `src/crdt/hierarchical_key.hpp`, `src/crdt/impl/hierarchical_key.cpp` — full `HierarchicalKey` implementation
- `src/multisig/MultiSig.hpp` — full API (`VerifyPayloadSignature`, `EvaluateQuorum`, `QuorumResult`)
- `src/blockchain/ValidatorRegistry.cpp` L1225-1350 — `RegisterFilter`, `FilterRegistryUpdate`, `RegistryUpdateReceived`, `ComputeUpdateSigningBytes` (exact reference implementation to generalize)
- `src/blockchain/ValidatorRegistry.hpp` L370-400 — `RegistryKey()`/`ValidatorTopic()` constants pattern
- `src/account/InputValidators.hpp` — `IInputValidator::Register/UnregisterIf/Get` static-registry precedent
- `src/crdt/crdt_datastore.hpp` L61-63, L221-223 — `CRDTElementFilterCallback`/`CRDTNewElementCallback`/`CRDTDeletedElementCallback` type aliases
- `src/crdt/crdt_data_filter.hpp` L30 — `ElementFilterCallback` signature
- `src/crdt/crdt_callback_manager.hpp` L27-39 — `NewDataPair`/`NewDataCallback`/`DeletedDataCallback` signatures
- `src/crdt/proto/delta.proto` L10-15 — `pb::Element` field layout
- `src/multisig/CMakeLists.txt`, `test/src/multisig/CMakeLists.txt` — build wiring precedent for a CRDT-independent component
- `src/crdt/globaldb/CMakeLists.txt` — `crdt_globaldb` target's link dependencies
- `src/CMakeLists.txt`, `test/src/CMakeLists.txt` — confirmed `securecrdt` subdirectory not yet added to either
- `test/src/crdt/globaldb_integration.cpp` L1-140 — the only real-`GlobalDB` test fixture found in the repo (full pubsub/libp2p node startup)
- `.planning/phases/09-securecrdt-layer/09-CONTEXT.md`, `.planning/REQUIREMENTS.md`, `.planning/STATE.md` — phase scope and locked decisions

### Secondary / Tertiary
None used — all findings this pass came from direct codebase reads (HIGH confidence); no
WebSearch or Context7 lookups were needed since this phase builds entirely on in-repo
primitives with no external library research required.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new dependencies, all components read directly from source
- Architecture: HIGH — patterns generalized from verified, existing code (`ValidatorRegistry`, `InputValidators`)
- Pitfalls: MEDIUM-HIGH — filter pattern-matching engine (Pitfall 1 / A1) needs one more targeted read of `crdt_data_filter.cpp` before planning locks in the exact filter-registration strategy; everything else HIGH

**Research date:** 2026-07-23
**Valid until:** No expiry pressure — all findings are pinned to current in-repo source, not external/versioned APIs. Re-verify only if `GlobalDB`, `HierarchicalKey`, or `MultiSig.hpp` change before this phase is implemented.
