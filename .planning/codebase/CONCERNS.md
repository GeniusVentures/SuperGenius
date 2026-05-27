# Codebase Concerns

**Analysis Date:** 2026-05-27

## Tech Debt

### Versioned Migration Chain (6 steps, 5 versions)
- Issue: Migration steps from v0.2.0 through v3.7.0 are accumulated and run sequentially when a legacy database is detected during upgrade. Each step involves database transformations that can fail. Brand new installs skip all migrations — they only affect upgrades from old versions.
- Files: `src/account/Migration0_2_0To1_0_0.{cpp,hpp}`, `src/account/Migration1_0_0To3_4_0.{cpp,hpp}`, `src/account/Migration3_4_0To3_5_0.{cpp,hpp}`, `src/account/Migration3_5_0To3_6_0.{cpp,hpp}`, `src/account/Migration3_5_0To3_5_1.{cpp,hpp}`, `src/account/Migration3_6_0To3_7_0.{cpp,hpp}`, `src/account/MigrationManager.{cpp,hpp}`, `src/account/IMigrationStep.hpp`
- Impact: Long upgrade times for legacy users, increased chance of failure during multi-step migration, difficulty testing all migration paths from every historical version.
- Fix approach: Consider collapsing the migration chain into fewer steps for upgrades from very old versions. Add integration tests that migrate from each historical version.

### ~40+ Unresolved TODO/FIXME Comments in Production Code
- Issue: TODO/FIXME markers are scattered across critical paths without tracking (no issue numbers or assignees).
- Files: see full list below
- Impact: Unknown criticality — some TODOs may represent subtle bugs waiting to manifest.
- Fix approach: Triage all TODOs into a tracking system. Categorize as bug, feature, or tech-debt. Prioritize those in consensus, transaction validation, and UTXO paths.

### Non-Atomic UTXO Persistence
- Issue: The UTXO storage snapshot iterates key-by-key with individual `remove` + `put` calls. If the process crashes mid-snapshot, stored state becomes inconsistent with in-memory state.
- Files: `src/account/UTXOManager.cpp` line 758
- Impact: Data loss/corruption on abrupt shutdown. RocksDB state not flushed to disk may be lost.

### Orphan Block Storage Leak
- Issue: Side-chain blocks that get rejected by finalization are never deleted from storage.
- Files: `src/blockchain/impl/key_value_block_storage.cpp` line 208
- Impact: Disk space grows indefinitely on nodes that encounter orphan/forked blocks.
- Fix approach: Implement a pruning mechanism triggered by finalization that removes blocks not on the canonical chain.

### Polling-Based Send Loop in RLPx Session
- Issue: The send loop uses a polling timer (`asio::steady_timer`) with a hardcoded poll interval instead of an async condition variable.
- Files: `evmrelay/src/rlpx/rlpx_session.cpp` lines 604-615
- Impact: Adds unnecessary latency between messages (at minimum the poll interval). Wastes CPU cycles on idle connections.
- Fix approach: Replace the timer-based polling with a proper `boost::asio::experimental::channel` or condition variable that wakes only when messages are queued.

### RocksDB Copy Overhead
- Issue: Every RocksDB read copies the stored string value into a Buffer via `Buffer{}.put(value)`.
- Files: `src/storage/rocksdb/rocksdb.cpp` line 118
- Impact: Unnecessary memory allocation and copy on every read. In read-heavy workloads this is significant overhead.
- Fix approach: Use RocksDB's `PinnableSlice` to avoid the copy, or use a `BufferView` wrapper.

### Boost Test Header in Production Code
- Issue: `boost/test/unit_test.hpp` is included in a production header solely because of an incorrect assert check in zkLLVM.
- Files: `src/proof/GeniusProver.hpp` line 18
- Impact: Production builds link against test framework symbols. Potential symbol conflicts. Inflates binary size.
- Fix approach: Replace the zkLLVM assert check or wrap it in a conditional header that uses a standard assertion in production builds.

### Fallback to Hardcoded Topic String
- Issue: The codebase uses string literal `"SuperGNUSNode.TestNet.FullNode"` in multiple places rather than a centralized constant.
- Files: `src/crdt/impl/crdt_datastore.cpp` line 1904
- Impact: May cause issues if the topic name ever changes. Existing constants `GNUS_FULL_NODES_TOPIC` and `GNUS_FULL_NODES_TOPIC_LEGACY` are defined in `TransactionManager.hpp` but not consistently used.
- Fix approach: Use the existing constants everywhere. Remove hardcoded strings.

## Known Bugs

### ProductionApiMock Disabled (Compilation Failure)
- Symptoms: The mock for `ProductionApi` cannot be compiled. The `MOCK_METHOD0` macro line is commented out.
- Files: `test/mock/src/runtime/production_api_mock.hpp` line 12
- Trigger: Attempting to use `ProductionApiMock` in any test.
- Workaround: Tests needing a mock ProductionApi must use a different approach or a hand-rolled stub.

### Dead Code / Bypassed Proof Verification
- Symptoms: In `FilterProof`, the code sets `valid_proof = true` then `break`s, making the subsequent `IBasicProof::VerifyFullProof()` call unreachable.
- Files: `src/account/TransactionManager.cpp` lines 2828-2830
- Trigger: Whenever `FilterProof` processes elements that aren't already in the DB.
- Impact: **This is a critical security bug.** All zero-knowledge proof verification is currently skipped. Malicious nodes can inject invalid transaction proofs that will be accepted as valid. The `do { ... } while (0)` block at lines 2814-2847 contains the actual verification code, but a premature `valid_proof = true; break;` at lines 2828-2829 skips it entirely.
- Fix approach: Remove the premature `valid_proof = true; break;` at lines 2828-2829. Re-enable `VerifyFullProof`. Consider adding unit tests that verify invalid proofs are correctly rejected.

## Security Considerations

### Public Chain Validator Accepts All External References
- Risk: `VerifyPublicChainSmartContract` is a stub that always returns `true`, accepting any external source reference without validating on-chain contract state. A malicious node can mint tokens by claiming arbitrary external transactions.
- Files: `src/account/InputValidators.cpp` lines 444-451
- Current mitigation: The comment acknowledges this is a "placeholder for real burn/finality/contract validation" and notes it is accepted "for bootstrap/test mints."
- Recommendations: Implement actual on-chain validation against the external public chain before deploying to production. Consider requiring consensus certificates that cross-validate the external chain state.

### Thread-Unsafe Singleton Pattern
- Risk: The `CSingleton<T>` template has a comment `"// start lock for multithreading here"` but no actual mutex/lock. Concurrent calls to `Instance()` during initialization create race conditions.
- Files: `src/singleton/Singleton.hpp` lines 37-38
- Current mitigation: Singleton is used for component factories where initialization is likely single-threaded, but this is not guaranteed.
- Recommendations: Either add proper `std::call_once`/`std::mutex` protection or replace with a dependency injection pattern. The placement-new pattern `new (_instance) T()` on an already-allocated pointer is particularly unusual and risky.

### Scalable Integer Vulnerability Surface
- Risk: The codebase uses custom fixed-point arithmetic via `ScaledInteger` for token amounts. Custom numeric types are historically a source of overflow and rounding bugs.
- Files: `src/base/ScaledInteger.{cpp,hpp}`, `src/account/TokenAmount.{cpp,hpp}`
- Current mitigation: Uses Boost multiprecision types internally.
- Recommendations: Add property-based tests for edge cases (overflow, underflow, rounding, zero values, negative inputs). Audit all arithmetic operations for overflow checks.

### `using namespace nil;` in Header
- Risk: Pollutes the global namespace for all compilation units that include this header.
- Files: `src/proof/GeniusProver.hpp` line 49
- Current mitigation: None.
- Recommendations: Remove the using-directive from the header. Use explicit namespace qualification or keep it in the `.cpp` file only.

### Missing Reputation System for Malicious Peers
- Risk: Nodes sending invalid proofs, incorrect votes, or malicious transactions face no consequences. A malicious actor can spam the network with invalid data at low cost.
- Files: `src/account/TransactionManager.cpp` line 2834, `src/blockchain/Consensus.cpp` line 1742
- Current mitigation: Invalid submissions are logged and ignored, but the peer is not penalized or blacklisted.
- Recommendations: Implement a key-based reputation system that reduces weight or blacklists peers submitting provably invalid data. Leverage the existing `ValidatorRegistry` infrastructure.

## Performance Bottlenecks

### Polling RLPx Send Loop
- Problem: The send loop busy-waits with a timer even when no messages are pending. On idle connections this wastes CPU.
- Files: `evmrelay/src/rlpx/rlpx_session.cpp` lines 604-615
- Cause: `send_channel_->try_pop()` returns nullopt when empty, then a timer-based sleep is used.
- Improvement path: Replace with a proper async channel that blocks until data arrives.

### Large Auto-Generated Circuit Header
- Problem: `RecursiveTransactionCircuit.hpp` is 71,718 lines (string-embedded LLVM IR bytecode). This inflates compile times and memory usage for every translation unit that includes it.
- Files: `src/proof/circuits/RecursiveTransactionCircuit.hpp`
- Cause: Auto-generated zkLLVM circuit bytecode embedded as a `static const std::string_view`.
- Improvement path: Move the bytecode to a `.cpp` file or an extern data file to avoid recompilation on every change in the proof module. Consider whether the string_view can be replaced with a `extern const char[]` to reduce per-TU overhead.

### Unnecessary String-to-Buffer Copies in RocksDB Reads
- Problem: Every key/value read from RocksDB copies the result string into a Buffer.
- Files: `src/storage/rocksdb/rocksdb.cpp` line 118, lines 141-144
- Cause: `Buffer{}.put(value)` always copies. Query operations do additional copies.
- Improvement path: Use RocksDB's PinnableSlice to reduce copies in the hot path.

## Fragile Areas

### TransactionManager (God Class)
- Files: `src/account/TransactionManager.{cpp,hpp}` (4829 + 673 lines)
- Why fragile: This class coordinates transaction creation, CRDT propagation, proof verification, status tracking, nonce requests, and cryptographic operations. It has 30+ member variables, complex state machine logic, and deeply nested callbacks. Changes in any sub-system risk introducing regressions.
- Safe modification: Keep changes small and focused. Add unit tests before refactoring. Consider splitting into TransactionCoordinator, ProofVerifier, and StatusTracker classes.
- Test coverage: Sparse — only integration-level tests exist in `test/src/`. No dedicated unit tests for `FilterProof`, `ValidateTransaction`, or the state machine transitions.

### ConsensusManager
- Files: `src/blockchain/Consensus.{cpp,hpp}` (2762 + 695 lines)
- Why fragile: Manages voting, proposal creation, certificate generation, registry updates, and pubsub message routing. Tight coupling with ValidatorRegistry. Multiple locks (proposals_mutex_, votes_mutex_) risk deadlocks.
- Safe modification: Audit lock ordering before adding new locks. Add deadlock detection tests.
- Test coverage: Minimal — consensus logic tested only via integration scenarios in processing tests.

### CRDT Datastore
- Files: `src/crdt/impl/crdt_datastore.cpp` (1940 lines), `src/crdt/crdt_datastore.hpp` (502 lines)
- Why fragile: Core data layer for the entire system. Uses weak_ptr callbacks for lifecycle management. Complex interaction with GraphSync, RocksDB, and broadcasting.
- Safe modification: Add tests for edge cases with missing CID lookups and concurrent Put/Delete operations.
- Test coverage: CRDT unit tests exist in `test/src/crdt/`.

### RLPx Session (EVM Relay)
- Files: `evmrelay/src/rlpx/rlpx_session.cpp` (762 lines)
- Why fragile: Handles Ethereum DevP2P protocol — crypto handshake, framing, compression, message dispatch. Inbound connection acceptance is completely unimplemented. The send loop uses polling.
- Safe modification: This module is documented as work-in-progress ("Phase 3.5" for inbound). Focus on completing inbound before adding new outbound features.
- Test coverage: No dedicated tests found. Tested only via the `eth_watch` example.

### Local Secure Storage
- Files: `src/local_secure_storage/impl/Android.cpp`, `src/local_secure_storage/impl/Apple.cpp`, `src/local_secure_storage/impl/Linux.cpp`, `src/local_secure_storage/impl/Windows.cpp`, `src/local_secure_storage/impl/KeyStoreHelper.java`
- Why fragile: Four completely independent platform implementations (Android KeyStore, Apple Keychain, Linux libsecret, Windows Credential Manager). Each has different error semantics and edge cases. The Android implementation uses JNI with complex ClassLoader resolution for finding the helper class.
- Safe modification: Test on all four platforms after any change. The Android module has a fragile initialization dependency chain where Java must call `KeyStoreHelper.initialize(context)` before C++ can use the storage.
- Test coverage: A JSON migration test exists (`test/src/local_secure_storage/json_migration_test.cpp`) but no dedicated per-platform storage tests.

### Graphsync DAG Syncer
- Files: `src/crdt/impl/graphsync_dagsyncer.cpp` (1080 lines)
- Why fragile: Manages peer discovery, CID routing, request retry logic, and blacklisting. Complex async interaction with IPFS libp2p host and GraphSync protocol.
- Safe modification: Add integration tests for peer discovery failures, CID routing timeouts, and blacklist scenarios.

## Scaling Limits

### Block Storage — One Block Data per Header
- Current capacity: One block-data entry per header (hardcoded "/tx/0" in the key path).
- Files: `src/blockchain/impl/key_value_block_storage.cpp` lines 197, 263, 406
- Limit: If the protocol ever needs multiple block-data entries per header, the storage schema would need a breaking change.
- Scaling path: Refactor to use indexed entries under the block header prefix.

### No Orphan Block Cleanup
- Current capacity: Orphan (non-finalized) blocks accumulate indefinitely. No pruning mechanism exists.
- Files: `src/blockchain/impl/key_value_block_storage.cpp` line 208
- Limit: Storage grows unbounded on nodes that observe forked chains.
- Scaling path: Implement a periodic pruning sweep after finalization that removes blocks not referencing the canonical chain.

### UPnP Port Retry Limit
- Current capacity: Hardcoded to 10 UPnP port mapping attempts.
- Files: `src/account/GeniusNode.cpp` lines 696-733
- Limit: If all 10 attempts fail to find an open port, the node logs an error and continues without UPnP mapping. This means the node will be unreachable from outside the local network.
- Scaling path: Consider falling back to relay-based connectivity or manual port configuration.

## Dependencies at Risk

### zkLLVM — Test Header Leak
- Package: nil/crypto3 (zkLLVM)
- Risk: The zkLLVM framework's assert macros pull in `boost/test/unit_test.hpp`, forcing production builds to link against Boost.Test.
- Impact: Binary bloat, potential symbol conflicts, build fragility.
- Migration plan: Work with the zkLLVM maintainers to separate test assertions from production assertions, or add a preprocessor guard.

### 6 Git Submodules
- Files: `.gitmodules` (submodules: gRPCForSuperGenius, GeniusKDF, ProofSystem, SGProcessingManager, docs, evmrelay)
- Impact: Complex CI/CD setup with multi-repo dependency chains. Version drift between submodules can cause subtle integration bugs. The thirdparty external dependency (not a submodule) adds another coordination point.
- Migration plan: Consider monorepo consolidation or pinning submodule commits with automated update workflows.

### External Thirdparty Dependency
- Risk: The `thirdparty` repository is an external dependency (not a submodule) that must be cloned separately and built first. The relative path convention (`../thirdparty`) is fragile.
- Impact: New developers must follow a multi-step setup. CI must pre-clone thirdparty. Path mismatches cause cryptic CMake errors.
- Migration plan: Document the dependency more clearly. Consider integrating thirdparty as a proper git submodule with pinned commits.

### Multiple Platform Build Targets
- Risk: The project builds for 5 platforms (Android arm64-v8a/armeabi-v7a, iOS, macOS, Linux x86_64/aarch64, Windows) across Debug/Release configurations.
- Impact: CI matrix is large (`.github/workflows/cmake.yml` ~623 lines, `build-release-tags.yml` ~435 lines). Build failures on niche platforms may go unnoticed.
- Migration plan: Add CI gates that require all platform builds to succeed before merge. Consider reducing platform surface if some targets have low adoption.

## Missing Critical Features

### No Inbound RLPx Connection Acceptance
- Problem: The EVM relay cannot accept inbound RLPx connections from Ethereum peers. It can only initiate outbound connections.
- Blocks: Passive monitoring of Ethereum network state. Nodes behind NAT cannot be discovered. Full two-way Ethereum bridge requires this.
- Files: `evmrelay/src/rlpx/rlpx_session.cpp` lines 462-466
- Priority: High for EVM bridge functionality.

### No Reputation/Penalty System
- Problem: Malicious or faulty nodes face no consequences for submitting invalid proofs, incorrect votes, or spam transactions.
- Blocks: Sybil resistance. Economic security of the consensus protocol.
- Files: `src/account/TransactionManager.cpp` line 2834, `src/blockchain/Consensus.cpp` line 1742
- Priority: High for production security.

### External Chain Contract Validation (Placeholder)
- Problem: Public-chain mint claims accept ALL external source references without validating on-chain state.
- Blocks: Secure token bridging from external chains.
- Files: `src/account/InputValidators.cpp` lines 444-451
- Priority: Critical before any bridge goes live.

### Orphan Block Cleanup
- Problem: No mechanism to remove blocks from rejected forks.
- Blocks: Long-running nodes will exhaust disk space.
- Files: `src/blockchain/impl/key_value_block_storage.cpp` line 208
- Priority: Medium for production nodes.

## Test Coverage Gaps

### No Unit Tests for TransactionManager::FilterProof
- What's not tested: The proof verification bypass bug (dead code at lines 2828-2830) went undetected because there are no unit tests for FilterProof.
- Files: `src/account/TransactionManager.cpp` lines 2810-2859
- Risk: Invalid proofs are silently accepted. Any fix to re-enable verification could break without detection.
- Priority: High

### No Dedicated Tests for Consensus Voting Logic
- What's not tested: Vote aggregation, quorum calculation, certificate generation, registry update triggers.
- Files: `src/blockchain/Consensus.cpp` (2762 lines)
- Risk: Consensus bugs could cause forks, double-spends, or network splits.
- Priority: High

### Disabled ProductionApiMock
- What's not tested: Runtime production API interactions.
- Files: `test/mock/src/runtime/production_api_mock.hpp`
- Risk: Production-related features cannot be unit tested.
- Priority: Low

### No Platform-Specific Secure Storage Tests
- What's not tested: Android KeyStore JNI path, Apple Keychain edge cases, Windows Credential Manager password length limits, Linux libsecret error handling.
- Files: `src/local_secure_storage/impl/`
- Risk: Platform-specific bugs in credential storage could lead to key loss on user devices.
- Priority: Medium

---

*Concerns audit: 2026-05-27*
