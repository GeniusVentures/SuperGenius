---
title: Deferred Consensus Validation
date: 2026-06-15
context: Exploration of proposal liveness when validation dependencies arrive out of order
---

## Problem

Consensus proposal handling currently behaves as a one-shot validation attempt. For example, Peer B
may receive `tx2` before the certificate for `tx1`, even though `tx2` correctly names `tx1` as its
predecessor. `CheckTransactionReplayProtection()` treats the missing predecessor certificate as a
terminal validation failure, so Peer B never votes after the certificate eventually arrives.

This is a liveness failure caused by conflating a temporarily unverifiable proposal with a proven
invalid proposal.

## Decision

Subject validation will support generic deferred outcomes:

- `Approve`: validation succeeded; broadcast the signed Approval vote.
- `Reject`: available evidence proves the proposal invalid; keep the decision local.
- `Pending`: validation may succeed after missing dependencies or transient local failures resolve;
  keep the decision local and retain the proposal for retry.
- `Stalled`: reserve for local infrastructure failures that prevent reliable processing.

Pending and Reject decisions are not consensus votes. Only Approval votes contribute to quorum.

## Retry Model

Pending validation should identify zero or more dependency keys:

- Explicit dependencies, such as a predecessor transaction certificate, trigger immediate retry when
  the dependency arrives.
- Transient failures without a concrete dependency event, such as RPC or datastore availability,
  use bounded scheduled retries with backoff.

The dependency index must map a dependency key such as `tx1` to every pending proposal waiting for
it, such as `tx2`. Indexing only by the pending proposal's own subject hash cannot solve out-of-order
certificate arrival.

## Lifecycle

- Default pending TTL: compile-time three minutes.
- Tests: inject a shorter TTL, normally ten seconds.
- Expiry means the proposal is inconclusive, not proven invalid.
- Expiry removes proposal state, dependency indexes, queued votes, retry metadata, and temporary
  transaction tracking.
- Enforce limits on pending proposal count and retained bytes to prevent memory exhaustion.
- Clear pending state immediately when the proposal is certified or becomes provably invalid.

Transaction status should distinguish:

- `FAILED`: locally proven invalid.
- `EXPIRED` or `UNCONFIRMED`: no conclusive consensus result before the pending lifetime ended.

## Deferred Scope

Signed Reject votes and reputation adjudication are separate work. The validator registry currently
contains positive and negative vote-weight logic, but Consensus discards negative votes. Reliable
negative-vote scoring would require a finalized rejection outcome, not isolated Reject broadcasts.

