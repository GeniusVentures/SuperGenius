# Phase 12: ValidatorRegistry Migration - Context

**Gathered:** 2026-07-27
**Status:** Ready for planning

<domain>
## Phase Boundary

`ValidatorRegistry`'s signature verification is migrated from hand-rolled `GeniusAccount::VerifySignature` calls onto the shared `multisig::VerifyPayloadSignature`/`multisig::EvaluateQuorum` primitives from Phase 8. This is a **narrow, signature-verification-only reuse** — `ValidatorRegistry` does NOT adopt `ISignedCRDTData`/`SecureCrdtRegistry` (Phase 9's framework), keeps its own CRDT storage scheme (`RegisterFilter`/`FilterRegistryUpdate`/`RegistryUpdateReceived` on its own `GlobalDB`/topic), and keeps its own weighted-quorum/certificate-driven update pipeline entirely untouched. No new CRDT filter/callback registration is introduced by this phase, and no new `SecureCrdt` instance is constructed.

This scope was deliberately narrowed during discussion: the originally-drafted MIG-05 ("migrate onto `ISignedCRDTData`, reusing `SecureCrdt`") was rejected as too deep a change relative to its value — `ValidatorRegistry`'s real quorum decision is weighted and made upstream in certificate/vote finalization, which `SecureCrdt`/`MultiSig` structurally cannot support without extending already-shipped Phase 8/9 interfaces. The user chose the smaller, still-valuable slice: reuse the multi-sig component only where `ValidatorRegistry` does raw signature verification.

</domain>

<decisions>
## Implementation Decisions

### Scope: signature verification only, not storage/quorum-counting
- **D-01 (corrected during planning-prep — pattern mapper caught this):** The migration touches exactly ONE call site: `VerifyUpdate`'s genesis path (`ValidatorRegistry.cpp:1387-1406`), currently a loop over `update.signatures()` checking `validator_id() == genesis_authority_` + `GeniusAccount::VerifySignature`. This becomes a call to `multisig::VerifyPayloadSignature` (confirmed byte-layout-compatible — `multisig::VerifyPayloadSignature` delegates directly to `GeniusAccount::VerifySignature` with identical parameter shapes, `MultiSig.cpp:15-20` — pure namespace substitution, no adapter needed). `StoreGenesisRegistry`'s signing call (`cpp:621-625`) is NOT part of this migration — it invokes an injected `sign` callback (not `GeniusAccount::VerifySignature`, and not verification at all), and `multisig::MultiSig.hpp` exposes no signing primitive to substitute in. Originally CONTEXT.md listed this as a second call site in error; corrected here before planning.
- **D-02:** `ValidatorRegistry`'s weighted-quorum machinery (`QuorumThreshold`, `IsQuorum`, `EvaluateSlotQuorum`, certificate/vote finalization in `ExtractCertificateVotes` and friends) is explicitly OUT of scope — it stays exactly as-is. `multisig::EvaluateQuorum` is unweighted and cannot represent it; no attempt is made to force a fit.
- **D-03:** `ValidatorRegistry` does NOT adopt `ISignedCRDTData`, `SecureCrdtRegistry`, or `SecureCrdt`. Its `RegisterFilter`/`FilterRegistryUpdate`/`RegistryUpdateReceived`/`cache_mutex_` machinery on its own `GlobalDB`/`ValidatorTopic()` is untouched. This is the key rejection from discussion: the originally-scoped ISignedCRDTData migration was judged too deep relative to its value, given the weighted-quorum mismatch.

### Regression risk posture (Phase 11's open multi_account_test issue)
- **D-04:** Proceed with this phase without first root-causing Phase 11's still-open `multi_account_test` regression. Since this phase adds zero new CRDT filter/callback registration and zero new `SecureCrdt`/`GlobalDB` construction (per D-03), the regression's blast radius should be unaffected by this phase's changes — but this must be verified empirically, not assumed.
- **D-05 (exit gate):** Phase 12 verification must run the `multi_account_test` suite 5-10 times consecutively clean before the phase is considered done, mirroring the bar `11-VERIFICATION.md` already set. If failure rate changes (better or worse) versus Phase 11's baseline (~2/4 observed), document that honestly — do not attribute a regression-rate change to "pre-existing flakiness" without evidence, per standing project directive on honest causal attribution.

### Build wiring
- **D-06:** `src/blockchain/impl/CMakeLists.txt`'s `blockchain_genesis` target gains a link to `multisig` (or `securecrdt`, which already transitively links `multisig` — planner's call on which is more appropriate given D-03 excludes `securecrdt`'s actual API usage; direct `multisig` link is likely correct and cleaner). This is the first time `blockchain_genesis` depends on the Phase 8 multi-sig stack.

### Claude's Discretion
- Whether to link `multisig` directly or reuse the existing `securecrdt` target's transitive link — given D-03 excludes any `SecureCrdt` API usage, a direct `multisig` link is the honest dependency and is expected to be the planner's choice, but not locked.
- Exact signature/byte-layout compatibility check between `ComputeUpdateSigningBytes`'s payload format and `multisig::VerifyPayloadSignature`'s expected payload shape (both should be raw byte buffers verified via the same underlying secp256k1 verify — confirm during planning/research that no format adapter is needed).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Current signature verification call sites (the exact surface being changed)
- `src/blockchain/ValidatorRegistry.cpp:1387-1406` — genesis-path signature loop in `VerifyUpdate`, currently `GeniusAccount::VerifySignature`-based single-signer check against `genesis_authority_`.
- `src/blockchain/ValidatorRegistry.cpp:621-625` — `StoreGenesisRegistry`'s signing call (`account_->Sign(data)`), the producer side of the signature `VerifyUpdate` checks.
- `src/blockchain/ValidatorRegistry.cpp:1329-1346` — `ComputeUpdateSigningBytes`, the canonical signing-bytes construction (`RegistrySigningPayload{registry, prev_registry_hash}` protobuf) fed into both the sign and verify sides above.

### MultiSig primitive being reused (already shipped, Phase 8)
- `src/multisig/MultiSig.hpp`/`.cpp` — `VerifyPayloadSignature(address, signature, payload)`, `EvaluateQuorum(signer_set, threshold, collected_signatures, payload) -> QuorumResult`. Only `VerifyPayloadSignature` is in scope per D-01/D-02; `EvaluateQuorum` is NOT used by this phase since `ValidatorRegistry`'s real quorum is weighted (D-02).

### Out-of-scope machinery (must remain untouched — read only to confirm boundaries, not to modify)
- `src/blockchain/ValidatorRegistry.cpp:311-334` (`QuorumThreshold`/`IsQuorum`, weighted), `:336-491` (`EvaluateSlotQuorum`/`EvaluateSlotQuorumStatic`), `:1408+` (`VerifyUpdate`'s non-genesis certificate path), `:1671+` (`ExtractCertificateVotes`) — all weighted-quorum/certificate machinery, confirmed structurally incompatible with `multisig::EvaluateQuorum` per D-02, left exactly as-is.
- `src/blockchain/ValidatorRegistry.cpp:1231-1261` (`RegisterFilter`), `:1263-1284` (`FilterRegistryUpdate`), `:1286-1327` (`RegistryUpdateReceived`) — CRDT storage/callback plumbing, untouched per D-03.
- `src/securecrdt/*`, `src/trustedpeer/*`, `src/account/BurnConfig.*` — Phase 9-11 framework, confirmed NOT consumed by this phase per D-03.

### Regression context (must read before verification)
- `.planning/phases/11-burnconfig-quorum-wiring/11-VERIFICATION.md` — documents the open `multi_account_test` regression (SEGFAULT/assertion failures, ~2/4 post-fix failure rate), the `crdt_set.cpp` `PutElems` lock-scope fix already applied, and the explicit "not fully root-caused" status.
- `test/src/multiaccount/multi_account_sync.cpp` — the regression's test surface; `ValidatorRegistryTest::MissingRegistryBlockIsFetchedFromPeerByCid` (`:256-333`) and `ConfigureConsensus` (`:55-73`) are the most directly relevant existing tests to rerun as the D-05 exit gate.

### Build wiring
- `src/blockchain/impl/CMakeLists.txt:10-31` — `blockchain_genesis` target's current link list (confirmed via grep: no `securecrdt`/`trustedpeer`/`multisig` link today).
- `src/securecrdt/CMakeLists.txt` — confirms `securecrdt` already publicly links `multisig` (reference only, per D-06's discretion note).

### Project-level context
- `.planning/PROJECT.md` — v1.1 milestone Key Decisions.
- `.planning/REQUIREMENTS.md` §"Milestone v1.1" — original MIG-05/MIG-06 text describes the broader `ISignedCRDTData` migration; this phase's actual scope is narrower per D-01/D-03 above and REQUIREMENTS.md should be read as superseded by this CONTEXT.md on that point (planner should note the discrepancy, not silently follow the stale broader wording).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/multisig/MultiSig.hpp`/`.cpp` — already shipped, this phase's only new dependency.

### Established Patterns
- None new — this is a like-for-like call-site substitution, not a new pattern.

### Integration Points
- Depends on Phase 8 (`MultiSig`) only, already shipped. Explicitly does NOT depend on or touch Phase 9/10/11 (`SecureCrdt`, `TrustedPeerRegistry`, `BurnConfig`).

</code_context>

<specifics>
## Specific Ideas

The user's framing during discussion: the value of this phase is narrowly "have ValidatorRegistry's signatures use the multi-sig component" — not a full architectural migration. When presented with the finding that the full `ISignedCRDTData`/`SecureCrdt` migration was structurally blocked by the weighted-vs-unweighted quorum mismatch, the user explicitly said the deep version could be dropped entirely if it was "too much of a change" — landing on this narrow signature-verification-only slice as the version worth keeping, once confirmed to be genuinely small (2 call sites + 1 CMake link, no CRDT/architecture change).

</specifics>

<deferred>
## Deferred Ideas

- Full `ISignedCRDTData`/`SecureCrdt` migration of `ValidatorRegistry`'s storage/callback plumbing — rejected as too deep given the weighted-quorum mismatch; would require extending already-shipped Phase 8 (`MultiSig`) or Phase 9 (`ISignedCRDTData`) interfaces to carry signer-weight information, which was judged not worth it for this milestone.
- Weighted quorum support in `multisig::EvaluateQuorum` (e.g. an optional per-signer weight map) — would be a prerequisite for ever doing the deeper migration above; not pursued now.
- Dedicated root-cause investigation of Phase 11's `multi_account_test` regression (profiling/instrumenting the multi-node construction sequence) — still an open follow-up item independent of this phase, tracked since Phase 11.

</deferred>

---

*Phase: 12-ValidatorRegistry Migration*
*Context gathered: 2026-07-27*
