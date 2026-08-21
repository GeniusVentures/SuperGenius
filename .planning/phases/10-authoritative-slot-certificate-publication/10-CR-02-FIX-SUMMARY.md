---
phase: 10-authoritative-slot-certificate-publication
review: CR-02
status: complete
---

# CR-02: Defer Registry Batch Slot Integration

## Decision

Registry batch updates remain outside Phase 10.  The Phase 10 in-memory
subject-hash-to-slot association has been removed because it only exists after
one process receives a finalization callback; a restart or late-joining peer
cannot reconstruct it safely.

Registry batches now fail closed: they are not created from finalized
certificates, registry-batch subjects are rejected, registry-batch certificates
are rejected, and certificate-based registry updates carrying a batch subject
are rejected.  This avoids silently reading a legacy `/cert/<subject-hash>`
record or accepting callback-local evidence.

## Removed

- `pending_certificate_slots_by_subject_` and its subject-to-slot accessors.
- The `ValidatorRegistry` and registry test friendship seams added by Plan 10-05.
- The Plan 10-05 registry certificate lookup test target and source.

## Deferred Follow-up

A future registry-focused phase must define a durable, replay-safe batch
representation that carries or deterministically reconstructs the canonical
slot for every member.  That work is intentionally not part of Phase 10; this
fix does not claim registry batch updates function under canonical-slot
certificate storage.

## Verification

- `git diff --check`
- `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test --parallel 2`
- `ctest --test-dir build/OSX/Release -R '^consensus_pending_lifecycle_test$' --output-on-failure` (1/1 passed)
