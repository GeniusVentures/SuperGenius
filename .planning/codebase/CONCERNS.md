# Codebase Concerns

**Analysis Date:** 2026-05-25

## Tech Debt

### Blank Outcome Error Codes — No Diagnostic Information
- Issue: 88+ locations return `outcome::failure(boost::system::error_code{})` — a default-constructed, zero-value error code that provides zero diagnostic context for debugging failures.
- Files: `src/account/TransactionManager.cpp` (6 instances), `src/crdt/impl/crdt_datastore.cpp` (17 instances), `src/crdt/impl/atomic_transaction.cpp` (8 instances), `src/crdt/impl/crdt_set.cpp` (12 instances), `src/crdt/impl/crdt_heads.cpp` (3 instances), `src/crdt/impl/graphsync_dagsyncer.cpp` (6 instances), `src/crdt/globaldb/keypair_file_storage.cpp` (8 instances), `src/crdt/globaldb/pubsub_broadcaster.cpp` (2 instances), `src/crdt/globaldb/pubsub_broadcaster_ext.cpp` (3 instances), `src/processing/impl/processing_task_queue_impl.cpp` (5 instances), `src/local_secure_storage/impl/json/JSONSecureStorage.cpp` (2 instances), `src/proof/NilFileHelper.hpp` (3 instances), `src/singleton/CComponentFactory.cpp` (1 instance), and all `Migration*.cpp` files (2 per file, ~10 total).
- Impact: When a failure occurs in production, there is no way to determine what went wrong — all these failure paths report the same unhelpful error code. Root-cause analysis requires code inspection or debugger attachment.
- Fix approach: Replace each blank error code with a domain-specific error enum value (e.g., `crdt::CrdtError::KEY_NOT_FOUND`, `processing::ProcessingError::QUEUE_EMPTY`) logged via the project's outcome pattern. Use `boost::system::errc::make_error_code()` or custom error categories for semantic meaning.

### Pervasive `std::this_thread::sleep_for` in Tests — Non-Deterministic and Slow
- Issue: 96+ instances of `std::this_thread::sleep_for` in test files with sleeps ranging from 1ms to 40 seconds (e.g., `processing_multi_test.cpp` uses 40-second sleeps). This violates the project's own testing discipline rule: "NEVER use std::this_thread::sleep_for in tests."
- Files: `test/src/processing_nodes/` (multiple), `test/src/processing_multi/processing_multi_test.cpp` (40s sleeps), `test/src/blockchain/blockchain_genesis_test.cpp` (8s sleeps), `test/src/transaction_sync/` (multiple), `test/src/processing/processing_service_test.cpp`, `test/src/processing/processing_engine_test.cpp`, `test/src/processing/processing_subtask_validation_test.cpp`, `test/src/crdt/crdt_atomic_transaction_test.cpp`, `test/src/graphsync/pubsub_graphsync_test.cpp`, `test/src/crdt/globaldb_integration.cpp`, `test/src/multiaccount/multi_account_sync.cpp`, `test/src/pubsub_counts/pubsub_counts.cpp`, `test/src/processing/processing_subtask_queue_accessor_impl_test.cpp`.
- Impact: Tests are slow (40s + 40s + 8s + 8s + … = minutes of wasted sleep time), non-deterministic (CI flakiness), and mask real race conditions rather than proving correctness.
- Fix approach: Migrate all test waits to the project-provided wait-condition template at `test/testutil/wait_condition.hpp` using `ASSERT_WAIT_FOR_CONDITION` or `EXPECT_WAIT_FOR_CONDITION` macros with condition-variable–backed polling. This was explicitly designed for this purpose but most tests ignore it.

### 19 DISABLED Tests — Untested Critical Functionality
- Issue: 19 test cases are explicitly disabled with `DISABLED_` prefix in GTest. These cover critical areas including processing node operations, transaction crash recovery, multi-account sync, consensus authorization checks, blockchain genesis validation, runtime core execution, and ED25519 signing.
- Files: `test/src/processing_nodes/processing_nodes_test.cpp` (5 disabled), `test/src/transaction_sync/transaction_crash_test.cpp` (1 disabled), `test/src/multiaccount/multi_account_sync.cpp` (2 disabled), `test/src/blockchain/blockchain_genesis_test.cpp` (2 disabled), `test/src/runtime/core_integration_test.cpp` (3 disabled), `test/src/processing/processing_service_test.cpp` (1 disabled), `test/src/crdt/globaldb_integration.cpp` (1 disabled), `test/src/scale/scale_collection_test.cpp` (1 disabled), `test/src/crypto/ed25519/ed25519_provider_test.cpp` (2 disabled).
- Impact: Regressions in crash recovery, multi-account synchronization, blockchain genesis authorization, and signature verification may go undetected. The compilation failure noted in `test/mock/src/runtime/production_api_mock.hpp` suggests some were disabled due to build breakage.
- Fix approach: Triage each disabled test: fix the underlying failure, update to current APIs, or delete if the functionality is deprecated. Prioritize crash recovery, genesis validation, and crypto tests.

### Massive Auto-Generated Circuit Files Committed to Source
- Issue: `src/proof/circuits/RecursiveTransactionCircuit.hpp` is a 71,718-line file containing LLVM IR bytecode as a string literal. `src/proof/circuits/TransactionVerifierCircuit.hpp` is 806 lines of the same nature. These are compiled output from a ZK circuit toolchain, not human-authored source code.
- Files: `src/proof/circuits/RecursiveTransactionCircuit.hpp` (71,718 lines), `src/proof/circuits/TransactionVerifierCircuit.hpp` (806 lines).
- Impact: Every small change to the circuit regenerates a multi-thousand-line diff that is unreviewable in code review. The files bloat the repository and increase clone times. They obscure the fact that these are generated assets.
- Fix approach: Move circuit bytecode generation to a build step. Store the source circuit definitions (e.g., `.cpp` circuit files) and have CMake invoke the ZK toolchain to generate the IR bytecode at compile time. The generated output should go to the build directory, not the source tree.

### EvmMessagingWatcher Uses Raw WebSocket eth_subscribe (Placeholder, Not evmrelay-Integrated)
- Issue: `src/watcher/impl/evm_messaging_watcher.*` connects to a single WebSocket endpoint and constructs `eth_subscribe` JSON-RPC payloads via string concatenation. No multi-provider quorum, no receipt verification, no P2P peer discovery. This was a prototype implementation.
- Files: `src/watcher/impl/evm_messaging_watcher.hpp`, `src/watcher/impl/evm_messaging_watcher.cpp`
- Impact: Single point of failure for EVM bridge observation. A compromised or unavailable RPC endpoint blocks all cross-chain minting. No independent verification of observed events.
- Fix approach: Migrate to use `evmrelay` as the Ethereum protocol library. `evmrelay` provides P2P peer discovery (discv4/discv5), multi-peer ETH subprotocol event watching, and `RpcManager` with multi-endpoint pool for independent receipt verification. `EvmMessagingWatcher` in `src/watcher/` becomes the orchestrator that receives verified observations from evmrelay and manages message handling lifecycle.

### `throw` in Utility Code Violates "No Exceptions" Policy
- Issue: `src/base/util.hpp` throws `std::invalid_argument` exceptions in `Vector2Num()`, `Vector2Num<uint128_t>()`, and `Vector2Num<uint256_t>()`. The CLAUDE.md explicitly states: "By default, generate code without exception handling. All functions should be declared noexcept unless explicitly required to throw." The `UNREACHABLE` macro in `src/macro/unreachable.hpp` also throws.
- Files: `src/base/util.hpp` (lines 76, 92, 108), `src/macro/unreachable.hpp` (line 16).
- Impact: These utility functions are called throughout the codebase. Exceptions propagate unexpectedly in a codebase that uses `outcome::result` for error handling, causing resource leaks (RAII may not unwind correctly if exceptions are unexpected).
- Fix approach: Refactor `Vector2Num` to return `outcome::result<T>` with a domain error on size overflow. Replace `UNREACHABLE` with `std::abort()` or a `BOOST_ASSERT` with a clear message, since reaching unreachable code is a programmer error, not a recoverable exception.

### Boost Test Header Leaked into Production Code
- Issue: `src/proof/GeniusProver.hpp` includes `<boost/test/unit_test.hpp>` — a test framework header — in production code. The comment acknowledges this: "TODO: remove this. Required only because of an incorrect assert check in zk."
- Files: `src/proof/GeniusProver.hpp` (line 18).
- Impact: Links the test framework into all translation units that include GeniusProver, potentially affecting production binaries. The header may define test macros that conflict with other code. Adds unnecessary compile-time dependency.
- Fix approach: Locate the zk library assert that depends on Boost.Test macros. Either fix the upstream assert to use standard `<cassert>` or wrap it with a local assertion macro. Remove the Boost.Test include once the dependency is broken.

### Non-Atomic UTXO Recording — Data Loss on Shutdown
- Issue: `src/account/UTXOManager.cpp` line 761 has a comment acknowledging that an operation is "not great because it's not atomic, so we lose the record and if we shutdown before we record it is gone."
- Files: `src/account/UTXOManager.cpp` (line 761).
- Impact: If the node shuts down between modifying UTXO state and persisting the record, the database and in-memory state become inconsistent. On restart, UTXO state may be incorrect, leading to double-spend vulnerabilities or lost funds.
- Fix approach: Use a write-ahead log (WAL) or batch the state mutation and record persistence atomically using a CRDT transaction. RocksDB already supports atomic batches — use `rocksdb::WriteBatch` to group the UTXO state change and the journal record into a single atomic write.

### Early Return Hides Unreachable Code in TransactionManager
- Issue: `src/account/TransactionManager.cpp` line 2310 has a TODO comment followed by `return;` that short-circuits initialization logic. Lines 2313–2335 below the return are dead code that requests missing transactions from peers but is never executed. Line 2855 has a `valid_proof = true; break;` that leaves lines 2857–2859 (actual proof verification) unreachable.
- Files: `src/account/TransactionManager.cpp` (lines 2310–2335, lines 2855–2859).
- Impact: The passive heads processing logic is effectively disabled but left in place. The proof verification code is bypassed for a CRDT workaround (line 2843: "This verification is only needed because CRDT resyncs every boot up"). Changes to CRDT behavior could inadvertently re-enable broken code paths.
- Fix approach: Remove the dead code blocks once the CRDT initialization redesign is complete. Until then, add `#if 0` guards or clear comments explaining why the code is intentionally unreachable.

### JSON Secure Storage Fallback — Private Keys on Disk as Plaintext JSON
- Issue: `src/local_secure_storage/impl/json/JSONSecureStorage.hpp` provides a JSON-backed implementation of `ISecureStorage` that stores data (including private keys) as JSON files on disk. While platform-specific secure storage exists (`Keychain` on macOS, `Keystore` on Android, etc.), the JSON backend is available as a fallback and does not encrypt data at rest.
- Files: `src/local_secure_storage/impl/json/JSONSecureStorage.hpp`, `src/local_secure_storage/impl/json/JSONBackend.hpp`.
- Impact: If the platform secure storage is unavailable and the JSON backend is used, private keys are stored in plaintext on disk. This is a significant security risk for any deployment where disk access is not fully controlled.
- Fix approach: Add at-rest encryption to the JSON backend using a platform-derived encryption key. If encryption is not possible, the JSON backend should log a prominent warning and require explicit user opt-in via configuration. Consider making the JSON backend debug-only.

### Debug-Only Code Paths with Different Behavior
- Issue: `src/account/GeniusNode.hpp` uses `#ifdef SGNS_DEBUG` to select different timeout values (50s for debug, 30s for release). `src/account/GeniusNode.cpp` uses `#ifndef SGNS_DEBUGLOGS` and `#ifdef SGNS_DEBUGLOGS` guards that change behavior. The `SGNS_DEBUG` macro in `GeniusNode.hpp` line 162 affects production logic (timeouts), not just logging.
- Files: `src/account/GeniusNode.hpp` (lines 162–170), `src/account/GeniusNode.cpp` (lines 494–497, 541–553).
- Impact: Code tested in debug mode runs with different timeouts than production. A race condition that appears at 30s but not at 50s will pass debug testing but fail in production. The debug-only libp2p log level settings at lines 541–553 are unconditional in debug builds but could mask log-level configuration.
- Fix approach: Move timeout values to runtime configuration, not compile-time macros. Keep `SGNS_DEBUG` for logging level changes only. Use a config file or environment variable for debug vs. release timeout tuning.

### Pervasive `shared_ptr` Despite Guidance to Prefer `unique_ptr`
- Issue: The CLAUDE.md guidance states: "Unique ownership: unique_ptr throughout, no shared_ptr." However, `shared_ptr` is used pervasively — 530+ instances across header files alone. Major components like `TransactionManager`, `Blockchain`, `ConsensusManager`, `GlobalDB`, `GeniusAccount`, and `ValidatorRegistry` all use `shared_ptr` for dependency injection and internal state.
- Files: `src/account/TransactionManager.hpp` (40+ shared_ptr), `src/blockchain/Blockchain.hpp` (30+ shared_ptr), `src/blockchain/Consensus.hpp` (25+ shared_ptr), `src/crdt/globaldb/globaldb.hpp` (45+ shared_ptr), and most other headers.
- Impact: `shared_ptr` has non-trivial overhead (atomic reference counting, control block allocation). It obscures ownership semantics — it's unclear who actually owns an object. Risk of reference cycles preventing cleanup (none observed yet but difficult to audit). Inconsistent with stated project standards.
- Fix approach: Audit dependency lifetimes. Many dependencies (e.g., `io_context`, `Hasher`, `Blockchain`) have clear single-owner lifetimes and should use `unique_ptr` with raw-pointer or reference passing to consumers. For truly shared ownership (e.g., the CRDT datastore shared between components), document the sharing explicitly. This is a long-term refactoring effort.

### Large God Files with Multiple Responsibilities
- Issue: Several source files exceed 1,000 lines and contain multiple distinct responsibilities:
  - `src/account/TransactionManager.cpp` — 4,923 lines (transaction parsing, validation, UTXO management, consensus bridge, CRDT operations)
  - `src/blockchain/Consensus.cpp` — 2,763 lines (certificate validation, proposal creation, voting logic, sync)
  - `src/blockchain/ValidatorRegistry.cpp` — 2,147 lines (validator management, reputation, staking)
  - `src/account/GeniusNode.cpp` — 1,938 lines (node lifecycle, processing, pricing, migration)
- Files: `src/account/TransactionManager.cpp`, `src/blockchain/Consensus.cpp`, `src/blockchain/ValidatorRegistry.cpp`, `src/account/GeniusNode.cpp`.
- Impact: Difficult to reason about, test in isolation, and review changes. High coupling between unrelated concerns. Changes to one area risk breaking another. Onboarding new developers is harder.
- Fix approach: Extract cohesive subsystems into separate classes/files: `UTXOManager` from `TransactionManager`, `CertificateValidator` and `ProposalBuilder` from `Consensus`, `ValidatorReputation` from `ValidatorRegistry`, `ProcessingPricing` from `GeniusNode`.

### Inconsistent Error Reporting in Production Paths
- Issue: Critical user-facing functions like `TransactionManager::TransferFunds` (line 576) and `TransactionManager::MintFunds` (lines 604, 661, 687) return blank `outcome::failure(boost::system::error_code{})` when the transaction manager is not in `READY` state. The caller receives no information about why the transfer or mint failed.
- Files: `src/account/TransactionManager.cpp` (lines 574–577, 596–604, 650–661, 680–687).
- Impact: Users see a generic "operation failed" with no actionable feedback. Support and debugging require log correlation rather than self-describing error codes.
- Fix approach: Define a `TransactionManager::Error` enum with values like `NOT_READY`, `INSUFFICIENT_FUNDS`, `INVALID_DESTINATION`. Return these domain-specific errors instead of blank boost error codes.

## Known Bugs

### Unreachable Proof Verification After CRDT Workaround
- Symptoms: Proof verification (lines 2857–2859 in `TransactionManager.cpp`) is never executed because a preceding `valid_proof = true; break;` at lines 2855–2856 unconditionally exits the loop. The CRDT existence check at line 2845 is treated as sufficient proof.
- Files: `src/account/TransactionManager.cpp` (lines 2845–2859).
- Trigger: The `processProofs` method is called. The CRDT datastore always contains the proof element (due to CRDT resync behavior noted in the TODO), so the existence check succeeds and the `break` is hit before verification.
- Workaround: None needed currently — the CRDT resync ensures the proof exists. However, if CRDT behavior changes (which is planned per the TODO), proofs would be accepted without verification, enabling forged transactions.

### UPnP Port Exhaustion Loop with Fixed Attempts
- Symptoms: `GeniusNode.cpp` lines 696–732 attempt UPnP port mapping up to 10 times. If all 10 attempts fail, the node proceeds without a port and logs an error, but continues operating in a degraded state where peers cannot connect.
- Files: `src/account/GeniusNode.cpp` (lines 696–732).
- Trigger: Running behind a NAT router that either doesn't support UPnP or has exhausted its port mapping table. More likely on networks with multiple SuperGenius nodes.
- Workaround: Manually configure port forwarding on the router. Not practical for end users.

### RocksDB String-to-Buffer Copy on Every Get
- Symptoms: `src/storage/rocksdb/rocksdb.cpp` line 118 has a FIXME noting an unavoidable string-to-Buffer copy on every `get()` call. RocksDB returns a `std::string` which must be copied into a `Buffer` for the application.
- Files: `src/storage/rocksdb/rocksdb.cpp` (line 118).
- Trigger: Every call to `rocksdb::get()`.
- Workaround: None. The copy is inherent to the current API design. Performance impact depends on value sizes and read frequency.

## Security Considerations

### JSON Secure Storage Stores Private Keys Without Encryption
- Risk: If the platform secure storage backend fails to initialize or is unavailable (e.g., running on an unsupported platform, keychain access denied), the JSON backend stores private keys as plaintext JSON files in a configurable directory. An attacker with filesystem access can read the keys directly.
- Files: `src/local_secure_storage/impl/json/JSONSecureStorage.hpp`, `src/local_secure_storage/impl/json/JSONSecureStorage.cpp`.
- Current mitigation: Platform-native secure storage (macOS Keychain, Android Keystore, Linux Secret Service) is preferred and selected at compile time via `SecureStorage.hpp`. The JSON backend is only a fallback.
- Recommendations: (1) Encrypt JSON storage with a key derived from a platform-specific secret (machine ID, TPM). (2) Log a prominent warning when falling back to JSON storage. (3) Require explicit configuration to enable the JSON backend in release builds. (4) Use file permissions (`chmod 600`) to restrict access.

### Missing Input Validation on External API Parameters
- Risk: Transaction parameters (amount, destination address, token ID, chain ID) are passed through to processing with minimal validation. A malformed destination address or negative/zero amount could propagate to CRDT storage before being caught.
- Files: `src/account/TransactionManager.cpp` (`TransferFunds`, `MintFunds`), `src/account/BridgeConsensusAdapter.cpp`.
- Current mitigation: Some validation exists in `InputValidators.cpp` and within `UTXOManager::CreateTxParameter`, but it's distributed and incomplete.
- Recommendations: Add a centralized input validation layer at the `TransactionManager` public API boundary that validates all transaction parameters before enqueueing. Validate address format, positive non-zero amounts, known token IDs, and supported chain IDs.

### Bridge Mint Verification Gap — No evmrelay RPC Integration for Transaction Verification
- Risk: The mint transaction path does not yet use evmrelay's `RpcManager` / `RpcReceiptSource` to independently verify bridge events via multiple RPC endpoints before minting. Mint verification currently depends on the single WebSocket observation from `EvmMessagingWatcher`.
- Files: `src/account/MintTransaction.cpp`, `src/watcher/impl/evm_messaging_watcher.cpp`
- Current mitigation: None. The evmrelay integration pipeline is planned but not yet implemented in the mint flow.
- Recommendations: Integrate evmrelay's `RpcManager` for multi-provider receipt verification before constructing `MintTransaction`. Use evmrelay's `verify_receipt_log()` and `BridgeEventClaim` types to validate observed claims.

### UPnP Port Mapping Could Expose Internal Services
- Risk: The UPnP implementation in `GeniusNode.cpp` requests port forwarding on the router. If the wrong port is mapped or the UPnP implementation has a vulnerability, internal services could be exposed to the internet.
- Files: `src/account/GeniusNode.cpp` (lines 696–732).
- Current mitigation: Limited to 10 attempts with specific port requests.
- Recommendations: Validate that the mapped port matches the requested port before accepting the mapping. Log a warning if the mapped port differs. Consider making UPnP opt-in rather than automatic.

## Performance Bottlenecks

### Proof Verification Bypassed (But Complete Proof Data Is Fetched and Stored)
- Problem: The full proof data is fetched, deserialized, and stored in CRDT, but the actual cryptographic verification is skipped (see bug above). This wastes CPU, memory, and network bandwidth.
- Files: `src/account/TransactionManager.cpp` (lines 2840–2859).
- Cause: CRDT resync on every boot makes the existence check sufficient, so verification was short-circuited as a performance optimization.
- Improvement path: Once CRDT persistence is fixed to eliminate re-sync on boot, re-enable verification. Until then, avoid fetching the full proof body if verification is going to be skipped — store only the proof hash and fetch the body lazily when verification is needed.

### Thread-Sleep–Based Polling in Production Code Creates Latency
- Problem: Multiple production code paths use `std::this_thread::sleep_for` for polling/retry loops: `TransactionManager.cpp` (100ms sleeps on signature polling), `crdt_datastore.cpp` (configurable sleep on head processing), `globaldb.cpp` (retry sleep), `GeniusNode.cpp` (5-second sleeps on port mapping, 50–100ms on various retries), `coinprices.cpp` (1.1-second sleeps between API calls), `processing_subtask_queue_manager.cpp` (configurable delay between processing cycles), `AccountMessenger.cpp` (10ms sleep in retry loops).
- Files: `src/account/TransactionManager.cpp` (3 instances), `src/crdt/impl/crdt_datastore.cpp` (2 instances), `src/crdt/globaldb/globaldb.cpp`, `src/account/GeniusNode.cpp` (7 instances), `src/coinprices/coinprices.cpp` (2 instances), `src/processing/processing_subtask_queue_manager.cpp`, `src/account/AccountMessenger.cpp` (3 instances), `src/account/MigrationManager.cpp`, `src/account/Migration3_6_0To3_7_0.cpp` (4 instances), `src/account/Migration3_4_0To3_5_0.cpp` (3 instances), `src/crdt/impl/graphsync_dagsyncer.cpp`.
- Cause: Thread sleep is the simplest way to implement a retry loop.
- Improvement path: Replace sleep-based polling with event-driven or callback-based patterns. Use `boost::asio` timers for scheduled retries, or condition variables for producer-consumer patterns. At minimum, reduce hardcoded sleep durations to configuration values.

### RocksDB String-to-Buffer Copy on Every Get
- Problem: Every `rocksdb::get()` call incurs a heap allocation and copy from `std::string` (RocksDB's return type) to `Buffer` (the application's buffer type). In read-heavy workloads, this adds measurable overhead.
- Files: `src/storage/rocksdb/rocksdb.cpp` (line 118).
- Cause: RocksDB API returns `std::string`. The `Buffer` class does not offer a move-from-string constructor or a way to adopt an existing allocation.
- Improvement path: Add a `Buffer(std::string&&)` constructor that takes ownership of the string's buffer. Alternatively, use `rocksdb::PinnableSlice` to avoid the copy for frequently accessed values. Note: the FIXME comment acknowledges this is a known issue.

## Fragile Areas

### TransactionManager — Massive God Class with 60+ TODO Comments
- Files: `src/account/TransactionManager.cpp` (4,923 lines), `src/account/TransactionManager.hpp` (682 lines).
- Why fragile: Handles transaction parsing, UTXO management, consensus bridging, CRDT integration, proof processing, Merkle operations, and signature verification — all in one class. 60+ TODO comments indicate known incomplete or incorrect behavior. Blank error codes hide failure causes. Dead code blocks (lines 2310–2335, 2855–2859) make control flow confusing. Changes to any subsystem risk breaking TransactionManager.
- Safe modification: Always read the full method before modifying. Check for dead code below any `return` you add. Prefer adding new functionality in separate classes and injecting them via the constructor pattern used by other recent additions (e.g., `BridgeConsensusAdapter`). Use the existing `transaction_parsers` dispatch map (lines 91–100) to add new transaction types.
- Test coverage: 49 test files exist but core transaction processing flows lack comprehensive unit tests. Most tests are integration tests that exercise the full node stack, making them slow and hard to debug.

### Consensus — Certificate Validation and Voting Logic Tightly Coupled
- Files: `src/blockchain/Consensus.cpp` (2,763 lines), `src/blockchain/Consensus.hpp` (692 lines).
- Why fragile: Certificate validation, proposal creation, voting logic, and synchronization are all in one class. Changes to certificate format or voting rules require careful review of the entire file. The TODO at line 1734 ("maybe see reputation?") suggests incomplete validator behavior.
- Safe modification: Keep certificate validation logic separate from voting logic. Add new message types to the `MessageType` enum and handle them in their own methods. Do not add more boolean flags to the constructor — use the Strategy pattern for pluggable validation rules.
- Test coverage: `test/src/blockchain/consensus_subject_test.cpp`, `test/src/blockchain/consensus_certificate_test.cpp`, and `test/src/blockchain/blockchain_genesis_test.cpp` exist but two of the genesis tests are DISABLED.

### CRDT Datastore — Core Data Layer with Many TODOs
- Files: `src/crdt/impl/crdt_datastore.cpp` (1,940 lines).
- Why fragile: The CRDT datastore is the foundation for all state in the system. 17 blank error codes make failure diagnosis impossible. TODO comments note incomplete filtering (line 1925), missing tombstone handling (line 1928), and missing prefix queries (line 140 in header). The resync-on-every-boot behavior creates a workaround that cascades into TransactionManager.
- Safe modification: Test all changes with `test/src/crdt/crdt_datastore_test.cpp` and `test/src/crdt/crdt_atomic_transaction_test.cpp`. Any change to the data format must be backward-compatible or include a migration step.
- Test coverage: Core CRDT operations are tested but edge cases (tombstone handling, concurrent modifications, network partitions) likely have gaps.

### Proof System — Test Framework Dependency in Production Code
- Files: `src/proof/GeniusProver.hpp`, `src/proof/GeniusAssigner.hpp`, `src/proof/GeniusProver.cpp`, `src/proof/GeniusAssigner.cpp`.
- Why fragile: The proof system depends on external ZK toolchain libraries (`nil/crypto3`). A test framework header (`<boost/test/unit_test.hpp>`) is included in production code. Circuits are compiled to 71k-line LLVM IR strings and committed to source.
- Safe modification: Do not change the circuit bytecode files directly — regenerate them from the circuit source using the ZK toolchain. Keep the Boost.Test dependency as-is until the upstream zk library assert is fixed.
- Test coverage: `test/src/proof/ProverTest.cpp` and `test/src/proof/GeniusProofsTest.cpp` exist.

### Migration System — Version-Specific Code with Duplicated Patterns
- Files: `src/account/Migration3_6_0To3_7_0.cpp` (587 lines), `src/account/Migration3_4_0To3_5_0.cpp`, `src/account/Migration3_5_0To3_6_0.cpp`, `src/account/Migration1_0_0To3_4_0.cpp`, `src/account/Migration0_2_0To1_0_0.cpp`.
- Why fragile: Each migration is a separate file that copies patterns from previous migrations. Shared bugs (blank error codes, sleep_for retry loops) are duplicated. The migration chain is linear — if one migration fails, the entire chain breaks.
- Safe modification: Do not copy code from older migrations. Extract common retry/error patterns into `MigrationManager` base utilities. Test each migration with a known database state.
- Test coverage: `test/src/transaction_sync/migration_sync_test.cpp` exists but uses sleep_for.

## Scaling Limits

### Single RocksDB Instance for All State
- Current capacity: One RocksDB instance backs the CRDT datastore, blockchain state, UTXO state, and migration journals.
- Limit: Write throughput is bounded by a single disk. As transaction volume grows, the single database becomes a bottleneck. Large datasets may cause compaction stalls.
- Scaling path: Shard by key prefix to multiple column families within RocksDB. For horizontal scaling, partition by address range across multiple RocksDB instances.

### Single-Threaded CRDT Head Processing
- Current capacity: CRDT head processing runs in a single processing loop with sleep-based polling.
- Limit: As the number of concurrent transactions and CRDT heads grows, processing latency increases linearly.
- Scaling path: Parallelize head processing across multiple threads using a work-stealing queue. Replace sleep-based polling with event-driven notification from the CRDT datastore.

## Dependencies at Risk

### ZK Proof Libraries (nil/crypto3)
- Risk: The proof system depends on `nil/crypto3` which has an incorrect assert that depends on `<boost/test/unit_test.hpp>`. Any update to the nil/crypto3 library could break the proof system. The library API may change.
- Impact: Proof generation and verification would break, blocking all transactions.
- Migration plan: Monitor nil/crypto3 releases. Maintain a pinned version. Upstream the assert fix.

### Protobuf Definitions
- Risk: Transaction and CRDT protocols are defined in `.proto` files (`src/account/proto/SGTransaction.pb.h`, `src/crdt/proto/delta.pb.h`). Breaking changes to these protos require coordinated upgrades across all nodes.
- Impact: Forks if nodes disagree on message format.
- Migration plan: Version the proto schemas. Add new fields as optional before making them required. Maintain backward compatibility for at least one version.

## Missing Critical Features

### No At-Rest Encryption for Local Storage
- Problem: Private keys, account state, and transaction history are stored in RocksDB without at-rest encryption. The JSON secure storage fallback stores keys in plaintext.
- Blocks: Deployment on shared infrastructure, compliance with security standards.

### No Comprehensive Transaction Fee Estimation
- Problem: Users cannot predict the cost of a transaction before submitting it. The processing pricing in `GeniusNode` calculates costs after processing, not before.
- Blocks: Good user experience for wallet integration.

### No Graceful Degradation When External Services Are Unavailable
- Problem: If `coinprices` fails to retrieve GNUS prices, the `GeniusNode` returns `NO_PRICE` error for all processing requests. There is no cached fallback price.
- Blocks: Reliable operation during exchange API outages.

## Test Coverage Gaps

### Transaction Crash Recovery
- What's not tested: The crash recovery test (`DISABLED_TransactionSyncAfterCrash`) is disabled. There is no test proving that a node can recover transaction state after an unclean shutdown.
- Files: `test/src/transaction_sync/transaction_crash_test.cpp`.
- Risk: A crash during transaction processing could leave inconsistent UTXO state. Undetected until users report lost funds.
- Priority: High.

### Core Runtime Execution
- What's not tested: Three core runtime tests (`DISABLED_ExecuteBlockTest`, `DISABLED_InitializeBlockTest`, `DISABLED_AuthoritiesTest`) are disabled. The mock file notes "Disabled testing, failed to compile."
- Files: `test/src/runtime/core_integration_test.cpp`, `test/mock/src/runtime/production_api_mock.hpp`.
- Risk: The runtime execution path is completely untested. Changes to block execution or authority management go unvalidated.
- Priority: High.

### ED25519 Signing Edge Cases
- What's not tested: `DISABLED_SignWithInvalidKeyFails` and `DISABLED_VerifyInvalidKeyFail` are disabled.
- Files: `test/src/crypto/ed25519/ed25519_provider_test.cpp`.
- Risk: Invalid key handling may not reject malformed signatures or keys correctly, leading to signature forgery vulnerabilities.
- Priority: High.

### Multi-Account Synchronization
- What's not tested: `DISABLED_SyncThroughEachOther` and `DISABLED_CRDTFilterDuplicateTx` are disabled.
- Files: `test/src/multiaccount/multi_account_sync.cpp`.
- Risk: Multi-account wallet scenarios may have undetected synchronization bugs — duplicate transactions or missed sync events.
- Priority: Medium.

### Production Code Without Corresponding Tests
- What's not tested: No dedicated unit tests found for `GeniusNode` processing flows, `AccountMessenger` request handling, `ValidatorRegistry` reputation calculations, or `Consensus` voting logic.
- Files: `src/account/GeniusNode.cpp`, `src/account/AccountMessenger.cpp`, `src/blockchain/ValidatorRegistry.cpp`, `src/blockchain/Consensus.cpp`.
- Risk: These are the most complex components in the system but rely solely on integration tests that exercise the full node stack. Isolating failures requires time-consuming debugging.
- Priority: Medium.

---

*Concerns audit: 2026-05-25*
