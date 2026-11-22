# Deferred execution findings

## Plan 21.1-01

- The existing `.clang-tidy` naming configuration uses `snake_case` for four
  identifier-naming options. Installed LLVM 22.1.8 reports those values as
  invalid. This configuration predates the payment extraction and is outside
  its scope; review the intended naming options separately. Changed-line
  checks still ran, and all Required findings introduced by this plan were
  cleared before commit.

## Plan 21.1-02

- `base::Hash256::fromReadableString` at `src/base/blob.hpp:103` accepts an odd
  65-character input through its integer-divided length check, then can write
  past its 32-byte blob; its `std::stoul` also throws on nonhex pairs and accepts
  partial pairs. New release decoders use the existing checked `fromHex`
  instead. Repair/audit of the pre-existing helper's other callers is outside
  this plan's ten-file codec ownership and remains deferred.

## Plan 21.1-03

- `GeniusInputValidator.cpp:94` retains the existing no-op token-registration
  hook. V2 rejects malformed output TokenID values and binds the consumed token
  to the actual certified hold, while valid result-output tokens retain the
  builder's existing policy. No token registry or unknown-token rejection is
  claimed by this plan.
- Resolved in plan 06: owning `GetCertificateBySlot` preserves NOT_FOUND and
  storage errors, maps rejected validation to invalid_argument, and maps
  Pending/Stalled authority to resource_unavailable_try_again. The nonce
  recovery lookup reuses that same authority. The release resolver consumes
  the classified result directly; its temporary raw-record probe was removed.
  Corrupted votes/quorum/registry bindings reject; missing/deferred evidence
  remains retryable. No private authorization policy was copied into accounts.
- Resolved in plan 04 and retained in plan 06's complete fast-target run: full
  V2 `HandleNonceConsensusSubject` approve, invalid-payment reject and
  missing-hold/certificate pending cases run through tag-10 dispatch and the
  manager parser. Historical Transfer replay still requires private exact
  canonical evidence; no fresh backdated Transfer escrow exception exists.

## Plan 21.1-07 — Blocking Recovery Gate

- Initial named gates and one complete five-case execution passed, but a later
  repetition missed E's existing 5-second topology barrier after partial restart
  (65.64s CTest, 4/5 passing cases). Logs show crossed reconnect handshakes.
- Test-only counterexample `5267fe748` replaces E's partial restart with the
  existing all-owner `RestartAndReconnect`. Its two-repeat focused gate failed
  overall (67.82s): iteration 1 retained valid canonical payout certificates but
  one node lacked complete payment effects after the 25-second catch-up
  condition; iteration 2 passed in 20.862s. The last-iteration XML is insufficient
  to describe the failed CTest result. No production fix is claimed.
- Resume Task 2 by capturing the existing certificate work-journal entry and
  exact per-node hold-spender/output state at shutdown/reopen. A retained
  Processing lease versus completion of a snapshotted weak-owner callback are
  hypotheses only; the exact journal state was not captured. Do not add a retry
  loop, timeout extension, new store, protocol fence or evidence clearing.
- Full logs: `/tmp/phase21-1-07-list-metadata.log` and
  `/tmp/phase21-1-07-restart-counterexample.log`. Completion is deliberately
  withheld; `21.1-07-PARTIAL.md` replaces the premature completion summary.

## Plan 21.1-08 — Final verification findings

- The plan-07 entry above is retained historical failure context. The final
  `21.1-07-SUMMARY.md` supersedes its partial-completion status after the
  deterministic callback RED/GREEN and final local recovery gates; the original
  intermittent E failure's sole cause remains unproven.
- The separate complete network battery at `56eebbf6a` failed in 189.38s:
  `RestartedFinalCertificateRevalidatesThroughGrandfatherProbe` validates the
  captured certificate as `Approve`, but its grandfather-probe counter stays
  8 versus 8 (`multi_node_finality_fault_test.cpp:3973`). All five new escrow
  cases, smoke and process ownership passed. The test body and production
  validation/replacement rules are byte-identical to the execution baseline.
  Existing lower-hash canonical replacement can make the old captured bytes
  take the valid strict path; same-slot serialized variants appear in the log,
  but first-peer pre/post bytes were not captured. This is a supported
  hypothesis, not proven sole causality or a proven pre-existing failing
  baseline. No production rule, test assertion, timeout or runner was changed.
  Technical readiness remains blocked pending the remaining required gates
  and a disposition of this observed failure. Evidence:
  `/tmp/phase21-1-08-network.log` and `-network-artifacts/`.
- Broad rebuilding exposes pre-existing warnings in unchanged
  `test/testutil/http_stub/HttpStubServer.cpp` (unused lambda capture) and
  `test/src/price_facade/price_facade_test.cpp` (missing field initializers).
  They are outside this phase's source diff and were not repaired.
- The unfiltered full Release gate also exposes five
  `burnconfig_policy_e2e_test` failures. Its existing
  `PayEscrowAndReadTransfer` helper casts the new dedicated V2 payout to the
  old `TransferTransaction` type and fails at line 219 before reading the
  burn output. This directly affected fixture requires a V2 reader while
  retaining every original amount, policy and overflow assertion; the
  observation does not establish a production arithmetic defect. The first
  failing XML is preserved in
  `/tmp/phase21-1-08-full-first-burnconfig-policy-xunit.xml`.
  No fixture or production change was made after the three blocker groups
  already repaired in task 1 (manager decoding, pruned-hold recovery and
  low-level linkage). Further repair requires technical continuation;
  technical readiness and operator checkpoint acceptance remain blocked.
  Its `CountOutputDestinations` helper also casts to Transfer at line 263.
  The maximum-value case aborts at `ASSERT_TRUE(full_burn)` at line 453;
  subsequent full-burn and invalid-rate/no-side-effect assertions were not
  exercised in this failing invocation.
- `policy_lifetime_multi_account_test` has the same directly affected fixture
  gap: `PayEscrowAndReadBurn` casts the queued V2 payment to Transfer at line
  183, returns zero, and fails the fatal burn-100 assertion at line 679.
  One of three cases fails (28.04s CTest); later passive burn-v2 convergence,
  no-receiver-action, below-quorum, stale-candidate and commit-failure
  assertions are not reached. Future correction must update its include/cast
  to V2 and preserve all assertions. No production policy/burn defect is
  established, and no further inline repair was performed under the task's
  three-blocker limit.
- The final unfiltered Release gate also fails three baseline-unchanged
  bridge-race targets. Single-burn starts one case but never completes before
  CTest reports Timeout (registered budget 600s, reported target time
  1024.71s); Anvil funding/readiness, all eleven node readiness barriers and
  endpoint configurations succeeded. Batch fails node 3 endpoint setup at
  line 82; fault-RPC fails `DirectSlotFailureFailClosesMint` at lines 165/263.
  Both endpoint failures immediately follow the existing transaction-manager
  not-ready warning, while the fixture waits for the distinct node-ready
  condition. The exact missing/non-ready manager transition and phase
  causality are not established. These are actual failures, not proven
  unavailable resources or proven pre-existing failing baseline results.
  The runner records 2597.21s monotonic for the full suite while CTest and
  UTC labels report 3584.25s; no clock/suspend explanation or timeout cause is
  inferred. No timeout, assertion, launcher or implementation was changed.
