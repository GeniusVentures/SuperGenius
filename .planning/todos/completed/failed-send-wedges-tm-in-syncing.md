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

RESOLUTION (2026-10-23, Phase 19 plan 19-02, decision D-02 both halves):

Half 1 — the preflight failures (witness/size, before any CRDT Put/Commit)
now return the typed `TransactionManager::Error::PRE_BROADCAST_VALIDATION`,
and the TickOnce error site rolls the batch back (FAILED releases the
reserved nonce) and STAYS READY instead of entering SYNCING — nothing was
published, so no nonce recovery is ever needed. The ambiguous errc codes
(`invalid_argument`/`message_size` fire on both sides of Commit) are no
longer used to classify.

Half 2 — a SYNCING state with no confirming nonce advance within a bounded
window (60s default, ForTest knob) falls back to the last DURABLE confirmed
nonce, WARNs, and returns READY; held nonce reservations are never touched
(no collision).

Regression tests: `PreBroadcastFailureKeepsManagerReadyWithNonceRelease`,
`PreBroadcastNegativeControlNonTypedFailureStillEntersSyncing`,
`SyncingExitFallsBackToDurableNonceAfterWindow`,
`SyncingExitNormalSyncPathDoesNotDependOnWindow`
(test/src/account/transaction_manager_pending_lifecycle_test.cpp, commits
6cbe22b45 + 4d104d43d + test commit on gsd/phase-19-pruned-node-survival-
recovery-hardening).
