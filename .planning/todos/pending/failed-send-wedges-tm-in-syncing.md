---
title: Failed pre-broadcast send leaves TransactionManager in SYNCING indefinitely
area: correctness
priority: P2
date: 2026-10-20
source: live node-example testing (owner, staging net 333, Light role)
resolves_phase: 19
---

A mint that fails BEFORE broadcast (BuildUTXOWitness "Missing producer
transaction" -> SendTransactionItem "Invalid argument") still advanced the
send-nonce baseline ("using first queued nonce 0 as send baseline"). The TM
transitions READY -> SYNCING for nonce recovery, then polls "Checking if my
nonce is updated" every ~2s forever — the tx FAILED, no confirmation can ever
arrive, and READY is never restored. All subsequent submissions rejected with
"Transaction manager not ready" (observed live for 10+ minutes until operator
gave up).

Root: nonce-recovery assumes the sent tx eventually resolves; a pre-broadcast
failure needs to roll the baseline back (or otherwise exit SYNCING) instead of
waiting. Feeds Phase 19's recovery-path audit (criterion 5); live-incident
evidence alongside the InitializeUTXOs boot-wedge todo.
