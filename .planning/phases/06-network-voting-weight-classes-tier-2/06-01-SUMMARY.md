---
phase: 06-network-voting-weight-classes-tier-2
plan: 01
subsystem: consensus/proto
tags: [consensus, proto, rpc-voting, slot-hash, phase-6]
requires:
  - ConsensusVote proto (1..5 fields)
  - PublicChainInputValidator::GetFirstRpcUrl pattern
  - ConsensusManager::CreateVote signing flow
provides:
  - ConsensusVote.slot_0_hash / slot_1_hash / slot_2_hash (tags 6,7,8)
  - PublicChainInputValidator::GetSlotHash(slot_index, chain_id)
  - PublicChainInputValidator::GetFirstConfiguredChainId()
  - ConsensusManager::SlotHashPopulator + SetSlotHashPopulator
  - Blockchain::SetSlotHashPopulator forwarder
  - GeniusNode init-time populator wiring
affects:
  - Vote signing surface (VoteSigningBytes now covers slot hashes)
  - GeniusNode INITIALIZING_TRANSACTIONS state
tech-stack:
  added: []
  patterns:
    - "Proto extension (forward/backward wire compatible, tags 6/7/8)"
    - "Optional callback injection for cross-subsystem bridging without changing CreateVote signature"
    - "Read-only additive accessor (D-10): Tier 1 verification path untouched"
key-files:
  created:
    - test/src/blockchain/consensus_vote_slot_test.cpp
    - test/src/account/public_chain_input_validator_slot_test.cpp
    - test/src/blockchain/consensus_slot_hash_populator_test.cpp
  modified:
    - src/blockchain/impl/proto/Consensus.proto
    - src/account/PublicChainInputValidator.hpp
    - src/account/PublicChainInputValidator.cpp
    - src/blockchain/Consensus.hpp
    - src/blockchain/Consensus.cpp
    - src/blockchain/Blockchain.hpp
    - src/blockchain/impl/Blockchain.cpp
    - src/account/GeniusNode.cpp
    - test/src/blockchain/CMakeLists.txt
    - test/src/account/CMakeLists.txt
decisions:
  - D-01 implemented: three slot-hash bytes fields on ConsensusVote
  - D-04 respected: slot hashes live ONLY in the vote (no ValidatorEntry/Registry change)
  - D-05 respected: empty bytes = abstain sentinel, documented in proto
  - D-10 respected: GetSlotHash/GetFirstConfiguredChainId are additive read-only accessors; VerifyPublicChainSmartContract untouched
  - Added Blockchain::SetSlotHashPopulator forwarder because consensus_manager_ is private on Blockchain (deviation from plan wording)
  - Single-chain resolution via GetFirstConfiguredChainId; multi-chain TODO documented
metrics:
  duration: ~15m
  completed: 2026-06-19
  tasks: 3
  files_created: 3
  files_modified: 10
---

# Phase 6 Plan 01: Slot-Hash Voting Foundation Summary

Established the cryptographic foundation of the slot-based RPC-hash voting model (D-01) by extending `ConsensusVote` with three slot-hash bytes fields, adding read-only endpoint-hashing accessors to `PublicChainInputValidator`, and wiring slot-hash population into `ConsensusManager::CreateVote` via a callback injected by `GeniusNode` (forwarded through `Blockchain`).

## What Was Built

### Task 1 — ConsensusVote proto extension
- Added `bytes slot_0_hash = 6` (DIRECT_API, weight >= 50), `slot_1_hash = 7` and `slot_2_hash = 8` (PUBLIC, weight < 50) to `ConsensusVote`.
- Added a Doxygen block documenting slot semantics, the empty-bytes abstain sentinel (D-05), the vote-only hash location (D-04), and forward/backward wire compatibility.
- `VoteSigningBytes` covers the new fields automatically (only `signature` is cleared), so any mutation invalidates the signature (T-06-01).

### Task 2 — PublicChainInputValidator::GetSlotHash
- Added `std::vector<uint8_t> GetSlotHash(size_t slot_index, const std::string& chain_id) const noexcept` returning the SHA-256 of the qualifying endpoint's `url` string via the existing `crypto::HasherImpl`.
- slot 0 → first endpoint with `consensus_weight >= 50`; slots 1/2 → first/second endpoint with `consensus_weight < 50`; unknown slot/chain or no qualifying endpoint → empty vector (abstention / fail-closed).
- Added `GetFirstConfiguredChainId()` additive accessor (used by the GeniusNode populator for single-chain resolution).
- Tier 1 verification logic (`VerifyPublicChainSmartContract`) untouched (D-10).

### Task 3 — ConsensusManager + GeniusNode wiring
- Added `ConsensusManager::SlotHashPopulator = std::function<void(ConsensusVote&)>`, `SetSlotHashPopulator` setter, and `slot_hash_populator_` member.
- `CreateVote` now invokes the populator (if set) after field population but before `VoteSigningBytes`, so the signature commits to the slot hashes. No-op when unset (backward compatible).
- Added `Blockchain::SetSlotHashPopulator` forwarder because `consensus_manager_` is private on `Blockchain` (the plan said "inject from GeniusNode" but the architecture encapsulates the manager behind Blockchain).
- `GeniusNode` wires the callback during `INITIALIZING_TRANSACTIONS` (after `transaction_manager_` is created), bridging `TransactionManager::GetPublicChainInputValidator()` → `Blockchain` → `ConsensusManager`.

## Commits (TDD: RED then GREEN per task)

| Task | Phase | Commit  | Message |
| ---- | ----- | ------- | ------- |
| 1    | RED   | fc804a03 | test(06-01): add failing tests for ConsensusVote slot-hash fields |
| 1    | GREEN | a33a2aae | feat(06-01): extend ConsensusVote with slot_0/1/2_hash bytes fields |
| 2    | RED   | cd409da2 | test(06-01): add failing tests for PublicChainInputValidator::GetSlotHash |
| 2    | GREEN | cf86d6a7 | feat(06-01): add PublicChainInputValidator::GetSlotHash accessor |
| 3    | RED   | b1d66381 | test(06-01): add failing tests for ConsensusManager slot-hash populator |
| 3    | GREEN | e7fa5368 | feat(06-01): wire slot-hash populator from ConsensusManager to GeniusNode |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Blockchain forwarder for SetSlotHashPopulator**
- **Found during:** Task 3
- **Issue:** The plan's action for Task 3 Step C said "GeniusNode calls `consensus_manager_->SetSlotHashPopulator(...)`". However, `ConsensusManager` is owned privately by `Blockchain` (`std::shared_ptr<ConsensusManager> consensus_manager_` at Blockchain.hpp:534) with no public accessor. `GeniusNode` only holds `blockchain_`, not `consensus_manager_`.
- **Fix:** Added `Blockchain::SetSlotHashPopulator(...)` following the existing delegation pattern (`RegisterSubjectHandler`, `RegisterCertificateHandler`, etc. at Blockchain.cpp:1669+), which forwards to `consensus_manager_->SetSlotHashPopulator(...)`. GeniusNode calls the Blockchain-level forwarder.
- **Files modified:** src/blockchain/Blockchain.hpp, src/blockchain/impl/Blockchain.cpp, src/account/GeniusNode.cpp
- **Commit:** e7fa5368

**2. [Rule 2 - Missing critical functionality] Single-chain resolver accessor**
- **Found during:** Task 3
- **Issue:** The plan's `SlotHashPopulator` callback signature is `std::function<void(ConsensusVote&)>` (no chain_id), and `CreateVote` does not pass chain context. The populator needs a chain_id to call `GetSlotHash`, but no public accessor exposed the configured chain set.
- **Fix:** Added `PublicChainInputValidator::GetFirstConfiguredChainId()` (read-only, additive, D-10 compliant) so the populator lambda can resolve a chain id for single-chain deployments. Multi-chain resolution (reading chain_id from the proposal subject) is documented as a future enhancement.
- **Files modified:** src/account/PublicChainInputValidator.hpp
- **Commit:** e7fa5368

### Plan wording vs. implementation notes

- The plan said to use `spdlog::debug()` for logging. The actual codebase pattern in the modified files is `base::Logger` / `ConsensusManagerLogger()->debug(...)` / `InputValidatorLogger()->debug(...)` / `node_logger_->debug(...)`. I followed the existing per-file logging pattern (ConsensusAuth.hpp already uses `crypto::HasherImpl`; PublicChainInputValidator.cpp uses a local `InputValidatorLogger()`; Consensus.cpp uses `ConsensusManagerLogger()`; GeniusNode.cpp uses `node_logger_`). This matches the project's logging infrastructure rather than calling spdlog directly.

## Build / Test Execution

The verification commands in the plan (`cd build/OSX/Debug && ninja && ctest ...`) were **not executed by the agent**. This worktree has no populated `build/OSX/Debug` directory, and per `CLAUDE.md` the user owns the build (thirdparty is built separately, full regression is the user's responsibility before push). The user must run:

```bash
cd build/OSX/Debug
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
ninja
ctest -R "consensus_vote_slot_test|public_chain_input_validator_slot_test|consensus_slot_hash_populator_test|ConsensusCertificate|PublicChainInputValidator|Consensus|GeniusNode" -j8 --output-on-failure
```

Expected: `consensus_certificate_test` remains commented out (pre-existing); the three new Phase 6 tests should pass once protos regenerate. If the proto regeneration or the `sha2_256(const void*, size_t)` overload resolution differ on the build host, the user should report back.

## Known Limitations

- **Single-chain resolution:** `GetFirstConfiguredChainId()` returns the first entry from an `unordered_map`, which is not iteration-stable across runs. This is acceptable for the Phase 6 single-chain target (exactly one configured chain). Multi-chain support (resolving chain_id from the vote's `proposal_id` subject) is deferred — documented inline in `GeniusNode.cpp` and `PublicChainInputValidator.hpp`.

## Known Stubs

None. All slot-hash fields are fully populated through the wired callback; no hardcoded empty/mock data flows to production rendering. The abstain (empty-bytes) path is the specified D-05 sentinel, not a stub.

## Threat Flags

None. The implemented surface matches the plan's threat model exactly: slot hashes are inside the signing surface (T-06-01 mitigated), the callback is set once at init by the trusted boot path (T-06-02 mitigated), and URL hashing reveals no secrets (T-06-03 accepted).

## Self-Check: PENDING

Self-check deferred — the agent did not run `ninja`/`ctest` (see Build / Test Execution). The following must be confirmed by the user before marking the self-check PASSED:

- `grep -c "slot_0_hash" src/blockchain/impl/proto/Consensus.proto` returns >= 1 (verified by agent: count = 2).
- `ninja` regenerates `Consensus.pb.h` with `slot_0_hash()`/`set_slot_0_hash()` accessors.
- `VoteSigningBytes` output changes when a slot hash is set (asserted by `consensus_vote_slot_test`).
- The three new test targets build and pass.
- Pre-existing `Consensus`/`GeniusNode`/validator tests remain green.
