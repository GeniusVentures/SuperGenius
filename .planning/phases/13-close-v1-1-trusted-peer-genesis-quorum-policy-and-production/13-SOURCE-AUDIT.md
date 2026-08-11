# Phase 13 Multi-Source Coverage Audit

| Source | ID | Feature / requirement | Plan | Status | Notes |
|---|---|---|---|---|---|
| GOAL | — | Authenticated genesis, persisted policy authority, production candidate operations, rollback-safe restart, and node-scoped live economics | 01-08 | COVERED | Roadmap goal and six success criteria map across the complete plan set. |
| REQ | BOOT-01 | Manual ceremony and placeholder labeling | 06 | COVERED | Runbook plus example config warning. |
| REQ | BOOT-02 | Canonical authenticated genesis manifest | 01, 06 | COVERED | Codec/model plus operator tool. |
| REQ | BOOT-03 | Production SecureCrdt genesis confirmation and persistence | 02, 05-07 | COVERED | Store, TPR activation, tool, startup. |
| REQ | BOOT-04 | Restart and rollback safety | 02, 07 | COVERED | Verified LKG and startup E2E. |
| REQ | POLICY-01 | Versioned signed policy and current-policy successor authorization | 01, 02, 04, 05 | COVERED | Exact model through activation. |
| REQ | VALID-01 | Complete peer and threshold validation | 01, 04 | COVERED | 256-peer bound, exact floors, decoder/candidate caps. |
| REQ | TEST-01 | Tamper, startup, restart, race, live burn, account-switch E2E | 02, 04-08 | COVERED | All validation-contract targets included. |
| REQ | SCRDT-04 | CRDT-only propose/sign/quorum production flow | 03-08 | COVERED | No new RPC/pubsub/consensus lifecycle. |
| REQ | TPR-01 | Production signed TPR genesis | 05-07 | COVERED | Tool to durable startup confirmation. |
| REQ | TPR-02 | Current-member quorum updates | 01, 04-06 | COVERED | Current policy exclusively authorizes successor. |
| REQ | BURN-01 | Quorum-signed live BurnConfig | 04-08 | COVERED | Candidate through PayEscrow. |
| REQ | BURN-02 | Cached callback/provider survives account selection | 05, 08 | COVERED | Node-scoped provider and repeated E2E. |
| REQ | BURN-03 | Confirmed default 100 basis points | 01, 05, 07, 08 | COVERED | Deterministic v1 sequencing and actual economics. |
| REQ | MIG-05 | Approved narrowed ValidatorRegistry multisig scope | 08 | COVERED | Canonical wording and summary metadata reconciled; no broadened migration. |
| REQ | MIG-06 | Existing ValidatorRegistry behavior retained | 08 | COVERED | Existing evidence/warning retained in metadata reconciliation. |
| CONTEXT | D-01 | First-boot reviewed config only; persisted authority thereafter | 06, 07 | COVERED | Ceremony and startup authority. |
| CONTEXT | D-02 | Persist canonical genesis fingerprint and fields | 01, 02 | COVERED | Canonical bytes plus durable snapshot. |
| CONTEXT | D-03 | Dedicated one-shot ephemeral-key command | 06 | COVERED | `sgns-trust genesis`; no argv/env/account persistence. |
| CONTEXT | D-04 | Manual collection, validation, canonical review | 01, 06 | COVERED | Automated validation plus manual runbook. |
| CONTEXT | D-05 | Both thresholds in persisted versioned policy | 01, 02, 05 | COVERED | One atomic policy state. |
| CONTEXT | D-06 | Strict-majority membership floor | 01, 05 | COVERED | Exact integer formula and activation. |
| CONTEXT | D-07 | Two-thirds Burn floor | 01, 05 | COVERED | Exact overflow-safe formula and activation. |
| CONTEXT | D-08 | Current policy exclusively authorizes linked successor | 01, 02, 04, 05 | COVERED | Model, store, ingress, activation. |
| CONTEXT | D-09 | Direct SecureCrdt candidates only | 04-06 | COVERED | Signed approval records over existing CRDT topic. |
| CONTEXT | D-10 | Current-member authenticated proposer | 04, 05, 07 | COVERED | Shared local/remote pre-retention gate. |
| CONTEXT | D-11 | Explicit local approval; proposer counts once | 04-06, 08 | COVERED | Receive never signs; local CLI/service does. |
| CONTEXT | D-12 | Content-addressed coexistence and one winner | 04, 05 | COVERED | Exact keys, race, durable CAS, stale loser. |
| CONTEXT | D-13 | Restricted first boot | 05, 07, 08 | COVERED | TPR then Burn v1 sequencing and economic gate. |
| CONTEXT | D-14 | Persisted restart authority; JSON conflict behavior | 02, 07 | COVERED | Critical-ignore trust fields, fatal network mismatch. |
| CONTEXT | D-15 | Rollback/fork rejection and LKG preservation | 02, 05, 07, 08 | COVERED | Store and node/E2E assertions. |
| CONTEXT | D-16 | Node-scoped ownership across SelectAccount | 03, 07, 08 | COVERED | Instance registry, startup ownership, repeated switching. |
| RESEARCH | R-01 | Explicit canonical binary codec and golden vectors | 01 | COVERED | Map-free versioned big-endian encoding. |
| RESEARCH | R-02 | Signed self-contained candidate approval records | 04 | COVERED | Ordering-independent local/remote validation. |
| RESEARCH | R-03 | Synchronous RocksDB store and persist-before-publish | 02, 05 | COVERED | Atomic batch, reload, fault/race tests. |
| RESEARCH | R-04 | Instance-scoped registry and callback ownership | 03, 08 | COVERED | Cross-node isolation and retained services. |
| RESEARCH | R-05 | Burn genesis sequencing open question | 05, 07 | COVERED | TPR durable first; deterministic v1/value 100 auto-approval only. |
| RESEARCH | R-06 | Exact candidate/peer resource caps | 01, 04 | COVERED | 256 peers; 64 KiB; 32 candidates; 256 approvals; 64 MiB. |
| RESEARCH | R-07 | Host-level whole-disk rollback boundary | 02, 06 | COVERED | Explicitly outside software-only guarantee; hardware/off-host guidance. |
| RESEARCH | R-08 | Reusable genesis networking composition | 06 | COVERED | Narrow GlobalDB composition, no full GeniusNode. |
| RESEARCH | R-09 | No new dependency/package | 01-08 | COVERED | Every threat model records no install. |
| RESEARCH | R-10 | No new remote admin RPC/pubsub/consensus proposal protocol | 04-08 | COVERED | Existing CRDT topic plus local command only. |

Deferred bridge timing and bridge startup/RPC todos are excluded exactly as stated in `13-CONTEXT.md`.
