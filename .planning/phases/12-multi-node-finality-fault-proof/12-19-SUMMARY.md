---
phase: 12-multi-node-finality-fault-proof
plan: 19
subsystem: restart-finality/recovery-republication + round-4 evidence
tags: [gap-closure, restart-at-vote, certificate-republication, submit-certificate, cert-02, cert-05, honest-stop, durable-evidence]
requires:
  - 12-18 verdict token DISPROVEN (round4-traces/hypothesis-verdict.md:9) + restart-focused-{1,2,3}.log
  - 12-17 NO-GO developer directive (fallback conditional on disproof)
  - STATE.md SubmitProposal re-advertisement precedent (recorded Phase 12 decision)
provides:
  - Post-restart certificate re-advertisement through the public ingress in RestartAtVote's restart-mint block (GetCertificateBySlot -> SubmitCertificate after RestartPeer + ConnectPeers; multi_node_finality_fault_test.cpp:2494-2515) — the directive's named "post-restart certificate re-publication" shape, CERT-02/CERT-05 preserving
  - Focused evidence that the single-shot re-publication shape does NOT stabilize the signature (run 1 OK 54.75s, run 2 FAILED 97.85s with the same mechanism) — the honest STOP record and the input to the routed developer decision
affects:
  - 12-20 gate re-entry (blocked on the routed decision: no attributed fix carries yet)
  - Any surviving-replica / delayed re-publication follow-up plan
tech-stack:
  added: []
  patterns:
    - "public-ingress re-advertisement of a durably-held unchanged certificate mirrors the recorded SubmitProposal re-advertisement precedent (offline GossipPubSub broadcasts are not replayed)"
key-files:
  created:
    - .planning/phases/12-multi-node-finality-fault-proof/round4-traces/fallback-restart-1.log
    - .planning/phases/12-multi-node-finality-fault-proof/round4-traces/fallback-restart-2.log
    - .planning/phases/12-multi-node-finality-fault-proof/12-19-SUMMARY.md
  modified:
    - test/src/blockchain/multi_node_finality_fault_test.cpp
    - .planning/STATE.md
key-decisions:
  - "Branch keyed on the exact token: hypothesis-verdict.md:9 carries DISPROVEN (single occurrence, consistent with 12-18-SUMMARY.md), so the directive's fallback condition IS met; no fallback-skip.md was created."
  - "Preferred shape implemented, not the alternative: the recreated publisher (proven durable holder via the Mint-effects barrier being downstream of certificate acceptance and persistence-before-advertisement) re-advertises the unchanged certificate through its own public SubmitCertificate after ConnectPeers — byte-identical replay, recipients receive-only, +22 diff lines confined to the block and comment."
  - "Honest STOP per the plan's own rule after run 2 failed: no run 3, no repair iteration beyond the directive's named shapes; the failure is preserved and attributed, and a new developer decision is routed (candidates: never-restarted surviving holder re-publishing once the restarted host is ready; delayed re-publication; accept the residual as a documented race)."
metrics:
  duration: 13m
  completed: 2026-09-03
---

# Phase 12 Plan 19: Round-4 Gap Closure Part 2 (Conditional Fallback Resolution) Summary

**One-liner:** The developer directive's conditional fallback resolved to the repair branch (verdict token DISPROVEN) and the named "post-restart certificate re-publication" was implemented through public SubmitCertificate ingress in RestartAtVote — focused-verified 1 pass / 1 fail, so the plan's honest STOP clause fired with full attribution and a new decision routed; 12-20 carries no fix yet.

## Branch Taken (Task 1) — REPAIR

The branch key read from `round4-traces/hypothesis-verdict.md:9` is exactly **DISPROVEN** (single occurrence in the file, consistent with 12-18-SUMMARY.md "VERDICT: DISPROVEN"). The developer directive's fallback condition ("only if it is disproven fall back to post-restart certificate re-publication / surviving-replica serving") IS met. No `fallback-skip.md` was created.

Entry condition (which log lines persisted, from hypothesis-verdict.md, all in restart-focused-2.log):

- :2167 (10:42:54.413852) `graphsync cannot connect, peer=B4bf4k, msg='Address already in use' state=1` — first failure is the boot-window port-handover race
- :2169-:2171 `CANNOT_CONNECT. Blacklisting peer and trying fallback` → `Erasing route for CID QmZqTKnC...` → `No usable route candidates left for CID QmZqTKnC...` — the certificate-data CID's only route is erased on first blacklist
- :2172 (10:42:55.325047) `Connection reset by peer` on the same reused identity 0.91s later — the override-provably-live re-dial still failing transport-level
- :2422 / :2832 the 25s (`recreated Mint peer repaired its marker`) and 20s (`Mint-boundary recovery stayed exact`) wait timeouts
- Affected block: restart-mint (third RestartAtVote block, network.first restarted) — reused identity B4bf4k resolves to restart-mint-validator-one (:2236 KeyPairFileStorage path)

Branch decision recorded in STATE.md (dated 12-19 entry, commit 96d1c06b).

## Step 1 Determination — Missing Link and Durable Holders

- **Dead-ending fetch:** the certificate-data CID (12-18 run 2: `QmZqTKnCVUT9b26ByT34opXDQNXak3mUXEh4Y81MgRfJBF`, verdict file: "Failing CID: ... (certificate data)"). The registry/DAG auxiliary fetches were not the dead end.
- **Durable holder at the restart boundary (block 3):** the recreated `network.first`. The Mint-effects barrier wait ("first validator paused after durable Mint effects and before bridge marker", test :2485-2487 pre-edit) proves first accepted and processed the certificate — Mint effects are downstream of certificate acceptance, and Phase 10's persistence-before-advertisement guarantees the record is durable before that. The durable root persists across RestartPeer.
- **second/third/passive retention is NOT proven in block 3** (the CheckCertificateForSlot "surviving peers retained" wait exists only in block 2, where the certificate publisher stays alive; block 3 has no such pre-restart retention wait), and the 12-18 evidence shows a fetcher left with no route — i.e. at least one peer lacked the content. This is why the preferred shape submits from the recreated first ("first-or-survivor" per the plan) rather than from an unproven survivor.

## Task 2 — The Repair (Preferred Shape) and Its Exact Diff Surface

`test/src/blockchain/multi_node_finality_fault_test.cpp` restart-mint block, +22 insertions / 0 deletions at :2494-2515, immediately after `RestartPeer( network.first )` + `ConnectPeers( Peers( network ) )` and before the unchanged 25s wait (now :2521):

```cpp
{
    const auto durable_certificate = network.first.consensus->GetCertificateBySlot( slot );
    ASSERT_TRUE( durable_certificate.has_value() );
    ASSERT_TRUE( network.first.consensus->SubmitCertificate( durable_certificate.value() ).has_value() );
}
```

preceded by a comment citing the developer directive's fallback name ("post-restart certificate re-publication / surviving-replica serving"), the DISPROVEN verdict path, and the STATE.md SubmitProposal re-advertisement precedent.

Why the preferred shape and not the justified alternative (Step 3 was NOT taken): the durable holder's public route accepted the resubmission — both ASSERTs passed in every run (see below), `SubmitCertificate` (src/blockchain/Consensus.cpp:2074-2158) re-validated the certificate, fell through the equal-hash existing-record short-circuit (byte-identical replay), re-put via the production `PutConvergentImmutable` authority path, and re-Published. There was no evidence of a "pure surviving-replica gap" requiring a different shape: the holder exists (first's durable root); the gap is propagation.

Guard verification:

- **CERT-02**: `PutConvergentImmutable` count in the test file remains **0** (no new CRDT-write site; the re-put happens inside production `SubmitCertificate`, the deterministic publisher authority route).
- **CERT-05**: the certificate is retrieved from the durable record via `GetCertificateBySlot` (Consensus.hpp:509 — validate-on-read, returns the durable bytes; never constructed), so the replay is byte-identical and idempotent.
- **No timing relaxation**: `git diff -U0 | grep -E "^[+-].*(chrono::seconds|Barrier|Arm|Release)"` → no matches; the diff is confined to the re-advertisement block and its comment. `SubmitCertificate` grep count 0 → 2 (call + comment citation).
- Build clean: `cmake --build build/OSX/Release --target multi_node_finality_fault_test` → 100% with no warnings on the file.

## Focused Verification — 1 Pass / 1 Fail → Honest STOP

Port preflight `lsof -nP -iTCP:54601-54634 -sTCP:LISTEN` empty before run 1. Runs serial with pre-run load headers (12-18 header format). Load discipline deviation: the below-2 requirement remained unreachable (external post-reboot daemon plateau, 12-18 precedent) — actual 1-min loads recorded per log header (2.93-3.59), far below the 12-12 contamination regime (15-20 on 8 cores).

| Run | Pre-run 1-min load | Outcome | Wall | Log |
|-----|--------------------|---------|------|-----|
| 1 | 2.93 | **OK** | 54.75 s | round4-traces/fallback-restart-1.log |
| 2 | 3.59 | **FAILED** | 97.85 s | round4-traces/fallback-restart-2.log |

Run 2 failure detail (fallback-restart-2.log):

- Timeouts: `:2458` `recreated Mint peer repaired its marker through normal certificate recovery (timeout: 25000ms)` and `:2874` `Mint-boundary recovery stayed exact after reopening every durable root (timeout: 20000ms)` (test :2521/:2534 post-edit) — the same two waits as 12-18's failing run, same block (restart-mint; restarting identity MKWVcg = restart-mint-validator-one, :2166/:2015).
- **The repair executed successfully**: no gtest failure in the :2490-2516 range — both ASSERTs passed, so `GetCertificateBySlot` returned the durable certificate and `SubmitCertificate` returned success (re-validate + re-put + re-Publish all succeeded on the recreated publisher).
- **Recipients still dead-ended**: zero post-restart `/cert/mint-v2` CRDT callbacks for the block-3 slot (vs four callbacks per passing block, e.g. block-1 slot :408/:456/:460/:464); `CheckCertificateForSlot( slot )` false on a surviving peer at both teardown checks (test :1261, log :2977 region and :2874 block); and the same certificate-CID route loss on the reused host identity in the boot window: `Connection reset by peer` :2160-:2165 → `Blacklisting peer` :2214/:2248/:2251 → `Erasing route` :2216/:2249/:2252 → `No usable route candidates left for CID QmPQpcb...` :2218/:2250/:2253 (route_count=1 to the reused identity MKWVcg, route added :2144-:2148 at 10:59:47 just before the old host teardown :2138).

**Per the plan's Step 4 rule, this fired the STOP clause**: "if any run fails, record it and STOP (do not iterate repairs beyond the directive's named shapes inside this plan; route a new decision instead)." No run 3 was started (`fallback-restart-3.log` intentionally does not exist — the no-reroll evidence budget is not spent fishing for passes), and no further repair was attempted.

### Attribution (diagnosis finding for the routed decision)

The single-shot re-publication through public ingress is necessary-but-insufficient: the recreated publisher successfully re-published, but recipients did not converge through it in the failing timing. Two candidate mechanisms, distinguishable only by a new scoped decision:

1. **Gossip-mesh race**: `ConnectPeers` proves libp2p connectivity, not gossip-topic graft readiness; the re-published `ConsensusMessage` may have been sent before recipients' consensus-topic subscriptions re-formed, and GossipPubSub does not replay — recipients then fall back to the CRDT CID fetch, which dead-ends on the reused host identity (route erased on first blacklist; the 100 ms backoff seam from 12-18 is still active in this test, letting dials retry into the boot-window port race).
2. **CRDT DAG dependency**: recipient-side processing of the re-put convergent-immutable record still requires fetching the CRDT DAG node by CID (route_count=1: only the restarted publisher serves it), so the embedded-content convergence path did not engage for these recipients.

Either way, the directive's named re-publication shape as implementable inside this test does not close the ~50% race (1 pass / 1 fail here; 2/3 in 12-18). The "surviving-replica serving" half of the directive's fallback remains untried in a form that survives the above: a **never-restarted** holder does not exist for the block-3 certificate on today's evidence (block 3 has no pre-restart retention wait proving second/third/passive hold it).

### Decision routed to the developer (12-20 gate input)

Options, with the evidence constraint that no surviving never-restarted holder is proven in the failing block:

| Option | Shape | Trade-off |
|--------|-------|-----------|
| A (needs test change) | Delay the re-advertisement until recipient gossip-topic readiness is observable, or re-advertise from a peer that provably holds the record (add a pre-restart CheckCertificateForSlot wait on second/third first, making a true surviving replica) | Stays inside the directive's named shapes; adds a readiness/wait step — must not relax any existing bound |
| B (needs seam/production decision) | Make CRDT sync resilient to the boot window (e.g. re-provide/route recovery for the restarted host identity) | Outside the test file; broader blast radius into graphsync/CRDT territory previously kept out of scope |
| C | Accept the RestartAtVote block-3 residual as a documented race and scope 12-20's gate to the stabilized signature | No code change; the three-pass gate would keep failing at ~50% per focused-run odds unless the filter excludes this case |

### Decision RECEIVED (developer, relayed by coordinator, 2026-09-03): Option A

The routed decision is resolved. Verbatim record:

> "Developer decision (2026-09-03): Option A — delay the re-advertisement until recipient gossip-topic readiness is observable, OR add a pre-restart CheckCertificateForSlot wait on second/third peers first (making a true surviving replica). Test-file change only; stays inside the directive's named shapes; must not relax any existing wait bound. Funded as round-5 scope before the 12-20 gate re-attempt."

Consequences: the round-5 plan implements Option A (readiness-gated re-advertisement or a pre-restart retention wait creating a true surviving replica — test-file only, no existing wait bound relaxed); the repair 12-19 shipped stays as the honest intermediate state (run evidence 1 pass / 1 fail preserved); 12-20's gate re-attempt waits on the round-5 outcome and still carries no fix until then; TEST-03/TEST-04 remain unmarked.

## Durable Evidence Paths (nothing in /tmp)

- round4-traces/fallback-restart-1.log — focused run 1, OK (pre-run load header)
- round4-traces/fallback-restart-2.log — focused run 2, FAILED (pre-run load header; all citation lines above)
- round4-traces/fallback-restart-3.log — intentionally absent (STOP clause; no reroll)
- 12-18's restart-focused-{1,2,3}.log and hypothesis-verdict.md — the entry-condition evidence

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking/environment] Load-below-2 discipline unreachable; proceeded at recorded external plateau**
- **Found during:** Task 2 Step 4 (preflight)
- **Issue:** The plan requires 1-min load below 2 per run; the machine stayed on the sustained external post-reboot daemon plateau (12-18's recorded deviation), actual 2.93-3.59 on 8 cores.
- **Fix:** Proceeded with the actual load recorded in each log header (12-14/12-18 precedent); far below the 12-12 contamination regime (15-20). Both outcomes occurred within this band; the failing run's signature is the load-independent transport/route mechanism quoted above.
- **Files modified:** log headers only (evidence integrity).
- **Commit:** 191e12f0

**2. [Plan-text drift, no action needed] Plan line references were pre-12-18**
- **Found during:** Task 2 Step 2
- **Issue:** The plan's cited line numbers (e.g. RestartPeer at :2416, barrier at :2404) predate 12-18's +19-line seam insert; current anchors verified by content (the unique "recreated Mint peer" wait message) before editing.
- **Fix:** None required — content-anchored edit; post-edit lines cited in this SUMMARY.

Otherwise the plan executed exactly as written, including its own STOP clause.

## Auth Gates

None.

## Must-Have Truth Status

| Truth | Status |
|-------|--------|
| The directive's conditional fallback resolved honestly (recorded skip or verified repair) | MET — exact-token DISPROVEN branch taken; repair implemented and focused-run-verified with both outcomes preserved honestly |
| If the repair branch runs: three consecutive focused passes, durable logs, CERT-02 preserved, no duplicate mint | PARTIAL — 1/2 passes then the plan's own STOP clause; logs durable; CERT-02 preserved (0 PutConvergentImmutable sites); no duplicate mint in either run (AssertSingleDurableMint/AssertOneLiveMintEffect failures were absent — the failures were missing-convergence, not double-mint) |
| Whichever branch runs, 12-20's gate has an unambiguous attributed fix to carry | NOT MET as a fix — the unambiguous output is instead the routed decision (above) with the mechanism attributed; 12-20 must not re-gate on this repair alone |

## Requirements Traceability

TEST-03 / TEST-04 were exercised by the focused runs but are **not marked complete** by this plan: run 2 failed the restart-recovery convergence (TEST-03's no-recipient-stall semantics on an unavailable CID) while exact-once semantics held in both runs (TEST-04). Marking them complete on a 1-pass/1-fail record would be dishonest; they remain open inputs to the routed decision.

## Known Stubs

None — the repair is functional code on the production public ingress; no placeholder flows.

## Threat Flags

None. T-12-19-01 (byte-identical certificate from GetCertificateBySlot, never constructed; zero new CRDT-write sites), T-12-19-02 (preferred shape sufficed — no alternative branch taken; no bound/timeout touched, diff-gated), T-12-19-03 (exact-token branch; both branches' durable records written), T-12-19-04 (logs under round4-traces/, committed; failure preserved), T-12-19-05 (serial runs, port preflight clean; load deviation honestly recorded) — all honored.

## Self-Check: PASSED

- Created files present on disk: 12-19-SUMMARY.md, round4-traces/fallback-restart-1.log, round4-traces/fallback-restart-2.log — FOUND each.
- Task commits verified in git log: 96d1c06b (Task 1 branch decision), 191e12f0 (Task 2 repair + logs + STATE record).
- Gates re-verified: SubmitCertificate grep 2 (was 0), PutConvergentImmutable grep 0 (unchanged), no chrono/Barrier lines in the diff, run 1 log contains exactly 1 OK line and 0 wait-timeout lines, run 2 log preserves the FAILED outcome and both timeout lines (honest STOP evidence), STATE.md carries the dated 12-19 branch-decision and repair/STOP entries (grep 5).
