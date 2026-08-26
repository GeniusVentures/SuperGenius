pre_task1_commit_sha=7b0e4aa07d8c07da539befefe9ba22303cdd96e1

# Phase 12 Plan 07: Mint-boundary reproduction gate

Passive friend-scoped Mint recovery snapshots found no broken boundary across three independently started, real-socket restart processes.

## Task 1 Evidence

P12_MINT_MARKER_RUN run=1 process_exit=0 trace=.planning/phases/12-multi-node-finality-fault-proof/12-07-traces/run-1.log
P12_MINT_MARKER_DIAG run=1 outcome=pass boundary=none state=complete error=none sequence=6
P12_MINT_MARKER_RUN run=2 process_exit=0 trace=.planning/phases/12-multi-node-finality-fault-proof/12-07-traces/run-2.log
P12_MINT_MARKER_DIAG run=2 outcome=pass boundary=none state=complete error=none sequence=6
P12_MINT_MARKER_RUN run=3 process_exit=0 trace=.planning/phases/12-multi-node-finality-fault-proof/12-07-traces/run-3.log
P12_MINT_MARKER_DIAG run=3 outcome=pass boundary=none state=complete error=none sequence=6

All three runs used the existing `RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce` real PubSub/CRDT/RocksDB path, exited successfully, and waited for each prior process to exit before the next began.

repair_authorization=none
status: blocked
phase_disposition=blocked-insufficient-repeatable-mint-boundary-evidence

## Task 2 Disposition

Task 2 was not entered. D-14 requires at least two independently started failures with an identical first boundary, state, and normalized error. This evidence contains no failures, so no RED test, repair, serial-suite execution, protocol change, or fixture-control change is authorized.

## Implementation Scope

The only source change is a passive test-owned collector behind the existing private `MultiNodeFinalityFaultTestAccess` friendship. It reads certificate availability and exact binding, UTXO/outpoint state, local bridge-marker presence, certificate-work-journal status, and tracked-transaction state. Its structured output contains only run, boundary, state, normalized error, and sequence; it emits no payload, RocksDB key, raw value, or account material.

No `ConsensusManager`, `TransactionManager`, CMake, timeout, retry, timer, transport, CRDT write, handler, publication, or production API behavior changed.

## Deviations from Plan

### Auto-fixed Issues

1. [Rule 1 - Bug] Corrected the observer's successful-run classification
   - **Found during:** first fresh process
   - **Issue:** A passing exact-once run reported unrelated unfinished certificate-journal work as its first broken boundary.
   - **Fix:** A completed run now emits the required `boundary=none state=complete error=none`; failure classification still reads the ordered passive snapshots.
   - **Files modified:** `test/src/blockchain/multi_node_finality_fault_test.cpp`
   - **Commit:** c9d658fd

## Known Stubs

None.

## Self-Check: PASSED

- Passive observer source and all three trace files exist.
- Evidence commit `c9d658fd` exists in Git history.
