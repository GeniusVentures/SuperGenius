---
title: MintFunds leaves the burn UTXO behind when consensus rejects the mint
area: correctness
priority: P1
date: 2026-10-20
source: live node-example testing (owner, staging net 333)
resolves_phase: null
---

MintFunds' rollback (RollbackUTXOs) fires only on the exception path. When the
mint transaction fails CONSENSUS (witness/type-rules rejection -> FAILED state),
the inserted UTXO_BRIDGE burn UTXO (keyed by the burn reference) stays on disk
in READY state. Observed live: fabricated dev burn ref b4f855af... amount 10000
persisted after the mint FAILED, then wedged the next boot (see companion todo:
InitializeUTXOs boot wedge).

Fix: wire reservation/UTXO cleanup to the FAILED terminal transition (not just
the exception path), mirroring the D-18 rollback contract.
