---
title: InitializeUTXOs blocks READY forever on a creating tx that can never arrive
area: correctness
priority: P1
date: 2026-10-20
source: live node-example testing (owner, staging net 333)
resolves_phase: 19
---

Boot's InitializeUTXOs re-fetches the creating transaction for every READY UTXO
(TransactionManager.cpp ~3196-3249); an unresolvable creating tx populates
missing_tx_hashes_ and the node never reaches READY. Observed live: a UTXO whose
creating "tx" is a fabricated burn reference (never existed) wedges boot in
INITIALIZING_TRANSACTIONS indefinitely, re-requesting a nonexistent tx every
sync cycle.

This is empirical evidence for Phase 19 GATE-05 (missing pruned tx = not
relevant, not a failure) — the same handling must cover never-existed refs, and
the current behavior is an infinite block rather than a bounded failure.
Interacts with the MintFunds burn-UTXO leak todo (that leak is the easiest way
to produce this state).
