# Phase 11: Code Review Fix Summary

**Fixed:** 2026-08-24T13:50:11Z  
**Source review:** `11-REVIEW.md`

## Fixed findings

- **CR-01** — `334ea8d4`: `UTXOManager::PutUTXO` now removes its temporary in-memory outpoint when snapshot persistence fails, so a replay must persist the output before it can create the bridge marker or terminal confirmation. Added a failed-store, reload, and retry regression.
- **CR-02** — `2a73e55a`: CRDT transaction candidates now require both the requested hash and `CheckHash()`; certificate binding also checks selected and embedded transaction integrity. Added a forged Mint V2 payload test with an unchanged claimed hash and extra output.
- **WR-01** — `1fc69684`: a `ConsensusManager` recovery mutex serializes durable certificate recovery/handler dispatch across timer and handler-registration triggers. Added a deterministic blocked-handler registration versus timer-recovery race test proving one dispatch.

## Verification

- Re-read every modified section and ran `git diff --check` successfully after each fix.
- Configured an isolated macOS Release build using the existing zkLLVM and third-party installations.
- Focused target build was blocked before compiling the changes by an existing out-of-tree protobuf generation configuration error: `delta.proto` is passed to `protoc` via an absolute worktree path that is not covered by its configured `--proto_path`.

The three source fixes are committed independently; this summary is intentionally left uncommitted for the workflow to record separately.
