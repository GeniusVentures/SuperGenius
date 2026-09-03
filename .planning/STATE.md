---
gsd_state_version: 1.0
milestone: v1.1
milestone_name: Multi-Signature Secure CRDT Storage
current_phase: 15
status: executing
last_updated: "2026-09-03T19:07:58.318Z"
last_activity: 2026-09-03
progress:
  total_phases: 19
  completed_phases: 6
  total_plans: 68
  completed_plans: 52
  percent: 32
---

# State: SuperGenius — Multi-Signature Secure CRDT Storage

**Last updated:** 2026-07-20
**Milestone:** v1.1 — Multi-Signature Secure CRDT Storage
**Current Phase:** 15

## Project Reference

See: .planning/PROJECT.md (updated 2026-07-20)

**Core value:** A decoupled multi-signature component and secure CRDT storage layer let specific CRDT-backed values require quorum signatures to create/update — first applied to `TrustedPeerRegistry` and `BURN_BASIS_POINTS`.
**Current focus:** Phase 15 — private-networks-consume-privatenetworkid-identity-and-bind-

## Current Position

Phase: 15 (private-networks-consume-privatenetworkid-identity-and-bind-) — EXECUTING
Plan: 16 of 16 — ALL EXECUTED (cycle-2 gap closure: 15-14, 15-15, 15-16 done; 15-04 permanently skipped by owner order; every executable plan has a SUMMARY)
Status: Phase execution complete — ready for verification
Last activity: 2026-09-03
**Progress:** [████████░░] 76%

Last session:
2026-09-03T19:07:58.314Z
Stopped At:
Completed 15-16-PLAN.md

## Roadmap Snapshot

| Phase | Name | Status | Requirements |
|-------|------|--------|--------------|
| 8 | MultiSig Primitive | not started | MSIG-01, MSIG-02, MSIG-03 |
| 9 | SecureCRDT Layer | blocked by 8 | SCRDT-01, SCRDT-02, SCRDT-03, SCRDT-04 |
| 10 | TrustedPeerRegistry | blocked by 9 | TPR-01, TPR-02, TPR-03 |
| 11 | BurnConfig Quorum Wiring | blocked by 10 | BURN-01, BURN-02, BURN-03 |
| 12 | ValidatorRegistry Migration | blocked by 9 | MIG-05, MIG-06 |

## Key Decisions

- Reuse `ConsensusAuth` primitives directly (signing-bytes/SHA-256/`VerifySignature`), not `ConsensusManager`'s proposal/vote/certificate lifecycle — `ConsensusManager`'s voter/weight source is hardwired to a single `ValidatorRegistry` instance
- Propose/sign/quorum flow transported over CRDT itself (pending-value + signature entries via filter callbacks); no new networking/RPC
- `ISignedCRDTData` interface-based per-type classes (not a generic `SignedCRDTValue<T>` template) — matches `ValidatorRegistry`'s existing per-type style
- `TrustedPeerRegistry` is separate from `ValidatorRegistry`'s consensus voter set — different concerns (economic-parameter signers vs. consensus validators)
- `BURN_BASIS_POINTS` cached in `TransactionManager`, refreshed via CRDT-change callback — avoids a CRDT read on every `PayEscrow` call

## Notes

- This milestone continues phase numbering from an undocumented prior body of work (`.planning/phases/01` through `07`, bridge-relayer/consensus-voting features). Phases 8-12 in this milestone are unrelated to those directories; do not reuse or renumber them.
- Precedent to build from: `ValidatorRegistry` (`src/blockchain/ValidatorRegistry.hpp`) already does signature+quorum-gated CRDT updates; `ConsensusAuth.hpp` has the reusable signing-bytes/SHA-256/verify primitives.
- Brownfield codebase map exists at `.planning/codebase/` (STACK, ARCHITECTURE, STRUCTURE, CONVENTIONS, TESTING, INTEGRATIONS, CONCERNS).
- Sequential dependency chain: 8 → 9 → {10 → 11, 12}. Phase 12 depends only on Phase 9 and could in principle run in parallel with 10/11, but is numbered last per the suggested delivery order.

### Roadmap Evolution

- Phase 15 added: Private-network identity and libp2p gater/pnet integration, tracked by GeniusVentures/SuperGenius#367 and blocked on GeniusVentures/libp2p#10.

## Operator Next Steps

- Review `.planning/ROADMAP.md` (Milestone v1.1 section) and `.planning/REQUIREMENTS.md` traceability
- Run `/gsd:plan-phase 8` to begin planning the MultiSig Primitive phase

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260827-hbf | Add GetGraphsyncNetwork() accessor to GeniusNode (SDK needs it — no public path existed) | 2026-08-27 | 8c9e1b4f | [260827-hbf-check-if-graphsyncnetwork-can-be-obtaine](./quick/260827-hbf-check-if-graphsyncnetwork-can-be-obtaine/) |

### v1.0 History

v1.0 (GeniusNode Construction Refactor) shipped 2026-07-03 — see `.planning/MILESTONES.md` and `.planning/milestones/v1.0-*` for full history. Between v1.0 and v1.1, a substantial body of bridge-relayer/consensus-voting work (`.planning/phases/01` through `07`) was executed outside formal GSD milestone tracking; it is unrelated to this milestone's scope.

## Performance Metrics

| Phase | Plan | Duration | Notes |
|-------|------|----------|-------|
| Phase 15 P09 | 18min | 2 tasks | 3 files |
| Phase 15 P10 | 6min | 2 tasks | 2 files |
| Phase 15 P11 | 28min | 2 tasks | 5 files |
| Phase 15 P12 | 11min | 2 tasks | 3 files |
| Phase 15 P13 | 20min | 2 tasks | 13 files |
| Phase 15 P14 | 41min | 3 tasks | 23 files |
| Phase 15 P16 | 30min | 2 tasks | 6 files |

## Decisions

- [Phase 15]: 15-09: NetworkRegistry::New re-runs SecureCrdt::RegisterFilters() after late pattern registration (idempotent; covers both GeniusNode wiring paths) so network-registry/<id> gets its ingest element filter (WR-04)
- [Phase 15]: 15-09: RefreshLoop drains refresh_pending_ under the mutex before TryConfirm (WR-02 busy-spin fixed ahead of 15-12 tx_globaldb_ wiring); duplicate New fail-closes with address_in_use without registering anything (WR-03)
- [Phase 15]: 15-10: missing network_config.json stays public-default (D-01) while an existing-but-unparseable one is fatal — exploit chain closed by writer escaping + fatal parse errors, not by making the file mandatory
- [Phase 15]: 15-10: WriteNetworkConfig escapes all JSON string-unsafe chars (CR-01) incl. swarm-key newlines; LoadNetworkConfig parse-error branch sets settings.valid=false (WR-01) so corrupt configs can never silently boot public
- [Phase 15]: 15-11: gossip membership gate lives at PubSubBroadcasterExt::OnMessage (single gossip->CRDT ingest chokepoint) — dual check of declared protobuf peer AND transport from-field; empty/malformed from is DENIED under a set filter (fail-closed)
- [Phase 15]: 15-11: inline-mirror layering — OnMessage reproduces AuthorizeGossipSender line-for-line instead of calling it (no networkregistry include in crdt); expiry fail-closure proven with an unregistered direct-ctor registry because New-built registries stay pinned by the RegisterFilters D-04 entry capture
- [Phase 15]: 15-12: filter install is colocated with NetworkRegistry construction (unconditional-on-success inside the guarded block, pre-READY) — a private node either runs membership-enforced gossip or fails closed; every private node's broadcaster carries the filter, closing the ungated-member relay vector at node level
- [Phase 15]: 15-12: live registry cache refresh enabled in-node (tx_globaldb_ as NetworkRegistry::New trailing global_db — the deferred 15-05 wiring, safe post-15-09 WR-02); teardown clears the broadcaster filter before registry release (fail-closed either way)
- [Phase 15]: 15-13: membership gates live at the three real SGNUS processing message handlers (grid OnMessage, results OnResultChannelMessage, queue OnProcessingChannelMessage) — same fail-closed AuthorizeGossipSender semantics as the 15-11 gossip chokepoint; empty filter = public pass-through
- [Phase 15]: 15-13: filter propagation covers both time axes — set-time (setBitswap mirror over existing nodes) and creation-time (both m_processingNodes insertion sites), so no enrollment window exists
- [Phase 15]: 15-13: processing test targets registered standalone with per-target source lists (single-source processing_service_test; channel-pubsub test + base.cpp) — registration gate ctest -N >=2 enforced before any test run; both new gate scenes mutation-verified non-vacuous
- [Phase 15]: 15-14: CR-G01 closed — all four membership gates authenticate before authorizing via an application-layer envelope (embedded pubkey must derive the from-field PeerId + signature over magic+from+payload); unsigned/unverifiable denied under a set filter; public nodes byte-identical raw
- [Phase 15]: 15-14: publishers seal with the SAME keypair that constructs the gossip host (SealGossipPayload); filter set + no key = publish fails closed; sign_messages=true at both production sites (wire sigs exist but are not consumed by SGNUS gates — gossip.hpp:129-135)
- [Phase 15]: 15-14: forged-from proven mutation-verified — impostor envelope (member sealing with another member's key) dropped at gated ingest although membership admits; signature rebuilt over the wire from-field gives double protection (binding OR verify disabled still denies)
- [Phase Phase 15]: 15-16: G-WR-01/02/04 closed — teardown removes the GlobalDB ingest element filter via SecureCrdt::UnregisterFiltersFor (token-guarded on UnregisterIf's removal result so a duplicate-New loser never strips a live registry's filter); RegisterCrdtChangeCallback failure fails New with address_in_use; SecureCrdtRegistry::RegisterIfAbsent makes policy registration atomic-detecting (no replace path, no destruction under the mutex) — Pattern-keyed teardown without ownership scoping would let the failed duplicate's destructor remove the LIVE filter (re-opening the unsigned-base-element griefing vector); New's failure paths explicitly Unregister() because the policy entry's peer_registry strong capture pins the instance — destructor-driven cleanup is unreachable while pinned
