# Codebase Structure

**Analysis Date:** 2026-05-25

## Directory Layout

```
SuperGenius/
├── AgentDocs/                     # Agent instructions, architecture docs, checkpoints
│   ├── Architecture.md            # Comprehensive architecture reference (1045 lines)
│   ├── CHECKPOINT.md              # Project checkpoint/status
│   ├── AGENT_MISTAKES.md          # Recorded agent mistakes and lessons
│   ├── BRIDGE_MINT_PLAN.md        # Bridge minting implementation plan
│   ├── EVMRELAY_SECURITY_HARDENING_PLAN.md
│   └── CLAUDE.md                  # CLAUDE agent instructions
├── src/                           # Core C++ library source (namespace sgns::*)
│   ├── account/                   # UTXO ledger, transactions, GeniusAccount/Node
│   ├── api/transport/             # HTTP/WebSocket JSON-RPC server
│   ├── base/                      # Buffer, Blob, Logger, hex/padding utilities
│   ├── blockchain/                # Consensus, BlockTree, BlockStorage, ValidatorRegistry
│   ├── coinprices/                # CoinGecko token price retrieval
│   ├── crdt/                      # CRDT datastore, GlobalDB, PubSub broadcaster
│   ├── crypto/                    # ED25519, SR25519, Secp256k1, VRF, Hasher, BIP39, PBKDF2
│   ├── local_secure_storage/      # Platform-specific encrypted key storage
│   ├── macro/                     # Utility macros (unreachable)
│   ├── outcome/                   # outcome::result<T> adapter header
│   ├── primitives/                # Block, Extrinsic, Authority, Transaction types
│   ├── processing/                # Distributed task/subtask queue and execution engine
│   ├── proof/                     # zkSNARK provers, assigners, transfer/processing proofs
│   ├── scale/                     # SCALE binary codec (encoder/decoder streams)
│   ├── singleton/                 # IComponent / CComponentFactory DI container
│   ├── storage/                   # RocksDB, in-memory, Trie/MPT, ChangesTrie
│   ├── subscription/              # Templated pub/sub event bus
│   └── watcher/                   # EVM chain event watcher
├── test/                          # Unit and integration tests
│   ├── src/                       # Test source files mirroring src/ structure
│   ├── mock/src/                  # Mock implementations for testing
│   ├── testutil/                  # Test utilities: wait_condition, literals, color_support
│   └── CMakeLists.txt             # Test build configuration
├── build/                         # Per-platform CMake build directories
│   ├── OSX/                       # macOS builds (Debug, Release, RelWithDebInfo)
│   ├── Linux/                     # Linux builds
│   ├── Windows/                   # Windows builds (VS 2022)
│   ├── Android/                   # Android cross-compile (armeabi-v7a, arm64-v8a, x86_64)
│   ├── iOS/                       # iOS builds
│   ├── CommonBuildParameters.cmake
│   ├── CommonCompilerOptions.cmake
│   └── CompilationFlags.cmake
├── cmake/                         # CMake helper modules
│   ├── config.cmake.in
│   ├── functions.cmake
│   ├── install.cmake
│   └── version.cmake
├── gRPCForSuperGenius/            # [Submodule] OpenAPI REST + gRPC interface definitions
├── GeniusKDF/                     # [Submodule] Key derivation function library
├── ProofSystem/                   # [Submodule] Standalone proof system + circuits
├── SGProcessingManager/           # [Submodule] ML inference engine (MNN-based processors)
├── evmrelay/                      # [Submodule] EVM relay bridge
├── docs/                          # [Submodule] SG documentation
├── CRDT.Datastore.TEST/           # CRDT datastore integration tests
├── example/                       # Example code/configuration
├── zkPOC/                         # Zero-knowledge proof-of-concept code
├── .planning/                     # GSD planning documents (this file's destination)
├── .github/                       # GitHub CI/CD workflows
├── .gitmodules                    # Git submodule definitions
├── .clang-format                  # Clang format configuration (Microsoft-based, 4-space indent, 120 cols)
├── .clang-tidy                    # Clang-tidy configuration
├── .clangd                        # Clangd LSP configuration
├── .gitignore                     # Git ignore rules
├── .git-blame-ignore-revs         # Blame-ignore revision list
├── LICENSE                        # Project license
├── Readme.md                      # Build instructions and project overview
└── new-issues.sh                  # Script to create new issues
```

## Directory Purposes

**`src/account/`:**
- Purpose: Block-lattice token ledger — every account has its own DAG block chain
- Contains: GeniusAccount (account CRDT chain), GeniusNode (network node with validator/processor roles), GeniusUTXO (UTXO wrapper), TransactionManager (lifecycle orchestration), UTXOManager (UTXO set tracking), AccountMessenger (P2P pubsub messaging), MigrationManager (schema migrations), Transaction types (Transfer, Mint, Processing, Escrow), InputValidators, Genesis/AccountCreation blocks
- Key files: `src/account/GeniusNode.hpp` (839 lines, top-level facade), `src/account/GeniusAccount.hpp`, `src/account/TransactionManager.hpp`, `src/account/UTXOManager.hpp`, `src/account/proto/SGTransaction.proto`, `src/account/proto/SGAccountComm.proto`

**`src/blockchain/`:**
- Purpose: Substrate-inspired consensus, block storage, block tree, finality, validator management
- Contains: Blockchain (genesis/account creation coordinator), Consensus, ConsensusAuth, BlockTree (fork-aware), BlockStorage/BlockHeaderRepository (interfaces), ValidatorRegistry
- Key files: `src/blockchain/Blockchain.hpp` (501 lines), `src/blockchain/Consensus.hpp`, `src/blockchain/impl/block_tree_impl.hpp`, `src/blockchain/impl/proto/SGBlockchain.proto`

**`src/processing/`:**
- Purpose: Distributed AI/ML compute network — task splitting, subtask queuing, execution, result validation
- Contains: ProcessingCore (interface, implemented in `impl/`), ProcessingEngine, ProcessingNode, ProcessingService, ProcessTaskSplitter, ProcessingSubTaskQueue, SubTaskQueueAccessor, SubTaskEnqueuer, SubTaskResultStorage, ProcessingValidationCore
- Key files: `src/processing/processing_core.hpp`, `src/processing/processing_service.hpp`, `src/processing/processing_engine.hpp`, `src/processing/proto/SGProcessing.proto`, `src/processing/impl/processing_core_impl.hpp`

**`src/crdt/`:**
- Purpose: Distributed replicated key-value store with causal consistency over IPFS
- Contains: CrdtDatastore (core CRDT), GlobalDB (high-level facade), CrdtSet (add-wins OR-Set), CrdtHeads (DAG tips), Broadcaster/DAGSyncer (abstract transport), GraphsyncDAGSyncer (IPFS block sync), PubSubBroadcaster, HierarchicalKey, AtomicTransaction, CRDTCallbackManager, CRDTDataFilter
- Key files: `src/crdt/crdt_datastore.hpp` (502 lines), `src/crdt/globaldb/globaldb.hpp`, `src/crdt/impl/crdt_datastore.cpp`, `src/crdt/proto/delta.proto`, `src/crdt/globaldb/proto/broadcast.proto`

**`src/proof/`:**
- Purpose: zkSNARK proof generation and verification using nil::crypto3
- Contains: GeniusProver, GeniusAssigner, TransferProof, RecursiveTransferProof, ProcessingProof, IBasicProof
- Key files: `src/proof/GeniusProver.hpp`, `src/proof/IBasicProof.hpp`, `src/proof/circuits/`, `src/proof/proto/SGProof.proto`

**`src/storage/`:**
- Purpose: Local persistent and in-memory key-value storage abstractions
- Contains: `face/` — abstract interfaces (Readable, Writeable, GenericMap, Batchable, MapCursor); `rocksdb/` — RocksDB adapter; `in_memory/` — std::map-backed storage; `trie/` — Merkle Patricia Trie; `changes_trie/` — block-level change tracking
- Key files: `src/storage/face/generic_storage.hpp`, `src/storage/rocksdb/rocksdb.hpp`, `src/storage/trie/supergenius_trie/`

**`src/crypto/`:**
- Purpose: Cryptographic primitives
- Contains: `ed25519/` — ED25519 sign/verify/generate (libsodium); `secp256k1/` — secp256k1 ECDSA; `hasher/` — blake2b, keccak, sha2, twox hashing; `sha/` — SHA-256; `keccak/` — Keccak hash; `pbkdf2/` — PBKDF2 key derivation; `twox/` — XXHash-based hasher
- Key files: `src/crypto/ed25519_provider.hpp`, `src/crypto/secp256k1_provider.hpp`, `src/crypto/hasher.hpp`

**`src/api/transport/`:**
- Purpose: JSON-RPC over HTTP and WebSocket
- Contains: `impl/ws/` — WebSocket listener/session/client (Boost.Asio); HTTP listener/session
- Key files: `src/api/transport/impl/ws/`

**`src/base/`:**
- Purpose: Core utility types shared across all subsystems
- Contains: Buffer (dynamic byte array), Blob<N> (fixed-size array, Hash256/Hash512 specializations), Logger (spdlog wrapper), hexutil (hex encoding/decoding), ScaledInteger, endian helpers, sgns_version, visitor pattern helpers
- Key files: `src/base/buffer.hpp`, `src/base/blob.hpp`, `src/base/logger.hpp`

**`src/primitives/`:**
- Purpose: Core blockchain data type definitions
- Contains: Block, BlockHeader, Extrinsic, Transaction, Authority, AuthorityList, Justification, Version, InherentData, Digest, SessionKey, ScheduledChange, ValidTransaction
- Key files: `src/primitives/block.hpp`, `src/primitives/block_header.hpp`, `src/primitives/transaction.hpp`

**`src/scale/`:**
- Purpose: SCALE binary codec (serialization for on-chain data)
- Contains: ScaleEncoderStream, ScaleDecoderStream, compact integer encoding, tuple/variant helpers
- Key files: `src/scale/scale_encoder_stream.hpp`, `src/scale/scale_decoder_stream.hpp`, `src/scale/types.hpp`

**`src/singleton/`:**
- Purpose: Dependency injection container
- Contains: IComponent (base interface), IComponentFactory (registry interface), CComponentFactory (singleton concrete registry), CSingleton<T> (Meyers singleton template)
- Key files: `src/singleton/IComponent.hpp`, `src/singleton/CComponentFactory.hpp`, `src/singleton/Singleton.hpp`

**`src/subscription/`:**
- Purpose: Internal publish/subscribe event bus
- Contains: SubscriptionEngine<EventT> (templated engine), Subscriber<EventT> (subscriber handle)
- Key files: `src/subscription/subscription_engine.hpp`, `src/subscription/subscriber.hpp`

**`src/watcher/`:**
- Purpose: Bridge message handling orchestration
- Contains: MessagingWatcher (abstract base, background thread), EvmMessagingWatcher (EVM bridge orchestrator — consumes evmrelay library)
- Key files: `src/watcher/messaging_watcher.hpp`, `src/watcher/impl/evm_messaging_watcher.hpp`
- Note: Current `EvmMessagingWatcher` contains placeholder code using raw WebSocket `eth_subscribe`; being migrated to use evmrelay as the Ethereum protocol library

**`src/local_secure_storage/`:**
- Purpose: Platform-native secure key storage
- Contains: ISecureStorage (interface), AppleSecureStorage (Keychain), AndroidSecureStorage (Keystore), LinuxSecureStorage (libsecret), WindowsSecureStorage (DPAPI), JSONSecureStorage (fallback)
- Key files: `src/local_secure_storage/ISecureStorage.hpp`, `src/local_secure_storage/SecureStorage.hpp`

**`src/coinprices/`:**
- Purpose: External token price data retrieval
- Contains: CoinGeckoPriceRetriever (REST API client)
- Key files: `src/coinprices/coinprices.hpp`

**`src/outcome/`:**
- Purpose: outcome::result<T> error handling adapter
- Contains: Single outcome.hpp header
- Key files: `src/outcome/outcome.hpp`

**`src/macro/`:**
- Purpose: Utility macros
- Contains: unreachable.hpp (unreachable code marker)
- Key files: `src/macro/unreachable.hpp`

**`test/src/`:**
- Purpose: Unit and integration tests mirroring `src/` structure
- Contains: test subdirectories for `account/`, `base/`, `blockchain/`, `crdt/`, `crypto/`, `graphsync/`, `local_secure_storage/`, `multiaccount/`, `price_retrieval/`, `primitives/`, `processing/`, `processing_datatypes/`, `processing_multi/`, `processing_nodes/`, `processing_schema/`, `proof/`, `pubsub_counts/`, `runtime/`, `scale/`, `storage/`, `transaction_sync/`, `watcher/`, `account_creation/`
- Key files: `test/src/crdt/crdt_datastore_test.cpp`, `test/src/crdt/globaldb_integration.cpp`, `test/src/blockchain/blockchain_genesis_test.cpp`, `test/src/processing/processing_engine_test.cpp`

**`test/mock/src/`:**
- Purpose: Mock implementations for testing
- Contains: Mock classes replacing real subsystems (e.g., mock CRDT broadcasters, mock DAG syncers)
- Key files: `test/src/crdt/crdt_custom_broadcaster.hpp`, `test/src/processing/processing_mock.hpp`

**`test/testutil/`:**
- Purpose: Shared test utilities
- Contains: `wait_condition.hpp` (condition_variable-based wait templates — NEVER use sleep_for), `literals.hpp` (hash literals), `outcome.hpp` (test outcome helpers), `sr25519_utils.hpp`, `color_support.hpp`, `mint_source_hash.hpp`, `primitives/`, `storage/`
- Key files: `test/testutil/wait_condition.hpp`

**`AgentDocs/`:**
- Purpose: Agent instruction files and architectural documentation
- Contains: Architecture.md (comprehensive reference), CHECKPOINT.md, AGENT_MISTAKES.md, CLAUDE.md, BRIDGE_MINT_PLAN.md
- Key files: `AgentDocs/Architecture.md` (1045 lines, authoritative architecture reference)

**`build/`:**
- Purpose: Platform-specific CMake build directories
- Contains: `OSX/`, `Linux/`, `Windows/`, `Android/`, `iOS/` — each with `Debug`/`Release`/`RelWithDebInfo` variants; shared CMake parameter/compiler flag files
- Key files: `build/CommonBuildParameters.cmake`, `build/CommonCompilerOptions.cmake`

**`gRPCForSuperGenius/`:**
- Purpose: Git submodule — OpenAPI REST + gRPC interface definitions
- Contains: `SuperGenius-OpenAPI.yaml` (token/account API), `SGProcessing-OpenAPI.yaml` (processing grid API); `SuperGenius_OpenAPIImpl` server stubs

**`GeniusKDF/`:**
- Purpose: Git submodule — standalone key derivation function library
- Contains: KDFGenerator class (`KeyGenerator` namespace); PBKDF2/Scrypt-style key derivation

**`ProofSystem/`:**
- Purpose: Git submodule — standalone proof system
- Contains: AESEncryption, ECDHEncryption, ECDSAPublicKey, ECElGamalKeyGenerator, Bitcoin/EthereumKeyGenerator; `SGProofCircuits/` (zkSNARK circuit definitions: MPCVerifier, TxVerifier)

**`SGProcessingManager/`:**
- Purpose: Git submodule — ML inference engine
- Contains: ProcessingManager (base), MNN_Image/Audio/ML processors, ImageSplitter, InputTypes; `generated/` (FlatBuffers-generated model config structs)

**`evmrelay/`:**
- Purpose: Git submodule — Ethereum protocol library providing watcher service (P2P discovery, event watching), public RPC list provider, and RPC connection maker
- Consumed by: `src/watcher/` (bridge orchestrator uses evmrelay as a library for Ethereum protocol interaction), `src/account/` (mint code uses evmrelay RPC endpoints for transaction verification)

**`CRDT.Datastore.TEST/`:**
- Purpose: Standalone CRDT datastore integration test suite
- Contains: Integration test binaries

**`zkPOC/`:**
- Purpose: Zero-knowledge proof-of-concept exploration code

**`example/`:**
- Purpose: Example code and configuration demonstrating SuperGenius usage

**`.planning/`:**
- Purpose: GSD planning documents (phase plans, codebase maps, sprint plans)
- Contains: `codebase/` directory for codebase analysis documents (this file's destination)

**`cmake/`:**
- Purpose: CMake helper modules (config template, utility functions, install rules, version)
- Key files: `cmake/functions.cmake`, `cmake/config.cmake.in`

## Key File Locations

**Entry Points:**
- `node/`: Top-level node class and CLI entry point; wires all subsystems
- `src/account/GeniusNode.hpp`: `GeniusNode::New()` factory — primary application bootstrap
- `src/api/transport/impl/ws/`: JSON-RPC WebSocket server entry point
- `src/watcher/impl/evm_messaging_watcher.hpp`: EVM watcher background entry point

**Configuration:**
- `build/CommonBuildParameters.cmake`: Shared CMake build parameters
- `build/CommonCompilerOptions.cmake`: Shared compiler flags
- `build/CompilationFlags.cmake`: Platform-specific compilation flags
- `build/apple.toolchain.cmake`: Apple cross-compilation toolchain
- `src/base/sgnsv.h.in`: Version header template (auto-generated)
- `cmake/config.cmake.in`: Config header template
- `src/account/GeniusNode.hpp`: `DevConfig` runtime configuration struct

**Core Logic:**
- `src/account/GeniusAccount.hpp`: Account CRDT chain management
- `src/account/TransactionManager.hpp`: Transaction lifecycle orchestration
- `src/blockchain/Blockchain.hpp`: Genesis/account creation + consensus integration
- `src/crdt/crdt_datastore.hpp`: CRDT core implementation (502 lines)
- `src/crdt/globaldb/globaldb.hpp`: High-level distributed database facade
- `src/processing/processing_core.hpp`: Processing algorithm interface
- `src/processing/processing_service.hpp`: Grid coordination service
- `src/proof/GeniusProver.hpp`: zkSNARK proof generation orchestration

**Testing:**
- `test/src/`: Mirror of `src/` structure with test files
- `test/mock/src/`: Mock implementations
- `test/testutil/wait_condition.hpp`: Condition-variable wait templates (mandatory for async testing)
- `test/testutil/outcome.hpp`: Test helpers for outcome::result assertions
- `test/CMakeLists.txt`: Test build configuration; test binary targets

**Documentation:**
- `Readme.md`: Build instructions (173 lines)
- `AgentDocs/Architecture.md`: Comprehensive architecture reference (1045 lines)
- `AgentDocs/CLAUDE.md`: Agent-specific development instructions
- `AgentDocs/CHECKPOINT.md`: Project status checkpoint
- `AgentDocs/AGENT_MISTAKES.md`: Recorded agent mistakes

## Naming Conventions

**Files:**
- PascalCase for class headers: `GeniusNode.hpp`, `TransactionManager.hpp`, `Blockchain.hpp`
- snake_case for non-class headers: `block_tree.hpp`, `buffer_map_types.hpp`, `scale_error.hpp`, `common.hpp`
- `.cpp` extension for implementation: `GeniusNode.cpp`, `blockchain.cpp`, `ed25519_provider_impl.cpp`
- `.hpp` extension for headers: `GeniusNode.hpp`, `buffer.hpp`, `common.hpp`
- Proto files: `SGTransaction.proto`, `SGAccountComm.proto`, `SGBlockchain.proto`, `SGProcessing.proto`, `SGProof.proto`, `delta.proto`, `bcast.proto`, `broadcast.proto`, `heads.proto`
- Test files: `<name>_test.cpp` suffix (e.g., `crdt_datastore_test.cpp`, `blockchain_genesis_test.cpp`, `processing_engine_test.cpp`)

**Directories:**
- snake_case for all directories: `local_secure_storage/`, `block_header_repository/`
- `impl/` subdirectory: Contains concrete implementations of interfaces defined in parent directory
- `proto/` subdirectory: Contains `.proto` files for the subsystem
- `test/src/` mirrors `src/` directory structure exactly
- PascalCase for submodules: `gRPCForSuperGenius/`, `GeniusKDF/`, `ProofSystem/`, `SGProcessingManager/`

**Classes:**
- PascalCase: `GeniusNode`, `CComponentFactory`, `TransferTransaction`, `ProcessingEngine`
- Interface prefix `I`: `IComponent`, `IComponentFactory`, `IGeniusTransactions`, `IBasicProof`, `IMigrationStep`, `ISecureStorage`
- Implementation suffix `Impl`: `BlockTreeImpl`, `ED25519ProviderImpl`, `ProcessingCoreImpl`, `SubTaskQueueAccessorImpl`
- Factory suffix `Factory`: `CComponentFactory`, `IComponentFactory`

**Namespaces:**
- Primary: `sgns`
- Sub-namespaces (nested, full indentation): `sgns::crdt`, `sgns::processing`, `sgns::crypto`, `sgns::storage`, `sgns::scale`, `sgns::primitives`, `sgns::blockchain`, `sgns::subscription`, `sgns::watcher`, `sgns::api`
- Third-party namespaces: `ipfs_lite::ipld`, `ipfs_lite::ipfs::graphsync`, `ipfs_pubsub`, `nil::crypto3`

**Variables:**
- camelCase: `myVariable`, `devConfig`, `basePort`, `isProcessor`
- Constants: `kCamelCase` prefix for constexpr/inline constexpr: `kPublicKeySize`, `kSignatureSize`
- Macro constants: ALL_CAPS: `OUTGOING_TIMEOUT_MILLISECONDS`, `INCOMING_TIMEOUT_MILLISECONDS`, `SINGLETONINSTANCE`

**Functions/Methods:**
- PascalCase: `GetName()`, `GetBalance()`, `Submit()`, `ProcessSubTask()`, `SplitTask()`
- Factory methods: `New()` or `Create()` returning `shared_ptr`
- Error enum classes defined inside their owning class: `Blockchain::Error`, `CrdtDatastore::Error`

## Where to Add New Code

**New Feature (e.g., new transaction type):**
- Primary code: `src/account/NewTransaction.hpp`, `src/account/NewTransaction.cpp`
- Proto schema (if needed): `src/account/proto/NewTransaction.proto`
- Tests: `test/src/account/new_transaction_test.cpp`

**New Component/Module (e.g., new subsystem):**
- Interface: `src/newmodule/INewService.hpp` (define abstract interface extending `IComponent`)
- Implementation: `src/newmodule/impl/new_service_impl.hpp`, `src/newmodule/impl/new_service_impl.cpp`
- Proto: `src/newmodule/proto/newmodule_messages.proto`
- CMake: `src/newmodule/CMakeLists.txt` + add `add_subdirectory(newmodule)` to `src/CMakeLists.txt`
- Factory wiring: `app/integration/NewServiceFactory.hpp` (if using CComponentFactory DI)
- Tests: `test/src/newmodule/new_service_test.cpp`

**New Crypto Provider:**
- Interface header: `src/crypto/new_provider.hpp`
- Implementation: `src/crypto/newcrypto/new_provider_impl.hpp`, `src/crypto/newcrypto/new_provider_impl.cpp`
- CMake: `src/crypto/newcrypto/CMakeLists.txt`
- Tests: `test/src/crypto/new_provider_test.cpp`

**Utilities:**
- Shared helpers: `src/base/new_utility.hpp`, `src/base/new_utility.cpp`
- Test utilities: `test/testutil/new_test_helper.hpp`

**New Integration/Mock:**
- Mock: `test/mock/src/mock_new_service.hpp`

## Special Directories

**`src/*/impl/`:**
- Purpose: Concrete implementations of interfaces declared in the parent directory
- Found in: `blockchain/impl/`, `crdt/impl/`, `processing/impl/`, `watcher/impl/`, `api/transport/impl/`, `local_secure_storage/impl/`, `storage/trie/impl/`
- Generated: No
- Committed: Yes

**`src/*/proto/`:**
- Purpose: Protocol Buffer message schemas for the subsystem
- Found in: `account/proto/`, `blockchain/impl/proto/`, `crdt/proto/`, `crdt/globaldb/proto/`, `processing/proto/`, `proof/proto/`
- Generated: `.proto` files are hand-written; generated `.pb.h` / `.pb.cc` files produced during build
- Committed: Only `.proto` source files are committed; generated files are build artifacts

**`build/<Platform>/`:**
- Purpose: Per-platform CMake build output directories
- Generated: Yes (all build outputs, binaries, generated protobuf code)
- Committed: No (gitignored, except for shared `.cmake` config files at `build/` root)

**`.planning/`:**
- Purpose: GSD planning documents, codebase maps, phase plans
- Generated: Yes (by GSD commands like `/gsd-map-codebase`, `/gsd-plan-phase`)
- Committed: Yes (tracked for team visibility and AI context)

**`AgentDocs/`:**
- Purpose: Agent instruction files, architecture reference, project status
- Generated: No (hand-maintained)
- Committed: Yes

**`CRDT.Datastore.TEST/`:**
- Purpose: Standalone CRDT integration test binaries
- Generated: No (hand-written test code)
- Committed: Yes

---

*Structure analysis: 2026-05-25*
