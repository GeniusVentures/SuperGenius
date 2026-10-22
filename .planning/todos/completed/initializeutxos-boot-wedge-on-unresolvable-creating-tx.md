---
title: InitializeUTXOs blocks READY forever on a creating tx that can never arrive
area: correctness
priority: P1
date: 2026-10-20
source: live node-example testing (owner, staging net 333)
resolves_phase: 18
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

RESOLUTION (2026-10-22, pulled forward from Phase 19 by owner after the third
live occurrence): fixed in phase 18 — InitTransactions' unconditional return
(dead recovery loop) replaced with GATE-05 semantics (transition proceeds,
bounded background recovery, attempt-capped drop with WARN). Phase 19 keeps
the REMAINING GATE-05 scope: the checkpoint-rebuild clear-and-rebuild guard
(never resurrect spent outputs from partial history) and the recovery-path
audit of the other consumers. Commit on gsd/phase-18 (fix(18): unresolvable
creating transaction no longer wedges boot).
