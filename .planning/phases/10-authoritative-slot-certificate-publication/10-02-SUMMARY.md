---
phase: 10-authoritative-slot-certificate-publication
plan: "02"
status: complete
---

# Plan 10-02: Authoritative publication and PubSub result contract

## Implemented

- Moved certificate persistence to the authoritative `/cert/<canonical-slot>` key and the convergent immutable GlobalDB write path before notification.
- Kept the normal deterministic aggregator rotation: only the selected path reaches `SubmitCertificate`; PubSub receipt remains validation/journal-only.
- Migrated certificate CRDT filter and recovery key validation to canonical slot keys and made accepted-slot checks read the authoritative record directly.
- Added a completed transport result to `GossipPubSub::Publish`, including a concrete non-running-service failure; propagated it through consensus and account messaging, with certificate notification failure logged without retry.
- Updated lifecycle fixtures to use canonical certificate keys and `MemorySecureStorage`; added a stopped-transport publish regression.

## External dependency commits

- `ipfs-pubsub` branch `phase10-pubsub-publish-result`: `4f5ece686f91f1ea648ce20e91edc1223c649cb6`
- `thirdparty` gitlink update on `develop`: `ccbca8aa1a2e0f8976e317e55bb59120de221e08`

## Verification

- `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test pubsub_counts_test --parallel 2`
- `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test|pubsub_counts_test' --output-on-failure` — 3/3 passed.
- A first lifecycle failure came from a stale `unit_5` CRDT fixture containing an old certificate; after removing the generated fixture, the isolated active-vote test and full focused suite passed.
