---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind-
reviewed: 2026-09-02T00:00:00Z
depth: standard
files_reviewed: 28
files_reviewed_list:
  - example/node_test/network_config.json
  - src/account/GeniusNode.cpp
  - src/account/GeniusNode.hpp
  - src/account/TransactionManager.cpp
  - src/account/TransactionManager.hpp
  - src/account/EscrowTransaction.hpp
  - src/blockchain/ValidatorRegistry.cpp
  - src/blockchain/ValidatorRegistry.hpp
  - src/blockchain/impl/Blockchain.cpp
  - src/networkregistry/NetworkRegistry.cpp
  - src/networkregistry/NetworkRegistry.hpp
  - src/peerregistry/PeerRegistry.hpp
  - src/processing/impl/TaskKeys.hpp
  - src/processing/impl/TaskQueueImpl.cpp
  - src/processing/impl/processing_core_impl.cpp
  - src/processing/impl/processing_subtask_result_storage_impl.cpp
  - src/securecrdt/QuorumThresholdValidation.hpp
  - src/securecrdt/SecureCrdtRegistry.hpp
  - src/trustedpeer/TrustedPeerRegistry.cpp
  - src/trustedpeer/TrustedPeerRegistry.hpp
  - test/src/account/network_config_private_network_test.cpp
  - test/src/account/private_network_registry_binding_test.cpp
  - test/src/blockchain/validator_registry_scope_test.cpp
  - test/src/networkregistry/network_registry_test.cpp
  - test/src/peerregistry/peer_registry_test.cpp
  - test/src/processing/processing_core_gating_test.cpp
  - test/src/processing/task_keys_scope_test.cpp
  - test/testutil/genius_node_test_access.hpp
findings:
  critical: 1
  warning: 5
  info: 4
  total: 10
status: issues_found
---

# Phase 15: Code Review Report

**Reviewed:** 2026-09-02
**Depth:** standard
**Files Reviewed:** 28 (plus supporting headers/CMakeLists listed in `<files_to_read>`)
**Status:** issues_found

## Summary

The phase's core scoping work is largely sound: `TaskKeys` scope helpers are byte-stable for
the public scope and branch every processing key/topic under `/chain/<id>`; `ValidatorRegistry`
scoping keeps public identifiers byte-identical and is verified three-way disjoint; the
`NetworkMembershipPayload` codec parses defensively (count/size cross-checks prevent
out-of-bounds reads); quorum floor math (`StrictMajorityQuorumFloor`) is correct; and the
processing-host gating composition (Noise-only + gater + eager `PskValidationError`) matches
the vendored injector contract. The descoped items (gater allow-list, membership enforcement)
were treated as owner decisions and not flagged.

However, the review found one fail-open chain in the private-network provisioning path
(CR-01), a permanent busy-spin thread in `NetworkRegistry`'s change-callback machinery, a
registration-clobbering failure path in `NetworkRegistry::New`, and an ingest-filter gap that
leaves `network-registry/<id>` elements unverified at CRDT ingest (unlike TPR/BurnConfig keys).

Per phase instructions: absence of libp2p allow-list enforcement (15-04 skip, 15-05/15-08
descope) was NOT flagged, and the documented no-genesis READY stall was not re-reported.

## Critical Issues

### CR-01: WriteNetworkConfig emits invalid JSON for the documented swarm-key `network_key` format, silently downgrading the node to PUBLIC on reload

**File:** `src/account/GeniusNode.cpp:225-247` (writer), `src/account/GeniusNode.hpp:160-163` (contract), `src/account/GeniusNode.cpp:1597-1603` (reload consequence)
**Issue:** `GeniusNode::WriteNetworkConfig` documents `network_key` as *"swarm-key text, or
base16/base64-encoded 32-byte PSK"*, and its escaping loop only escapes `\\` and `"`. The
canonical go-ipfs swarm-key text (`/key/swarm/psk/1.0.0/\n/base16/<64 hex>\n`) contains
**literal newline bytes** (the vendored `Psk::fromSwarmKeyText` this project links against
explicitly parses and tolerates them — `thirdparty/libp2p/include/libp2p/security/pnet/psk.hpp:39-46`).
Raw control characters are illegal inside JSON strings, so the helper writes a file that
rapidjson cannot parse. On the next start, `LoadNetworkConfig` hits the parse-error branch
(`GeniusNode.cpp:1599-1603`), logs an error, and **returns default settings with
`settings.valid == true`** — no `network_key`, no `private_network_id`. A node the operator
provisioned as private therefore boots as a fully public node: no PSK boundary on gossip or
the per-subtask processing host, public validator-registry identifiers, public CRDT paths.
This is a fail-open of exactly the D-01/D-08 misroute scenario the phase's validation was
added to prevent (`network_key` without `private_network_id` "would run a PSK-isolated node
writing private-intent data into PUBLIC CRDT paths"); a corrupt config bypasses all of it
because the parse failure short-circuits before any identity validation runs. The current
tests only round-trip the newline-free bare-base16 form, so the gap is untested.
**Fix:** Escape (or reject) all JSON string-unsafe characters in the writer, and make the
parse failure fail-closed:

```cpp
// GeniusNode.cpp, WriteNetworkConfig escaping loop — handle control chars:
for ( char c : network_key )
{
    switch ( c )
    {
        case '\\': escaped += "\\\\"; break;
        case '"':  escaped += "\\\""; break;
        case '\n': escaped += "\\n";  break;
        case '\r': escaped += "\\r";  break;
        case '\t': escaped += "\\t";  break;
        default:
            if ( static_cast<unsigned char>( c ) < 0x20 )
            {
                escaped += fmt::format( "\\u{:04x}", static_cast<unsigned>( c ) );
            }
            else { escaped += c; }
    }
}
```

```cpp
// GeniusNode.cpp, LoadNetworkConfig — treat unreadable/unparseable config as fatal
// (mirrors the private_network_id fail-closed posture):
std::stringstream buffer;
buffer << config_file.rdbuf();
rapidjson::Document config_json;
config_json.Parse( buffer.str().c_str() );
if ( config_json.HasParseError() || !config_json.IsObject() )
{
    GeniusNodeLogger()->error( "network_config.json is unreadable or invalid JSON - refusing to start" );
    settings.valid = false;   // InitNetwork() then fails closed (GeniusNode.cpp:1920-1923)
    return settings;
}
```

## Warnings

### WR-01: Unreadable or unparseable `network_config.json` loads public defaults instead of failing closed

**File:** `src/account/GeniusNode.cpp:1588-1603`
**Issue:** Both the missing-file branch (`Could not read network config file`) and the
parse-error branch return `settings` with `valid == true`, so `InitNetwork` proceeds with
public-mode defaults. Independently of CR-01's writer bug, any hand-edited/truncated/corrupt
config (or a missing file on a node that was provisioned private) silently boots public,
defeating the malformed-`private_network_id` and half-provisioned-pair checks added this
phase (they run only after a successful parse). The missing-file branch is a long-standing
behavior, but the phase made identity misconfiguration fatal everywhere else; this is the one
remaining fail-open path.
**Fix:** Same `settings.valid = false` treatment as CR-01 for the parse-error branch. For the
missing-file branch, at minimum fail closed when `private_network_id`/`network_key` were
expected (or always — the file is a required provisioning artifact per D-01).

### WR-02: `NetworkRegistry::RefreshLoop` busy-spins forever after the first change notification (`refresh_pending_` never cleared)

**File:** `src/networkregistry/NetworkRegistry.cpp:437-470` (set at 417, predicate at 443-447)
**Issue:** The datastore callback sets `refresh_pending_ = true` (line 417) and the refresh
thread's wait predicate is `refresh_pending_ || refresh_stopping_` (lines 443-447), but no
code ever stores `false` back into `refresh_pending_`. Once the first `network-registry/<id>`
(or `sig/<addr>`) element arrives, `wait()` returns immediately on every iteration and the
loop spins continuously calling `TryConfirm()` → `SecureCrdt::ReadIfQuorum` →
`RetainAuthorizedLegacySignatures` (a `QueryKeyValues` scan of the datastore on every spin,
plus `logger_->warn` spam whenever TryConfirm errors). That is a permanent 100%-CPU thread
with continuous GlobalDB reads for as long as the registry lives. The production wiring
currently passes `global_db = nullptr` (GeniusNode calls the 5-arg `New`), so the thread is
not started in-node today, but the machinery is public library API and is exercised by
`network_registry_test.cpp`'s `CacheRefreshViaCrdtChangeCallback`.
**Fix:** Clear the flag once the notification has been consumed:

```cpp
if ( refresh_stopping_.load( std::memory_order_acquire ) )
{
    return;
}
refresh_pending_.store( false, std::memory_order_release ); // drain-once semantics
lock.unlock();
auto confirmed = TryConfirm();
```

(Re-set it if `TryConfirm` returns `success(false)` and you want retry semantics, but then
re-wait with a timeout instead of spinning.)

### WR-03: `NetworkRegistry::New` failure path silently unregisters a live registry for the same network id

**File:** `src/networkregistry/NetworkRegistry.cpp:364-367, 375-396, 579-602`
**Issue:** `SecureCrdtRegistry::Register` **replaces** an existing entry for the same pattern
and returns `false` to signal "replaced" (`src/securecrdt/SecureCrdtRegistry.hpp:121-137`).
`RegisterSignerSetSource` forwards that `false`, `New` maps it to
`outcome::failure(std::errc::file_exists)`, the `instance` shared_ptr is destroyed, and
`~NetworkRegistry` → `Unregister()` → `UnregisterIf(pattern, &registry_token_)` removes the
**newly inserted** entry (its token matches). Net effect of constructing a second
`NetworkRegistry` for an already-registered network id: the first, still-live registry has
its policy entry destroyed — every subsequent `ProposeValue`/`AddSignature`/`ReadIfQuorum`
on that key fails with `UNREGISTERED_KEY`, bricking the existing registry with no error
surfaced to its owner. Also, `std::errc::file_exists` is a misleading error code (the header
documents `QUORUM_THRESHOLD_BELOW_FLOOR` for failures).
**Fix:** Make the duplicate case non-destructive: resolve the existing entry first and fail
without registering, or snapshot and restore the replaced entry on the failure path:

```cpp
const auto pattern = EscapeRegex( base_key_.GetKey() );
if ( secure_crdt_->Registry().Resolve( base_key_.GetKey() ) )
{
    return outcome::failure( std::errc::address_in_use ); // already registered, entry untouched
}
```

### WR-04: `network-registry/<id>` elements never receive a SecureCrdt ingest filter (RegisterFilters runs before NetworkRegistry::New in both wiring paths)

**File:** `src/account/GeniusNode.cpp:1037 vs 1059`; `src/securecrdt/SecureCrdt.cpp:653-693`; `src/crdt/impl/crdt_datastore.cpp:417`
**Issue:** `SecureCrdt::RegisterFilters()` snapshots `AllEntries()` and installs a CRDT
element filter per registered key pattern. In the non-controller path it runs at
`GeniusNode.cpp:1037`, immediately after TPR/BurnConfig are registered, and the
`NetworkRegistry` is constructed later at line 1059 — so its `network-registry/<id>` pattern
gets **no** `FilterSecureCrdtUpdate` filter. The controller path has the same ordering
(`TrustStartupController.cpp:160` calls `RegisterFilters` inside `New`; NetworkRegistry is
constructed after the controller reports ready). Because `CRDTDataFilter` is constructed
with `accept_by_default = true` (`crdt_datastore.cpp:417`), remote-originated membership
payloads and `sig/<addr>` children are accepted into the local datastore **without** the
canonical-signer/quorum ingest verification that every other SecureCrdt-registered key
(trusted-peer-registry, burn-config) receives. `TryConfirm`'s `ReadIfQuorum` still gates
application to the cache, so this is not an authorization bypass, but it is a
defense-in-depth asymmetry: a same-transport peer can push unfiltered/unvalidated values
into the branch that the phase calls "the authorization state for the private network"
(e.g. overwriting the proposed record bytes — a griefing/reset vector for pending
bootstrap confirmations).
**Fix:** Re-run `secure_crdt_->RegisterFilters()` after a successful `NetworkRegistry::New`
(it is idempotent per pattern), or give `NetworkRegistry::New` a hook that registers its own
ingest filter the way BurnConfig's wiring ends up covered.

### WR-05: `entry.peer_registry = shared_from_this()` creates an ownership cycle — an un-Unregistered `NetworkRegistry` can never be destroyed

**File:** `src/networkregistry/NetworkRegistry.cpp:393`
**Issue:** The `SecureCrdtRegistry` entry holds a strong `shared_ptr<PeerRegistry>` back to
the registry, while the registry holds `secure_crdt_` (which owns the registry map). The
refcount can therefore never reach zero until `Unregister()` explicitly removes the entry —
meaning the destructor's `Unregister()` call (`~NetworkRegistry`, line 262) is unreachable
for any instance whose owner simply drops the pointer. Every such instance leaks its whole
closure: `SecureCrdt`, `TrustedPeerRegistry`, and (when enabled) the `GlobalDB` change
callback plus the refresh thread — the callback then fires into a registry whose
`weak_ptr` still locks, forever. `TrustedPeerRegistry.cpp:601` shares this convention (one
global instance, explicitly unregistered in `ShutdownNodePolicyServices`), but
`NetworkRegistry` is the new *per-network, app-constructible* type whose documented factory
contract does not require `Unregister()`. GeniusNode's teardown handles it explicitly
(`GeniusNode.cpp:2262-2277`), so this is a foot-gun for the future SuperGenius-side consumers
rather than an in-node leak.
**Fix:** Store a `weak_ptr` in the entry (`entry.peer_registry` is only used for identity
comparison in tests — `peer_registry_test.cpp:95-98` compares `.get()` after resolving via
`Resolve()`, which could lock-and-return instead), or document `Unregister()` as mandatory
and make `New()`'s contract state that dropping the last pointer without `Unregister()`
leaks the SecureCrdt graph.

## Info

### IN-01: `NetworkRegistry::Unregister()` is not safe for concurrent invocation (racy check-then-join)

**File:** `src/networkregistry/NetworkRegistry.cpp:579-602`
**Issue:** `Unregister()` can run from both an explicit call and the destructor (GeniusNode
calls it explicitly at `GeniusNode.cpp:2264` and then resets the pointer). The
`if (refresh_thread_.joinable()) refresh_thread_.join();` pair is not atomic — two
concurrent callers can both observe `joinable()` and double-`join()`, which is
`std::terminate()`. Current call sites are sequential, so this is latent.
**Fix:** Guard `Unregister()` with a `std::once_flag` or an atomic
`unregister_started_.exchange(true)` early-return.

### IN-02: `WriteNetworkConfig` performs no pairing/encoding validation, so it can emit configs its own loader refuses

**File:** `src/account/GeniusNode.cpp:203-270`
**Issue:** The writer happily emits `network_key` without `private_network_id` (or a
malformed id); `LoadNetworkConfig` then fails the node at startup (D-01). Tests/examples
using the helper with a half-provisioned combination get a config that bricks the node with
no write-time error. Cheap to reject at write time with the same rules the loader applies.
**Fix:** Return `Error::INVALID_NODE_TYPE`-style failure from `WriteNetworkConfig` when
`network_key.empty() != private_network_id.empty()`, or when a non-empty id fails the
66-char 0x-hex shape check.

### IN-03: Private-network consensus traffic still uses public identifiers (topic and `/cert/` keys)

**File:** `src/blockchain/impl/Blockchain.cpp:172-186` (`consensus_topic=""` → public full-node topic), `src/blockchain/ValidatorRegistry.cpp:955` (`/cert/<subject_hash>` unscoped)
**Issue:** D-09 scoped the validator registry's key/topic/CID, but `ConsensusManager`
publishes on the empty-default topic (resolved to `SuperGNUSNode.TestNet.FullNode`) and
batch certificates are stored under unscoped `/cert/<hash>` keys shared with the public
namespace. Cross-network contamination requires breaking the pnet transport boundary (or a
256-bit subject-hash collision, which `BuildRegistryFromBatchCertificates` then rejects via
`registry_cid`/`epoch` checks), so this is acceptable today — but the scoping story is
incomplete and worth recording before private networks rely on consensus-batch features.
**Fix:** Thread `network_scope_` into the consensus topic and the `/cert/` prefix in a
follow-up, mirroring `ValidatorTopicValue()`.

### IN-04: `HasPeerIdMultihashPrefix` accepts only `Qm…` and `12D3KooW…` peer ids

**File:** `src/networkregistry/NetworkRegistry.cpp:25-29, 244`
**Issue:** `NetworkMembershipPayload::Verify` structurally rejects any peer id not starting
with the identity-hash (`Qm`) or Ed25519 (`12D3KooW`) multihash prefixes. Other libp2p
identity codecs (e.g. ECDSA keys, or future identity multihashes) would be rejected from
membership records entirely — a silent fail-close for legitimate members. Documented as a
heuristic; fine for the current signer population, but it couples record validity to a
prefix allow-list that is not part of the wire format.
**Fix:** Validate length/base58 charset instead of prefixes, or centralize the accepted
multihash prefix set next to the PeerId codec so it evolves with it.

---

Scope notes for the fixer: the `NetworkMembershipPayload` codec (`FromBytes`/`ParseCountLine`)
was checked for bounds/count mismatches and is safe (line-count cross-check at
`NetworkRegistry.cpp:223-224` prevents the `assign` iterators from going out of range;
`stoull` inputs are digit-checked and length-capped). `StrictMajorityQuorumFloor` was
verified for n = 0..200 against ceil(0.51·n). The `ValidatorRegistry` scoped identifiers were
verified to remain disjoint under `std::regex_match` full-match semantics
(`crdt_data_filter.cpp:108`, `crdt_callback_manager.cpp:169`) — the public pattern cannot
match a scoped key.

_Reviewed: 2026-09-02_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
