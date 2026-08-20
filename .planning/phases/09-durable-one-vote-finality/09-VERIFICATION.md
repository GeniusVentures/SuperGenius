---
phase: 09-durable-one-vote-finality
verified: 2026-08-20T22:29:31Z
status: passed
score: 10/10 must-haves verified
overrides_applied: 0
---

# Phase 9: Durable One-Vote Finality — Verification Report

**Phase Goal:** A validator deterministically chooses and durably commits to at most one usable vote for a canonical slot throughout contention and restart recovery.

**Verified:** 2026-08-20T22:29:31Z  
**Status:** passed  
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|---|---|---|
| 1 | D-01: the first approved contender opens one fixed 2-second generic window. | VERIFIED | `ContinueProposalAfterSubject` establishes `candidate_deadline = now + candidate_window_` once and admits only before it ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:641)); the test forces the deadline without sleeps. |
| 2 | D-02/D-03: eligibility freezes at the deadline and the generic deterministic comparator selects one winner. | VERIFIED | `ProcessDueVoteWork` freezes then selects with unchanged `IsBetterProposal` ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1407)); `HandleVote` rejects non-frozen/non-winning proposals ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2701)). Lifecycle cases prove lowest-ranked pre-deadline winner and late-contender exclusion ([test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:1176), [test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:1350)). |
| 3 | D-04: arbitration is generic slot-key behavior, not a Mint-only path. | VERIFIED | The only new paths consume `GetSlotKey`, `SlotState`, and `IsBetterProposal`; no Mint slot code changed. The existing non-Mint slot-key test remains among the 6 passing slot-key cases. |
| 4 | D-05/D-06: before first outbound vote, direct local RocksDB holds the slot, full proposal, exact signed vote bytes, and absolute deadline. | VERIFIED | Local-only `ActiveVoteRecord` has exactly those four fields and is outside the network oneof ([Consensus.proto](../../../../src/blockchain/impl/proto/Consensus.proto:203)). `ProcessDueVoteWork` calls `PersistOrLoadExactActiveVote` before adding to the publish list ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1426)); persistence uses `GetDataStore()->get/put` at `/consensus/vote/<slot>` ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1249)). |
| 5 | D-07: an occupied slot accepts only the exact serialized record; collisions, malformed records, and write errors cannot broadcast a replacement. | VERIFIED | Existing bytes must equal the complete encoded record or the method fails without a put ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1251)); decode recomputes slot, voter/proposal/approval coherence, and verifies the original signature ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1197)). Write-failure and corrupt-record regressions pass ([test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:1212), [test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:1272)). |
| 6 | D-08: restart and bounded retry can re-announce only the stored, validated signed vote before its recorded deadline. | VERIFIED | Startup enumeration decodes and validates local records before rehydrating them ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1276)); due work serializes the retained vote rather than recreating it and checks the absolute deadline ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1440)). Exact-byte retry and restart are asserted in the lifecycle runner ([test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:1235)). |
| 7 | D-09: expiry stops reannouncement but retains the durable no-revote lock. | VERIFIED | Due work skips when `now_ms >= acceptance_deadline_ms` ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1459)); it does not remove either map state or RocksDB data. The expiry regression asserts no new announcement and a retained lock ([test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:1272)). |
| 8 | D-10: callback receipt is pre-commit/stalled-only; only durable readback, key/binding revalidation, approval, same-slot derivation, and local deletion can release. | VERIFIED | `CertificateReceived` only marks journal work stalled ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:2505)). `RecoverPendingCertificateWork` reads the live value, revalidates it, derives its slot, then invokes release ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:3521)). `ReleaseActiveVoteForAcceptedSlot` validates the local record and removes only its matching direct-local key ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1136)). |
| 9 | D-10 safety edges: receipt-only, missing/racing readback, keyless PubSub, other-slot finality, and removal failure retain the lock; same-slot finality may name another proposal. | VERIFIED | The certificate lifecycle test constructs a different signed proposal for the same slot, checks receipt/missing-readback and PubSub retain the record, then checks removal failure retains it until a later readback succeeds ([test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:1456)). |
| 10 | Finalized-slot scan errors fail closed without extending/reopening a window or merging post-deadline contenders; recovery cannot revote an accepted slot. | VERIFIED | The read-only legacy scan returns an error on indeterminate data ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1087)); candidate recovery retains only pre-deadline contenders and clears the buffer after the original deadline ([Consensus.cpp](../../../../src/blockchain/Consensus.cpp:1345)). Three focused regressions cover scan retry, pre-deadline merge, and post-deadline freeze ([test](../../../../test/src/blockchain/consensus_pending_lifecycle_test.cpp:1305)). |

**Score:** 10/10 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `src/blockchain/impl/proto/Consensus.proto` | Private durable vote envelope | VERIFIED | Substantive four-field record; no `ConsensusMessage` variant. |
| `src/blockchain/Consensus.hpp` | Generic window, durable vote, release, and scan contracts | VERIFIED | Private `SlotState`, `ActiveVoteState`, and helper declarations are wired by the manager timer/startup paths. |
| `src/blockchain/Consensus.cpp` | Persist-before-send, exact replay, fail-closed scan, post-commit release | VERIFIED | Direct source trace above establishes all three lifecycle boundaries. |
| `test/src/blockchain/consensus_pending_lifecycle_test.cpp` | Deterministic adversarial lifecycle coverage | VERIFIED | 17 direct-run tests exercise real in-memory storage/manager seams, not a mocked local-author shortcut. |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| Vote freeze | Local RocksDB | `PersistOrLoadExactActiveVote` before publish/`SubmitVote` | WIRED | Persist/load success is required before the vote enters `publish`; the send occurs after the mutex section. |
| Restart/retry | Stored vote bytes | `DecodeActiveVoteRecord` → `active_votes_` → `SubmitVote` | WIRED | Recovered/retried bytes come from parsed stored `Vote`, never `CreateVote`. |
| CRDT certificate callback | Vote release | Stalled journal entry → later `db_->Get` readback → validation → release | WIRED | Callback has no release call; release is reachable only from committed-work recovery. |
| Accepted certificate scan | Candidate gate/recovery | `HasAcceptedCertificateForSlot` tri-state result | WIRED | Errors block admission/replay; an accepted slot becomes a no-revote fence. |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Build focused consensus targets | `cmake --build build/OSX/Release --target consensus_pending_lifecycle_test consensus_slot_key_test --parallel 4` | Completed | PASS |
| Direct lifecycle behavior | `build/OSX/Release/test_bin/consensus_pending_lifecycle_test --gtest_color=no` | 17/17 passed | PASS |
| Slot-key regression behavior | `build/OSX/Release/test_bin/consensus_slot_key_test --gtest_color=no` | 6/6 passed | PASS |
| CTest registration/wrapper | `ctest --test-dir build/OSX/Release -R 'consensus_(slot_key|pending_lifecycle)_test' --output-on-failure` | 2/2 passed in 18.74s | PASS |
| Scope/whitespace | `git diff --check 35daf3d0..HEAD -- <phase files>` | Clean; `MintTransactionV2.cpp` unchanged | PASS |

### Requirements Coverage

| Requirement | Source Plan | Status | Evidence |
|---|---|---|---|
| VOTE-01 | 09-01 | SATISFIED | Fixed generic window, frozen winner, late-contender and scan-recovery tests. |
| VOTE-02 | 09-01 | SATISFIED | Local private record is persisted and revalidated before first publication. |
| VOTE-03 | 09-01 | SATISFIED | Exact byte replay/restart, collision/write/corruption/expiry fail-closed coverage. |
| VOTE-04 | 09-02 | SATISFIED | Pre-commit callback boundary, post-commit same-slot readback/release, and no-revote finalized scan. |

No Phase-9 requirements are orphaned from the plans.

### Anti-Patterns and Scope Check

No Phase-introduced stubs or debt markers were found. The sole nearby `//TODO` in `Consensus.cpp` predates this phase (`52fc1c253`) and is outside the changed region. The Phase 9 source delta is limited to the four planned files. It adds neither `/cert/<slot>` authority, CRDT multiwriter behavior, publisher/failover logic, nor mint-recovery work; the legacy `/cert/<subject-hash>` scan is read-only local safety fencing.

### Probe Execution

No Phase 9 probe scripts are declared or present. The focused C++ targets are the phase's runnable verification contract and passed directly and through CTest.

## Conclusion

Phase 9 achieves its goal. The validator now has a generic, bounded contender window and a single durable exact-vote lock; storage/restart/error paths cannot produce a replacement vote. Lock release is deliberately ordered after durable same-slot certificate acceptance, while indeterminate certificate scans fail closed and preserve the original window rules. Phase 10's authoritative slot-certificate publication and failover, and Phase 11's convergent consumption/mint recovery, remain outside this implementation.

---

_Verified: 2026-08-20T22:29:31Z_  
_Verifier: the agent (gsd-verifier)_
