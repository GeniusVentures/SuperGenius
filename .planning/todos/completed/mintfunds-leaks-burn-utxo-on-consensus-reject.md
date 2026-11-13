---
title: MintFunds leaves the burn UTXO behind when consensus rejects the mint
area: correctness
priority: P1
date: 2026-10-20
source: live node-example testing (owner, staging net 333)
resolves_phase: 18
resolved: 2026-10-21
resolution: resolved-in-phase-18
---

MintFunds' rollback (RollbackUTXOs) fires only on the exception path. When the
mint transaction fails CONSENSUS (witness/type-rules rejection -> FAILED state),
the inserted UTXO_BRIDGE burn UTXO (keyed by the burn reference) stays on disk
in READY state. Observed live: fabricated dev burn ref b4f855af... amount 10000
persisted after the mint FAILED, then wedged the next boot (see companion todo:
InitializeUTXOs boot wedge).

Fix: wire reservation/UTXO cleanup to the FAILED terminal transition (not just
the exception path), mirroring the D-18 rollback contract.

## Resolution (Phase 18, plan 18-05)

Reconciled against live source before fixing (file:line evidence in
18-05-SUMMARY.md): the todo's premise was half-outdated — the FAILED transition
ALREADY called RollbackUTXOs for local mint-v2 (TransactionManager.cpp:5077-
5082, reservation id = dag.uncle_hash() = the MintFunds claim id byte for
byte). The REAL delta was rollback semantics: RollbackUTXOs only transitions
RESERVED -> READY in memory; it never removes the MintFunds-INSERTED burn
entry, which therefore survived in memory AND on disk in READY state — exactly
the observed dangling-UTXO_BRIDGE signature.

Fix: UTXOManager::RemoveBurnUTXOs(inputs, reservation_id) — releases the
reservation AND erases the inserted UTXO_BRIDGE entry (READY or RESERVED with
matching holder id), persists the owner snapshot, never touches CONSUMED
entries (sibling-mint spend evidence keeps blocking duplicate burns). Wired to
the FAILED-transition mint-v2 leg (discrimination unchanged: mint-v2 AND local
src AND UTXO params) and to the MintFunds exception path (same inserted-entry
semantics). Pinned by CertificateFallbackTest.BurnCleanup_* — reproduction
(real consensus-reject trigger, entry gone in memory + on disk, no lingering
reservation), idempotency on FAILED re-fire, and the normal-tx negative
control.
