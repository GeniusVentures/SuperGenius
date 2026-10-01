---
created: 2026-08-10T19:18:09.997Z
title: Secure trusted-peer genesis configuration
area: security
files:
  - src/account/GeniusNode.cpp:438
  - src/account/GeniusNode.cpp:758
  - src/trustedpeer/TrustedPeerRegistry.cpp:138
  - src/account/BurnConfig.cpp:95
  - src/securecrdt/QuorumThresholdValidation.hpp:29
  - example/node_test/sgns_config.json:10
  - .planning/v1.1-MILESTONE-AUDIT.md:237
---

## Problem

`trusted_peers`, `bootstrapper_node`, `trusted_peer_quorum_threshold`, and
`burn_config_quorum_threshold` are currently loaded directly from mutable
`sgns_config.json`. The example peer addresses are provisional rather than the
final manually verified project peers, but no operator ceremony or warning
makes that clear.

Before TrustedPeerRegistry genesis is confirmed, the configured bootstrapper is
the sole signer and the configured peer list is copied directly into the live
cache. BurnConfig consumes that cache even while genesis is unconfirmed. There
is no pinned manifest hash, offline-root signature, network binding, persisted
genesis identity, or restart-time mismatch check.

An attacker who can replace the JSON and supply matching keys can redefine a
node's trust root. Replacing the list with one attacker-controlled address and
threshold 1 passes the current majority-floor check. An attacker without the
keys can still create denial of quorum or node divergence. A compromised shared
deployment artifact could extend this to every node. Thresholds above signer
count are also accepted and make quorum permanently impossible.

## Solution

Implement these closure requirements before v1.1 is archived:

- [ ] **BOOT-01 — Operator ceremony:** Document how operators collect, verify,
  canonicalize, and approve the real project trusted-peer addresses and quorum
  policies. Clearly mark current example addresses as non-production
  placeholders.
- [ ] **BOOT-02 — Authenticated manifest:** Define a canonical genesis manifest
  containing network ID, bootstrapper public key, ordered unique peer list,
  trusted-peer threshold, BurnConfig threshold, version/epoch, and manifest
  hash. Authenticate it with a separately pinned hash or offline-root signature.
- [ ] **BOOT-03 — First-boot confirmation:** Verify the manifest before using
  its values, submit the signed TrustedPeerRegistry genesis record, call
  `TryConfirm()`, and persist the confirmed manifest/genesis identity.
- [ ] **BOOT-04 — Restart and rollback safety:** On later boots, load confirmed
  state and fail closed if mutable configuration conflicts with the pinned or
  persisted genesis identity. Prevent rollback to an older manifest/epoch.
- [ ] **POLICY-01 — Authenticated thresholds:** Remove quorum thresholds as
  locally authoritative JSON knobs. Derive them deterministically from the
  confirmed peer set or carry them in quorum-signed policy state. Any later
  threshold change must require the existing trusted-peer quorum.
- [ ] **VALID-01 — Complete validation:** Require a non-empty unique valid peer
  set and enforce `1 <= threshold <= signer_count` plus the configured majority
  or supermajority policy.
- [ ] **TEST-01 — Tamper and E2E coverage:** Add tests for altered peer lists,
  bootstrapper replacement, lowered/oversized thresholds, manifest mismatch,
  rollback, inconsistent node manifests, successful first boot, and restart
  from confirmed state. Include the production TPR genesis and live BurnConfig
  update flows identified by the v1.1 milestone audit.

The JSON may retain a path to the authenticated manifest and other
non-authoritative operational settings, but it must not define the trust root.
