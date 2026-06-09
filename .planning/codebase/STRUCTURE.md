# Codebase Structure

**Analysis Date:** 2026-05-27

## Directory Layout

```
SuperGenius/
├── .clang-format                           # C++ code formatting rules (Microsoft-based, 120 cols)
├── .clang-tidy                             # Static analysis checks (boost, bugprone, cert, cppcoreguidelines)
├── .clangd                                 # LSP configuration for clangd
├── .github/workflows/                      # CI/CD pipeline definitions
├── .planning/codebase/                     # GSD codebase map documents
├── .vscode/                                # VS Code workspace settings
├── AgentDocs/                              # Agent documentation (Architecture.md, AGENT_MISTAKES.md, etc.)
├── cmake/                                  # CMake helper modules (config.cmake.in, functions.cmake, etc.)
├── build/                                  # Platform-specific CMake build directories (NOT committed)
│   ├── OSX/                                # macOS (x86 + ARM fat library)
│   ├── Linux/                              # Linux (x86_64, aarch64)
│   ├── Windows/                            # Windows (Visual Studio 2022)
│   ├── Android/                            # Android (armeabi-v7a, arm64-v8a, x86_64)
│   └── iOS/                                # iOS cross-compile
├── src/                                    # Core library source (namespace sgns::*)
│   ├── CMakeLists.txt                      # Top-level build (17 add_subdirectory entries)
│   ├── account/                            # UTXO ledger, transactions, GeniusAccount/GeniusNode
│   ├── api/transport/                      # HTTP/WebSocket JSON-RPC server
│   ├── base/                               # Buffer, Blob, Logger (spdlog), hex utilities, version
│   ├── blockchain/                         # Genesis bootstrap, ConsensusManager, BlockTree, BlockStorage
│   ├── coinprices/                         # CoinGecko price retrieval
│   ├── crdt/                               # CRDT datastore, GlobalDB, DAG syncer, PubSub broadcaster
│   ├── crypto/                             # ED25519, SR25519, secp256k1, VRF, BIP39, Hasher (blake2/keccak/sha2/twox)
│   ├── local_secure_storage/               # Platform-specific encrypted key storage
│   ├── macro/                              # Utility macros (unreachable.hpp)
│   ├── outcome/                            # outcome::result<T> adapter
│   ├── primitives/                         # Block, Extrinsic, Authority, Transaction core types
│   ├── processing/                         # Distributed task/subtask queue and execution engine
│   ├── proof/                              # zkSNARK provers, assigners, transfer/processing proof circuits
│   ├── scale/                              # SCALE binary codec (encoder/decoder streams)
│   ├── singleton/                          # IComponent / CComponentFactory DI container
│   ├── storage/                            # RocksDB, in-memory, Trie/MPT, changes trie, storage face interfaces
│   ├── subscription/                       # Templated publish/subscribe event bus
│   └── watcher/                            # EVM chain event watcher (WebSocket)
├── example/                                # Runnable example apps (10 entry points with their own main())
│   ├── node_test/                          # Primary integration example (NodeExample.cpp, 751 lines)
│   ├── processing_room/                    # Processing service demo
│   ├── crdt_globaldb/                      # CRDT GlobalDB usage demo
│   └── ...                                 # ipfs_client, ipfs_pubsub, evm_messaging_dapp, echo_client, etc.
├── test/                                   # Unit and integration tests
│   ├── CMakeLists.txt                      # Test root (includes testutil and test/src)
│   ├── mock/src/                           # Mock implementations for testing
│   ├── testutil/                           # Test utilities (color_support, literals, WaitCondition, outcome helpers)
│   └── src/                                # Test suites (mirrors src/ structure)
├── example/                                # Example apps and integration tests
│   ├── CMakeLists.txt
│   ├── crdt_globaldb/                      # CRDT GlobalDB usage example
│   ├── echo_client/                        # Echo client example
│   ├── evm_messaging_dapp/                 # EVM messaging dApp example
│   ├── ipfs_client/                        # IPFS client examples (two variants)
│   ├── ipfs_pubsub/                        # IPFS pubsub example
│   ├── mnn_chunkprocess/                   # MNN chunk processing example
│   ├── node_test/                          # Node integration tests
│   ├── processing_dapp/                    # Processing dApp example
│   ├── processing_json/                    # Processing with JSON input example
│   └── processing_room/                    # Processing room example
├── GeniusKDF/                              # [submodule] Key derivation function library
├── ProofSystem/                            # [submodule] Standalone proof system (ElGamal, ECDSA, AES, KDF, circuits)
├── SGProcessingManager/                    # [submodule] MNN-based ML inference engine
├── evmrelay/                               # [submodule] EVM RLP/Discv4/Discv5/RLPx relay library
├── gRPCForSuperGenius/                     # [submodule] OpenAPI REST + gRPC interface definitions
├── docs/                                   # [submodule] Documentation (Doxygen XML, Doxyfile)
├── CRDT.Datastore.TEST/                    # CRDT datastore integration test data
├── CRDT.Datastore.TEST.unit/               # CRDT datastore unit test data (RocksDB files)
├── Testing/Temporary/                      # Temporary test artifacts
├── Readme.md                               # Build instructions (CMake per platform)
├── LICENSE                                 # License file
└── .gitmodules                             # Submodule declarations (7 submodules)
```

## Directory Purposes

**src/:**
- Purpose: Core library containing all domain logic, interfaces, and implementations
- Contains: C++ headers (`.hpp`) and source files (`.cpp`), Protobuf schemas (`.proto`), CMake build files
- Key files: `src/CMakeLists.txt` (defines 17 subsystems), `src/account/GeniusAccount.hpp`, `src/blockchain/Blockchain.hpp`, `src/crdt/crdt_datastore.hpp`

**src/account/:**
- Purpose: Block-lattice UTXO ledger — account identity, transaction types, token management
- Contains: `GeniusAccount` (account identity/keys), `GeniusNode` (validator/processor node), `GeniusUTXO`, `TransferTransaction`, `MintTransaction` (V1 + V2), `ProcessingTransaction`, `EscrowTransaction`, `UTXOManager`, `TransactionManager`, `AccountMessenger` (P2P), `MigrationManager` (schema migrations)
- Key files: `GeniusAccount.hpp`, `TransactionManager.hpp`, `AccountMessenger.hpp`, `MigrationManager.hpp`

**src/blockchain/:**
- Purpose: Blockchain bootstrap, consensus protocol, block storage
- Contains: `Blockchain` (genesis/account creation controller), `ConsensusManager` (weighted voting), `ValidatorRegistry`, `BlockTree`, `BlockStorage`, `BlockHeaderRepository`
- Key files: `Blockchain.hpp`, `Consensus.hpp` (695 lines), `impl/Blockchain.cpp`

**src/crdt/:**
- Purpose: CRDT-based distributed state replication over IPFS
- Contains: `CrdtDatastore` (core CRDT with DAG workers), `globaldb/GlobalDB` (high-level API), `CrdtSet` (add-wins OR-Set), `CrdtHeads`, `PubSubBroadcaster`, `GraphsyncDAGSyncer`, `CRDTCallbackManager`, `HierarchicalKey`
- Key files: `crdt_datastore.hpp` (502 lines), `globaldb/globaldb.cpp`, `impl/crdt_datastore.cpp`

**src/processing/:**
- Purpose: Distributed ML job processing pipeline
- Contains: `ProcessingService` (grid coordinator), `ProcessingNode`, `ProcessingEngine`, `ProcessingCore` (interface), `ProcessTaskSplitter`, `ProcessingSubTaskQueueManager`, `SubTaskQueueAccessor`, `ProcessingValidationCore`
- Key files: `processing_service.hpp`, `processing_engine.hpp`, `processing_core.hpp`, `impl/processing_core_impl.cpp`

**src/storage/:**
- Purpose: Persistent storage backends and trie data structures
- Contains: `face/` (abstract interfaces: `readable`, `writeable`, `generic_storage`, `batchable`), `rocksdb/` (RocksDB backend), `in_memory/` (ephemeral), `trie/` (Merkle Patricia Trie), `changes_trie/` (block-level change tracking)
- Key files: `face/generic_storage.hpp`, `rocksdb/rocksdb.hpp`, `trie/supergenius_trie/supergenius_trie.hpp`

**src/proof/:**
- Purpose: zkSNARK proof generation/verification
- Contains: `IBasicProof`, `GeniusProver`, `GeniusAssigner`, `TransferProof`, `RecursiveTransferProof`, `ProcessingProof`, `circuits/` (TransactionVerifierCircuit, RecursiveTransactionCircuit), `NilFileHelper`
- Key files: `GeniusProver.hpp`, `TransferProof.hpp`, `IBasicProof.hpp`

**src/crypto/:**
- Purpose: All cryptographic primitives
- Contains: `hasher/` (multi-algorithm), `sha/`, `keccak/`, `twox/`, hashing, ED25519/SR25519/secp256k1 providers, VRF, BIP39
- Key files: `hasher.hpp`, subdirectories for each provider

**src/base/:**
- Purpose: Fundamental types and utilities
- Contains: `Buffer` (dynamic byte array), `Blob<N>` (fixed-size), `Logger` (spdlog wrapper), `hexutil`, `ScaledInteger`, `sgns_version`
- Key files: `buffer.hpp` (296 lines), `logger.hpp`, `blob.hpp`

**src/singleton/:**
- Purpose: Dependency injection container
- Contains: `IComponent` (base interface), `CComponentFactory` (singleton registry), `Singleton.hpp` (CRTP Meyers singleton)
- Key files: `IComponent.hpp`, `CComponentFactory.hpp`, `Singleton.hpp`

**src/primitives/:**
- Purpose: Core blockchain data types shared across all subsystems
- Contains: `Block`, `BlockHeader`, `BlockData`, `Extrinsic`, `Transaction`, `Authority`, `Version`, `ProductionConfiguration`, `InherentData`
- Key files: `block.hpp`, `block_header.hpp`, `transaction.hpp`

**src/account/GeniusNode (node entry point):**
- Purpose: Application lifecycle management — absorbed all functionality from the deleted `node/` and `app/integration/` directories
- Contains: `GeniusNode.hpp` (836 lines) / `GeniusNode.cpp` (1953 lines) — God-class facade owning all subsystems, inline DI wiring, node state machine FSM
- Key files: `src/account/GeniusNode.hpp`, `src/account/GeniusNode.cpp`

**example/:**
- Purpose: Runnable example applications with individual `main()` functions
- Contains: 10 examples — `node_test/` (primary), `processing_room/`, `crdt_globaldb/`, `evm_messaging_dapp/`, `ipfs_client/`, `ipfs_pubsub/`, `ipfs_client2/`, `echo_client/`, `mnn_chunkprocess/`, `processing_json/`
- Key files: `example/node_test/NodeExample.cpp`

**test/src/:**
- Purpose: Unit/integration test suites organized by subsystem
- Contains: Subdirectories mirroring `src/` layout: `account/`, `blockchain/`, `crdt/`, `crypto/`, `processing/`, `storage/`, `proof/`, `watcher/`, etc., plus cross-cutting: `multiaccount/`, `processing_multi/`, `transaction_sync/`, `pubsub_counts/`, `runtime/`, `graphsync/`, `scale/`, `price_retrieval/`
- Key files: `test/src/CMakeLists.txt` (lists all subsystem test dirs)

**example/:**
- Purpose: Runnable example applications demonstrating subsystem usage
- Contains: `crdt_globaldb/`, `processing_dapp/`, `evm_messaging_dapp/`, `ipfs_client/`, `ipfs_pubsub/`, `processing_room/`, `node_test/`
- Key files: `example/CMakeLists.txt`

**Submodules:**
- `GeniusKDF/` — Standalone key derivation library (`KeyGenerator::KDFGenerator`)
- `ProofSystem/` — ElGamal, ECDSA, AES encryption, ETH/BTC key generators, `SGProofCircuits/` (zkLLVM circuits)
- `SGProcessingManager/` — MNN-based ML inference: `MNN_Image`, `MNN_Audio`, `MNN_ML` processors, `ImageSplitter`, FlatBuffers model configs
- `evmrelay/` — EVM RLP encoding, Discv4/Discv5 discovery, RLPx transport, `EthWatch` event watcher
- `gRPCForSuperGenius/` — OpenAPI REST specs (`SuperGenius-OpenAPI.yaml`, `SGProcessing-OpenAPI.yaml`), gRPC definitions
- `docs/` — Doxygen configuration and pre-built XML docs (1,207 XML files)

## Key File Locations

**Entry Points:**
- `example/node_test/NodeExample.cpp`: Primary integration example — creates node via `GeniusNode::New()` with hand-rolled CLI
- `src/account/GeniusNode.cpp`: Core lifecycle class — inline constructor wiring, state machine FSM, all subsystem initialization
- `src/api/transport/`: HTTP/WebSocket JSON-RPC listener

**Configuration:**
- `.clang-format`: C++ formatting rules (Microsoft-based, 120 char limit, C++17)
- `.clang-tidy`: Static analysis checks (boost, bugprone, cert, cppcoreguidelines, concurrency, modernize, performance)
- `.clangd`: LSP configuration
- `cmake/config.cmake.in`: CMake config template for `find_package(SGNs)`
- `cmake/functions.cmake`: Shared CMake helper functions
- `Readme.md`: Platform-specific build instructions

**Core Logic:**
- `src/account/`: UTXO ledger, all transaction types, migration steps
- `src/blockchain/`: Genesis bootstrap, consensus, block tree
- `src/crdt/`: Distributed state replication, CRDT operations
- `src/processing/`: ML job grid, subtask queues
- `src/proof/`: zkSNARK prover/verifier
- `src/storage/`: RocksDB/in-memory backends, trie structures

**Testing:**
- `test/src/`: All test suites, mirrored `src/` structure
- `test/mock/src/`: Mock objects for unit tests
- `test/testutil/`: `WaitCondition` templates (condition_variable polling, NEVER `sleep_for`), `literals.hpp`, `outcome.hpp`, `primitives/`, `storage/` helpers

**Protobuf Schemas (co-located with subsystems):**
- `src/crdt/proto/delta.proto`: CRDT Delta and Element messages
- `src/crdt/proto/bcast.proto`, `src/crdt/globaldb/proto/broadcast.proto`: CRDT broadcast messages
- `src/blockchain/impl/proto/SGBlockchain.proto`: Genesis and AccountCreation blocks
- `src/blockchain/impl/proto/SGBlocks.proto`: Block storage structures
- `src/blockchain/impl/proto/Consensus.proto`: Consensus proposals, votes, certificates
- `src/blockchain/impl/proto/ValidatorRegistry.proto`: Validator weights and registry messages
- `src/account/proto/SGTransaction.proto`: Transaction types (DAGStruct, TransferTx, MintTx, EscrowTx, UTXO)
- `src/account/proto/SGAccountComm.proto`: Account P2P messaging protocol
- `src/processing/proto/SGProcessing.proto`: Task, SubTask, ProcessingQueue, SubTaskResult, Grid messages
- `src/proof/proto/SGProof.proto`: Proof serialization

## Naming Conventions

**Files:**
- PascalCase for classes: `GeniusAccount.hpp`, `Blockchain.hpp`, `CrdtDatastore.hpp`
- snake_case for non-class modules: `crdt_set.hpp`, `processing_engine.hpp`, `buffer.hpp`
- `CMakeLists.txt` in every buildable directory
- `proto/` subdirectory for `.proto` files within each subsystem
- `impl/` subdirectory for concrete implementations
- `face/` subdirectory for abstract interfaces (storage layer only)

**Directories:**
- Lowercase snake_case: `local_secure_storage/`, `in_memory/`, `globaldb/`
- Subsystem root directories named after domain concept (singular noun): `account/`, `blockchain/`, `crdt/`, `proof/`, `storage/`, `processing/`

**Classes:**
- PascalCase: `GeniusAccount`, `CrdtDatastore`, `ConsensusManager`, `TransactionManager`
- Interface classes prefixed with `I`: `IComponent`, `IBasicProof`, `ISecureStorage`, `IGeniusTransactions`
- Concrete implementations suffixed with `Impl`: `BlockTreeImpl`, `ProcessingCoreImpl`, `HasherImpl`
- Management/singleton classes prefixed with `C`: `CComponentFactory`

**Functions:**
- PascalCase (pseudo-Java style): `GetGenesisCID()`, `SaveGenesisCID()`, `OnGenesisBlockReceived()`
- Static factory: `New(...)` returning `std::shared_ptr<T>`
- CRDT-style: `PutKey()`, `GetKey()`, `HasKey()`, `DeleteKey()`

**Variables:**
- camelCase with prefixes: `m_` for class members (some files), `a` prefix for parameters in CRDT code: `aKey`, `aDatastore`, `aDelta`
- snake_case with trailing underscore: `dataStore_`, `options_`, `broadcaster_`, `logger_` (most common pattern in newer code)
- ALL_CAPS for constants: `ELGAMAL_PUBKEY_PREDEFINED`, `TIMEOUT_GENESIS_BLOCK_MS`

**Namespaces:**
- Top-level: `sgns`
- Sub-namespaces match directory: `sgns::crdt`, `sgns::processing`, `sgns::blockchain`, `sgns::crypto`, `sgns::storage`, `sgns::scale`, `sgns::base`, `sgns::subscription`
- Full indentation on nested namespaces per `.clang-format` (`NamespaceIndentation: All`)
- Protobuf packages: `sgns.crdt.pb`, `sgns.blockchain`, `SGTransaction`, `SGProcessing`, `SGProof`

## Where to Add New Code

**New Feature (domain logic):**
- Primary interface: `src/<feature>/<Feature>.hpp`
- Implementation: `src/<feature>/impl/<feature>_impl.cpp`, `src/<feature>/impl/<feature>_impl.hpp`
- Tests: `test/src/<feature>/` (mirror source structure)
- Wiring: Add member and initialization to `GeniusNode::StateTransition()` in `src/account/GeniusNode.cpp`
- CMake: Add `add_subdirectory(<feature>)` to `src/CMakeLists.txt`

**New Component/Module (pluggable service):**
- Interface inheriting `IComponent`: `src/<module>/<Module>.hpp`
- Wire into `GeniusNode` constructor or `StateTransition()` method in `src/account/GeniusNode.cpp`
- Follow static `New()` factory pattern returning `std::shared_ptr<T>`

**New Transaction Type:**
- Define protobuf message in `src/account/proto/SGTransaction.proto`
- Implement transaction class in `src/account/<New>Transaction.cpp/.hpp`
- Extend `IGeniusTransactions` interface if needed
- Add test suite: `test/src/account/`

**New Storage Backend:**
- Implement storage face interfaces (`src/storage/face/`)
- Place backend in `src/storage/<new_backend>/`
- Example backends: `rocksdb/`, `in_memory/`

**Utilities:**
- Shared helpers: `src/base/` (general utilities), `test/testutil/` (test-only helpers)
- Macro utilities: `src/macro/`
- CMake helpers: `cmake/`

## Special Directories

**build/:**
- Purpose: Platform-specific CMake build output directories
- Generated: Yes (by CMake configure step)
- Committed: No (build artifacts, `compile_commands.json`, etc.)

**src/crdt/proto/, src/blockchain/impl/proto/, src/account/proto/, src/processing/proto/, src/proof/proto/:**
- Purpose: Protobuf schema definitions for each subsystem
- Generated: `.pb.h` / `.pb.cc` generated by protoc at build time
- Committed: Only `.proto` files are committed; generated C++ is built output

**CRDT.Datastore.TEST/ and CRDT.Datastore.TEST.unit/:**
- Purpose: CRDT datastore test data (RocksDB database files)
- Generated: Yes (by running CRDT tests)
- Committed: Yes (contains test fixture data)

**AgentDocs/:**
- Purpose: AI agent documentation and project rules
- Generated: No (manually maintained)
- Committed: Yes
- Key files: `Architecture.md` (full subsystem catalog), `AGENT_MISTAKES.md`, `CHECKPOINT.md`, `CLAUDE.md` (coding rules)

**docs/doxygen/xml/:**
- Purpose: Pre-built Doxygen XML documentation (1,207 files, ~454 classes/structs)
- Generated: Yes (by Doxygen)
- Committed: Yes (submodule `docs`)

---

*Structure analysis: 2026-05-27*
