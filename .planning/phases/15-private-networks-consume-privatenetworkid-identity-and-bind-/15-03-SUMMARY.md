---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "03"
subsystem: auth
tags: [securecrdt, networkregistry, trustedpeer, peerregistry, quorum, cplusplus, cmake]

# Dependency graph
requires:
  - phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
    plan: "02"
    provides: sgns::peerregistry::PeerRegistry contract, SecureCrdtRegistryEntry::peer_registry association (D-04)
  - phase: 13-close-v1-1-trusted-peer-genesis-quorum-policy-and-production
    provides: TrustedPeerRegistry root trust domain, SecureCrdt/SecureCrdtRegistry policy machinery
provides:
  - sgns::networkregistry::NetworkRegistry — SecureCRDT-backed per-privateNetworkId membership registry (D-06) implementing PeerRegistry
  - NetworkMembershipPayload (ISignedCRDTData) — secret-free membership records (D-03) carrying the PeerId allow-list for the 15-05 gater
  - securecrdt::ValidateQuorumThreshold / StrictMajorityQuorumFloor — ceil(0.51*N) strict-majority floor helper
  - NetworkRegistry::New 5-arg call shape preserved (extra args are defaulted trailing params) for 15-05's GeniusNode wiring
affects: [15-05-gater-membership, securecrdt, trustedpeer]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Dual-identity membership records: gater-facing libp2p PeerId list + signer-facing 128-hex member addresses in one payload, because SecureCrdt verifies signatures only against hex account addresses"
    - "Off-callback CRDT refresh: a NewElementCallback must not synchronously run ReadIfQuorum (it prunes stale sigs via GlobalDB::Remove) — flag + notify a dedicated refresh thread instead"
    - "Regex-escaped per-network base keys: SecureCrdtRegistry::Register compiles key patterns as regexes, so 'network-registry/<id>' keys are escaped before Register/UnregisterIf"

key-files:
  created:
    - src/networkregistry/NetworkRegistry.hpp
    - src/networkregistry/NetworkRegistry.cpp
    - src/networkregistry/CMakeLists.txt
    - test/src/networkregistry/network_registry_test.cpp
    - test/src/networkregistry/CMakeLists.txt
  modified:
    - src/securecrdt/QuorumThresholdValidation.hpp
    - src/CMakeLists.txt
    - test/src/CMakeLists.txt
    - .planning/phases/15-private-networks-consume-privatenetworkid-identity-and-bind-/deferred-items.md

key-decisions:
  - "Payload carries network_signers (128-hex member addresses) alongside the PeerId list: the plan's literal post-confirmation signer set (base58 PeerIds) is mechanically impossible — SecureCrdt::ResolveLegacySignerSnapshot rejects non-hex signer sets and multisig verification is secp256k1 over 128-hex addresses. Schema choice is within 15-CONTEXT's explicit discretion ('Exact NetworkRegistry payload schema... provided bootstrap uses TPR majority and later updates cannot be unilateral'); D-03 (no secrets) and the 15-05 GetCurrentPeers()->PeerId-base58 contract both preserved"
  - "New's extra params (initial_network_signers, pnet_key_fingerprint, global_db) are defaulted TRAILING args so 15-05's documented 5-arg call compiles unchanged; a registry constructed without member signers fail-closes all post-confirmation updates (empty signer set)"
  - "Change-callback refresh is asynchronous (flag + condition variable + dedicated refresh thread): the synchronous BurnConfig-pattern callback ran ReadIfQuorum's destructive sig-pruning re-entrantly inside datastore Put callbacks and crashed once the signer authority transitioned"
  - "ValidateQuorumThreshold added to QuorumThresholdValidation.hpp (ceil(0.51*N) floor via ValidateThresholdAtFloor) because the plan's interface block and verification grep reference a symbol that did not exist"

patterns-established:
  - "Trust-transitioning SecureCRDT registries need off-callback cache refresh (authority transitions make sig-pruning reachable from datastore callbacks — BurnConfig never hits this only because its signer set never transitions)"
  - "Per-network base keys registered with regex escaping; explicit Unregister() remains the teardown path (TPR entry-cycle precedent)"

requirements-completed: [D-03, D-05, D-06, PNET-NETREG]

# Metrics
duration: 86min
completed: 2026-09-01
---

# Phase 15 Plan 03: NetworkRegistry Summary

**SecureCRDT-backed per-privateNetworkId membership registry (D-06): bootstrap record confirmed only by a strict majority of the global TrustedPeerRegistry, then cached self-governance at the network's own quorum — with secret-free records (D-03) and a 9-case test suite proving the trust lifecycle**

## Performance

- **Duration:** ~86 min (fresh worktree: submodule init, cmake configure, full dependency-cone build; plus a thirdparty-tree switch mid-run)
- **Started:** 2026-09-01T19:09:39Z
- **Completed:** 2026-09-01T20:35:10Z
- **Tasks:** 2/2
- **Files modified:** 9 (5 created, 4 modified)

## Accomplishments
- `sgns::networkregistry::NetworkRegistry` — a PeerRegistry-implementing child trust domain over `SecureCrdt` (no bespoke signature protocol): per-network base key `network-registry/<privateNetworkId>`, double quorum-floor validation at `New` (TPR bootstrap majority + self-governance), cached-only `ResolveSignerSet` (pre-confirm = TPR snapshot at TPR-majority, post-confirm = own member signers at own quorum; never re-enters ReadIfQuorum), `SeedBootstrap` (propose-only, no self-sign), `TryConfirm` (ReadIfQuorum → FromBytes → Verify → Apply → cache overwrite), `ProposeMembershipChange`/`SignMembershipChange`, `Unregister`, and a BurnConfig-pattern change-callback cache refresh
- `NetworkMembershipPayload` with an explicit line-based wire layout; `Verify()` is structural-only (non-empty unique PeerId-prefixed entries, unique 128-hex signers, version ≥ 1, fingerprint empty-or-hex) — never diffs against cached state; records carry only non-secret metadata
- `securecrdt::ValidateQuorumThreshold` + `StrictMajorityQuorumFloor` (ceil(0.51*N)) added to QuorumThresholdValidation.hpp
- 9-case test suite over a real single-node SecureCrdt fixture with real secp256k1 signers: TPR-majority bootstrap (under-signed never confirms within a bounded window), post-confirm self-governance (sub-quorum never confirms; member quorum confirms and switches the signer authority), unilateral self-admission rejected before persistence, PSK-sentinel absent from persisted records while version/fingerprint metadata is present, unconfirmed resolution returns the TPR snapshot, and async change-callback refresh confirms without explicit TryConfirm
- Full regression green: `ctest -R "network_registry|trustedpeer|securecrdt"` → 11/11 passed

## Task Commits

Each task was committed atomically:

1. **Task 1: NetworkRegistry core module** - `7d163bab` (feat)
2. **Task 1 follow-up fix: off-callback refresh** - `5bda5eed` (fix, discovered during Task 2 verification)
3. **Task 2: Bootstrap/self-governance/secret-exclusion tests** - `27b651e4` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified
- `src/networkregistry/NetworkRegistry.hpp` - NetworkRegistry + NetworkMembershipPayload (ISignedCRDTData); PeerRegistry overrides; refresh-thread machinery
- `src/networkregistry/NetworkRegistry.cpp` - bootstrap-from-TPR lifecycle, cached self-governance, double ValidateQuorumThreshold floors, regex-escaped registration, TryConfirm, async change-callback refresh
- `src/networkregistry/CMakeLists.txt` - library `networkregistry` (PUBLIC securecrdt/peerregistry/trustedpeer, PRIVATE hexutil), supergenius_install
- `src/securecrdt/QuorumThresholdValidation.hpp` - added ValidateQuorumThreshold + StrictMajorityQuorumFloor (additive)
- `src/CMakeLists.txt` - add_subdirectory(networkregistry)
- `test/src/networkregistry/network_registry_test.cpp` - 9 cases (3 payload codec/Verify, 6 registry lifecycle)
- `test/src/networkregistry/CMakeLists.txt` - network_registry_test target (trustedpeer link set + networkregistry)
- `test/src/CMakeLists.txt` - add_subdirectory(networkregistry)
- `deferred-items.md` - logged the BurnConfig latent re-entrancy hazard

## Decisions Made
- Dual-identity payload (`network_peers` PeerIds + `network_signers` 128-hex addresses) — see key-decisions; the defining constraint is that `GeniusAccount::VerifySignature`/`ResolveLegacySignerSnapshot` only accept 128-hex secp256k1 addresses as signers, so a PeerId-only signer set can never confirm anything
- `New` validates the network quorum floor over `initial_network_peers.size()` (plan-literal) AND over `initial_network_signers.size()` when signers are provisioned (correctness — the signers are the actual self-governance set)
- Apply() is a no-op (TPR convention): the cache overwrite lives in TryConfirm where the owning registry context exists
- Base keys are regex-escaped before Register/UnregisterIf because SecureCrdtRegistry compiles key patterns into regexes

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `securecrdt::ValidateQuorumThreshold` did not exist**
- **Found during:** Task 1 (the plan's interface block and verify grep reference it)
- **Issue:** QuorumThresholdValidation.hpp only had `ValidateMembershipQuorumThreshold` (N/2+1 floor); the plan requires the (N*51+99)/100 strict-majority floor under the name `ValidateQuorumThreshold`, and Task 1's automated verify greps for that symbol
- **Fix:** Added `ValidateQuorumThreshold` + `StrictMajorityQuorumFloor` to QuorumThresholdValidation.hpp as thin wrappers over the existing `ValidateThresholdAtFloor`; additive, header-only, no behavior change for existing callers
- **Files modified:** src/securecrdt/QuorumThresholdValidation.hpp
- **Verification:** ninja builds; existing securecrdt/trustedpeer suites pass unchanged
- **Committed in:** 7d163bab

**2. [Rule 3 - Blocking] Plan-as-written payload cannot satisfy self-governance: PeerId signer sets are unverifiable**
- **Found during:** Task 1 design (verified against SecureCrdt.cpp:137-170 ResolveLegacySignerSnapshot and GeniusSigner.cpp:95-145)
- **Issue:** The plan specifies payload `network_peers` as base58 PeerIds AND post-confirmation `ResolveSignerSet` = `{cached_network_peers_, threshold}` — but SecureCrdt rejects any signer-set member failing `IsHexAddress` (128-hex) with UNAUTHORIZED_SIGNER, and signatures are secp256k1 over the address-as-public-key. A base58 signer set can never AddSignature or meet quorum: tests 2 and 3 (and production self-governance) would be impossible
- **Fix:** Payload carries a second non-secret list `network_signers` (128-hex member account addresses) alongside the PeerId list; `GetCurrentPeers()` returns the PeerId list (15-05/D-07 contract intact) while post-confirmation `ResolveSignerSet` returns the member signers. Explicitly within 15-CONTEXT's discretion clause for the payload schema, and preserves both D-06 constraints (TPR-majority bootstrap, no unilateral updates) and D-03 (no secrets)
- **Files modified:** src/networkregistry/NetworkRegistry.hpp, src/networkregistry/NetworkRegistry.cpp
- **Verification:** SelfGovernanceAfterConfirm and SinglePeerCannotAdmitItself pass end-to-end with real signatures
- **Committed in:** 7d163bab

**3. [Rule 1 - Bug] Synchronous change-callback re-entrancy crashed the datastore**
- **Found during:** Task 2 (first test run: EXC_BAD_ACCESS inside a datastore Put callback)
- **Issue:** The BurnConfig-pattern `RegisterNewElementCallback` ran `TryConfirm` synchronously inside the callback; `ReadIfQuorum` → `RetainAuthorizedLegacySignatures` prunes stale signature children via `GlobalDB::Remove`, and executing that re-entrantly from inside a Put callback corrupted the datastore once the authority transition made the bootstrap TPR signatures stale. It also auto-confirmed mid-test, racing the plan's explicit-TryConfirm semantics
- **Fix:** The callback now only sets an atomic pending flag and notifies a condition variable; a dedicated refresh thread calls `TryConfirm` outside any datastore callback context. `Unregister` stops and joins the thread. (BurnConfig has the same latent pattern — logged to deferred-items.md, out of scope)
- **Files modified:** src/networkregistry/NetworkRegistry.hpp, src/networkregistry/NetworkRegistry.cpp
- **Verification:** All 9 cases + 11/11 regression suites pass, no crashes
- **Committed in:** 5bda5eed

**4. [Rule 1 - Bug] Iterator-across-temporaries UB in my own test assertion**
- **Found during:** Task 2 (EXC_BAD_ACCESS in TestBody — comparing `.end()` from one `GetCurrentPeers()` temporary against `.begin()` from another)
- **Issue:** Two separate `GetCurrentPeers()` calls produced two different vector copies; comparing iterators across them is UB and produced both a bogus failure and the earlier in-test segfault
- **Fix:** Single cached copy of the membership list before the find/end comparison
- **Files modified:** test/src/networkregistry/network_registry_test.cpp
- **Verification:** SinglePeerCannotAdmitItself passes deterministically
- **Committed in:** 27b651e4

---

**Total deviations:** 4 auto-fixed (2 blocking, 2 bugs)
**Impact on plan:** Deviations 1-2 were required for the plan to be implementable at all against the existing SecureCrdt hex/secp256k1 contract; 3-4 are correctness fixes discovered by the new tests. No scope creep; all D-06/D-03 must_haves hold.

## Environment Notes
- The thirdparty tree moved mid-execution: the build was initially configured against `/Users/henriqueklein/gnus/3rdparty` (per the wave instructions) and was reconfigured to `/Users/henriqueklein/gnus/thirdparty` (Sep 1 08:55 install, same pnet-era headers) on operator direction; all targets rebuilt and all suites re-run green against it
- Submodules initialized via `git submodule update --init ProofSystem SGProcessingManager evmrelay`; fresh `build/OSX/Release` Ninja tree with `BUILD_TESTING=ON`

## Issues Encountered
- None blocking beyond the deviations above. The known pre-existing no-genesis-READY failure (deferred-items #1) does not affect this plan — the suite uses a genesis-configured 3-peer TPR and performs no GeniusNode/WaitForReady assertions

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 15-05 consumes: `NetworkRegistry::New(secure_crdt_, trusted_peer_registry_, private_network_id_, network_bootstrap_peers_, threshold)` compiles unchanged (extra args are defaulted trailing params); `GetCurrentPeers()` returns cached PeerId base58 strings for the gater predicate; `Unregister()` + destructor ordering follow the TPR pattern (explicit Unregister required — the registry entry holds a self shared_ptr)
- Production self-governance requires member signing addresses: 15-05's wiring passes config-provisioned PeerIds only, which fail-closes post-confirmation updates (empty signer set) until a member-signer provision path (config field or TPR-signed record carrying signers) exists — flagged for the 15-05 executor
- All 11 related suites green: network_registry, trustedpeer (3), securecrdt (6), plus payload codec cases

## Self-Check: PASSED

- All 8 created/modified source + test files exist on disk
- Commits verified in git log: 7d163bab, 5bda5eed, 27b651e4
- Working tree clean (no unstaged task files, no untracked build output)
- `ctest -R "network_registry|trustedpeer|securecrdt"`: 11/11 passed
- `grep -c "network_key" src/networkregistry/NetworkRegistry.hpp` = 0; `grep -c "sleep_for" test/src/networkregistry/network_registry_test.cpp` = 0

---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-01*
