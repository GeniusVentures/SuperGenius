<!-- refreshed: 2026-05-25 -->
# Architecture

**Analysis Date:** 2026-05-25

## System Overview

```text
┌──────────────────────────────────────────────────────────────────────────────────────┐
│                              GeniusNode (Top-Level Facade)                            │
│                           `src/account/GeniusNode.hpp`                                │
│              Orchestrates account, networking, transaction, blockchain,               │
│                              and processing subsystems                                │
├─────────────────┬────────────────────┬─────────────────────┬─────────────────────────┤
│ Account/Tx      │ Blockchain          │ Processing Grid      │ Proof System            │
│ `src/account/`  │ `src/blockchain/`   │ `src/processing/`    │ `src/proof/`            │
│                 │                    │                      │ `ProofSystem/`          │
│ UTXO ledger,    │ Consensus,         │ Distributed ML job   │ zkSNARK (Groth16,       │
│ Mint, Escrow,   │ Finality,          │ splitting & exec,    │ PlonK, HyperPlonK),     │
│ Transfer,       │ BlockTree,         │ subtask queues,      │ recursive/folding,      │
│ DAG blocks      │ ValidatorRegistry  │ result validation    │ transfer/processing     │
└────────┬────────┴─────────┬──────────┴───────────┬──────────┴──────────┬──────────────┘
         │                  │                      │                     │
         ▼                  ▼                      ▼                     ▼
┌──────────────────────────────────────────────────────────────────────────────────────┐
│                                CRDT / GlobalDB (Persistence)                          │
│                   `src/crdt/` — CrdtDatastore, GlobalDB, DAGSyncer                    │
│                Replicated over IPFS pubsub/Graphsync for causal consistency           │
├──────────────────────────────────────────────────────────────────────────────────────┤
│  Storage (`src/storage/`)        │  Crypto (`src/crypto/`)        │  SCALE (`src/scale/`)│
│  RocksDB, In-Memory, Trie/MPT   │  ED25519, SR25519, Secp256k1,  │  Binary codec        │
│                                  │  VRF, Hasher, BIP39, PBKDF2    │                      │
└──────────────────────────────────┴────────────────────────────────┴────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────────────────────┐
  │  External Interfaces                                                                  │
  │  `src/api/transport/` — JSON-RPC WS/HTTP  |  `src/watcher/` — EVM bridge orchestrator │
  │  `gRPCForSuperGenius/` — OpenAPI REST      |  `src/coinprices/` — CoinGecko           │
  └──────────────────────────────────────────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────────────────────┐
│  EVM Relay Submodule (`evmrelay/`)                                                     │
│  Ethereum P2P watcher service + public RPC list provider + RPC connection maker        │
│  Peer discovery (discv4/discv5), RLPx transport, ETH subprotocol event watching,       │
│  RPC endpoint pool management, receipt fetching, bridge event types                    │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

SuperGenius is a **block-lattice crypto-token system** (inspired by Nano) extended with:
- A **UTXO-based token ledger** with escrow, minting, and transfer semantics.
- A **distributed AI/ML job-processing network** where nodes bid on and execute subtasks in exchange for tokens.
- A **CRDT-replicated distributed database** (GlobalDB) over IPFS pubsub/Graphsync for consensus without a global chain.
- A **zkSNARK proof system** (via `nil::crypto3` / zkLLVM) for verifiable computation and private transfer proofs.
- A **Substrate-inspired WebAssembly runtime** (Binaryen) for on-chain logic.
- An **EVM bridge watcher** that listens for cross-chain minting events.

**Primary namespace:** `sgns` (with sub-namespaces: `sgns::crdt`, `sgns::processing`, `sgns::crypto`, `sgns::storage`, `sgns::scale`, `sgns::primitives`, `sgns::blockchain`, `sgns::subscription`, `sgns::watcher`, `sgns::api`)

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| `GeniusNode` | Top-level node facade; initializes and coordinates all subsystems | `src/account/GeniusNode.hpp` |
| `GeniusAccount` | User account with DAG block-lattice chain; holds credentials and CRDT-backed ledger | `src/account/GeniusAccount.hpp` |
| `TransactionManager` | Orchestrates transaction lifecycle: build → sign → broadcast → track | `src/account/TransactionManager.hpp` |
| `UTXOManager` | Manages UTXO set for an account in GlobalDB | `src/account/UTXOManager.hpp` |
| `AccountMessenger` | P2P messaging for accounts over libp2p pubsub (nonce, block, UTXO requests) | `src/account/AccountMessenger.hpp` |
| `Blockchain` | Genesis/account-creation blocks; consensus subject/proposal tracking | `src/blockchain/Blockchain.hpp` |
| `BlockTree` | Fork-aware in-memory + persistent block tree; finality tracking | `src/blockchain/block_tree.hpp` |
| `ValidatorRegistry` | Validator set with weight configuration | `src/blockchain/ValidatorRegistry.hpp` |
| `CrdtDatastore` | Core CRDT implementation (add-wins OR-Set, delta-based); DAG-synced via IPFS | `src/crdt/crdt_datastore.hpp` |
| `GlobalDB` | High-level distributed DB wrapping CrdtDatastore; namespace-scoped key-value ops | `src/crdt/globaldb/globaldb.hpp` |
| `ProcessingServiceImpl` | Coordinates the processing grid; manages ProcessingStatus per task | `src/processing/processing_service.hpp` |
| `ProcessingEngine` | Executes subtasks on a node via ProcessingCore | `src/processing/processing_engine.hpp` |
| `ProcessingCore` | Interface for processing algorithms; implemented by SGProcessingManager (MNN) | `src/processing/processing_core.hpp` |
| `GeniusProver` | Orchestrates zkSNARK proof generation via nil::crypto3 | `src/proof/GeniusProver.hpp` |
| `GeniusAssigner` | Generates arithmetic circuit assignment table (witness) | `src/proof/GeniusAssigner.hpp` |
| `IBasicProof` | Base interface for all proof types (Generate, Verify, Serialize, Deserialize) | `src/proof/IBasicProof.hpp` |
| `CComponentFactory` | Singleton DI container; RegisterComponent / GetComponent for all IComponent instances | `src/singleton/CComponentFactory.hpp` |
| `SubscriptionEngine` | Templated publish/subscribe engine for internal events | `src/subscription/subscription_engine.hpp` |
| `EvmMessagingWatcher` | Bridge message handling orchestrator; receives verified observations from evmrelay, manages event lifecycle, triggers mint verification via RPC | `src/watcher/impl/evm_messaging_watcher.hpp` |

## Pattern Overview

**Overall:** Layered architecture with interface-implementation separation, singleton DI container, and outcome-based error handling.

**Key Characteristics:**
- **Interface/Implementation separation:** Headers declare abstract interfaces or public APIs; concrete implementations live in `impl/` subdirectories (e.g., `src/crdt/impl/`, `src/blockchain/impl/`, `src/watcher/impl/`, `src/processing/impl/`)
- **`outcome::result<T>` for all error propagation:** No exceptions in hot paths; `OUTCOME_TRY` macro chains results; error types are plain `enum class` values registered via `OUTCOME_HPP_DECLARE_ERROR`
- **`IComponent` / `CComponentFactory` DI container:** Every injectable service inherits `IComponent` (just `GetName()`); `CComponentFactory` is a `CSingleton` registry that wires components at startup
- **`CSingleton<T>` Meyers singleton:** Used for `CComponentFactory` and global logging backend; provides `T::Instance()` accessor
- **Protobuf for message schemas:** All subsystem-specific messages defined in `proto/` subdirectories (`.proto` files);
- **Factory pattern for construction:** Most classes use a static `New()` or `Create()` factory method that returns `std::shared_ptr`, often accepting callbacks for async init
- **CRDT-as-persistence-backbone:** Account blocks, UTXO sets, and processing queue state all stored in CRDT-replicated GlobalDB

## Layers

**Account / Transaction Layer:**
- Purpose: Block-lattice UTXO ledger — DAG blocks per account, token transfers, minting, escrow
- Location: `src/account/`
- Contains: GeniusAccount, GeniusNode, TransferTransaction, MintTransaction, EscrowTransaction, ProcessingTransaction, UTXOManager, TransactionManager, AccountMessenger, MigrationManager
- Depends on: CRDT/GlobalDB (persistence), Crypto (signing), Blockchain (consensus integration), evmrelay (RPC endpoints for mint verification)
- Used by: GeniusNode (orchestration), External API

**Blockchain Layer:**
- Purpose: Consensus coordination, block storage, block tree, finality, validator registry
- Location: `src/blockchain/`
- Contains: Blockchain, BlockTree, BlockStorage, BlockHeaderRepository, ValidatorRegistry, Consensus
- Depends on: CRDT/GlobalDB, Crypto, Primitives
- Used by: Account layer, Node entrypoint

**Processing Grid Layer:**
- Purpose: Distributed compute network — task splitting, subtask queuing, execution, result validation
- Location: `src/processing/`
- Contains: ProcessingCore, ProcessingEngine, ProcessingNode, ProcessingService, ProcessTaskSplitter, ProcessingSubTaskQueue, SubTaskQueueAccessor, ProcessingValidationCore
- Depends on: CRDT/GlobalDB (queue state), PubSub (channel communication), SGProcessingManager (ML execution), Proof (validation)
- Used by: GeniusNode (processing nodes)

**Proof Layer:**
- Purpose: zkSNARK proof generation and verification (transfers, processing results, recursive aggregation)
- Location: `src/proof/`, `ProofSystem/`, `ProofSystem/SGProofCircuits/`
- Contains: GeniusProver, GeniusAssigner, TransferProof, RecursiveTransferProof, ProcessingProof, IBasicProof
- Depends on: nil::crypto3 (underlying zkSNARK library), Crypto
- Used by: Transaction layer (transfer proofs), Processing layer (computation proofs)

**CRDT / Persistence Layer:**
- Purpose: Distributed replicated key-value store over IPFS for causal consistency
- Location: `src/crdt/`
- Contains: CrdtDatastore, GlobalDB, CrdtSet, CrdtHeads, Broadcaster, DAGSyncer, GraphsyncDAGSyncer, HierarchicalKey, AtomicTransaction
- Depends on: RocksDB (local storage), IPFS pubsub/Graphsync (network transport), Protobuf (delta serialization)
- Used by: All other layers as persistence backbone

**Storage Layer:**
- Purpose: Local persistent and in-memory key-value storage
- Location: `src/storage/`
- Contains: RocksDB adapter, InMemoryStorage, Trie/MPT (Merkle Patricia Trie), ChangesTrie
- Depends on: RocksDB (3rd-party), base::Buffer/Blob types
- Used by: CRDT layer, Blockchain layer, Runtime

**Crypto Layer:**
- Purpose: Cryptographic primitives — key generation, signing, hashing, VRF, key derivation
- Location: `src/crypto/`
- Contains: ED25519, SR25519, Secp256k1 providers; VRF, Hasher (blake2b, keccak, sha2, twox), BIP39, PBKDF2
- Depends on: libsodium (ED25519/SR25519), OpenSSL (secp256k1)
- Used by: All layers requiring cryptographic operations

**API / Transport Layer:**
- Purpose: JSON-RPC over HTTP and WebSocket for external access
- Location: `src/api/transport/`
- Contains: HTTP listener (Boost.Asio), WebSocket listener, WebSocket client
- Depends on: Boost.Asio
- Used by: External clients

**Watcher / Bridge Orchestrator Layer:**
- Purpose: EVM bridge message handling orchestration — receives verified observations from evmrelay, manages event lifecycle, coordinates mint verification
- Location: `src/watcher/`
- Contains: MessagingWatcher (abstract base), EvmMessagingWatcher (EVM bridge orchestrator)
- Depends on: evmrelay (Ethereum protocol services: peer discovery, event watching, RPC transport, receipt verification)
- Used by: GeniusNode (bridge lifecycle management)
- Note: Current `EvmMessagingWatcher` contains placeholder legacy code using raw WebSocket `eth_subscribe`; being migrated to use evmrelay's `EthWatchService` + `RpcManager`

**Subscription / Event Layer:**
- Purpose: Templated publish/subscribe engine for internal event propagation
- Location: `src/subscription/`
- Contains: SubscriptionEngine, Subscriber
- Used by: Cross-subsystem event propagation (storage changes, block finality, extrinsic pool events)

**Base / Primitives Layer:**
- Purpose: Core data types, buffers, hex utilities, logging, strong types
- Location: `src/base/`, `src/primitives/`
- Contains: Buffer, Blob<N>, Hash256, Hash512, Logger, Block, Extrinsic, Authority, Transaction, Version
- Used by: All layers

## Data Flow

### Primary Request Path (Token Transfer)

1. User calls `TransferTransaction::Build()` — selects UTXOs from `UTXOManager::GetUTXOs()` → CRDT GlobalDB → RocksDB (`src/account/TransferTransaction.cpp`)
2. `GeniusProver::Generate()` produces zkSNARK transfer proof (`src/proof/GeniusProver.cpp`)
3. `ED25519Provider::sign()` produces signature on transaction (`src/crypto/ed25519/ed25519_provider_impl.cpp`)
4. `TransactionManager::Submit()` coordinates broadcast + persistence (`src/account/TransactionManager.cpp`)
5. `AccountMessenger::Broadcast()` publishes signed transaction over libp2p pubsub (`src/account/AccountMessenger.cpp`)
6. `GlobalDB::Put(tx_hash, TransferTx proto)` persists to CRDT-backed store (`src/crdt/globaldb/globaldb.cpp`)
7. `UTXOManager::MarkSpent()` updates UTXO set atomically via `AtomicTransaction` (`src/account/UTXOManager.cpp`)

### Distributed Processing Flow

1. Job submitted (escrow locked via `EscrowTransaction`) → `TransactionManager::Submit()` (`src/account/TransactionManager.cpp`)
2. `ProcessingServiceImpl` listens on GridChannel pubsub for `ProcessingChannelRequest` (`src/processing/processing_service.cpp`)
3. `ProcessTaskSplitter::SplitTask()` divides job into `SubTask` list (`src/processing/processing_tasksplit.cpp`)
4. `SubTaskEnqueuerImpl::Enqueue()` publishes subtasks via `ProcessingSubTaskQueueChannelPubSub` → IPFS pubsub (`src/processing/processing_subtask_enqueuer_impl.cpp`)
5. Each `ProcessingNode` worker: `SubTaskQueueAccessorImpl::GetNextSubTask()` → `ProcessingEngine::Execute()` → `ProcessingCoreImpl::ProcessSubTask()` → `SGProcessingManager` (MNN inference) (`src/processing/impl/processing_core_impl.cpp`)
6. `SubTaskResult` published to results_channel → `SubTaskResultStorageImpl::Store()` → GlobalDB (`src/processing/impl/processing_subtask_result_storage_impl.cpp`)
7. `ProcessingValidationCore::Validate()` verifies results → `EscrowReleaseTransaction` released via `TransactionManager` (`src/processing/processing_validation_core.cpp`)

### EVM Minting Flow

Concern separation between `evmrelay/` (Ethereum protocol library) and `src/watcher/` (bridge orchestrator):

1. `evmrelay` discovers peers (discv4/discv5) and establishes RLPx sessions → ETH subprotocol receives `NewBlock`/`NewPooledTransactionHashes` → `EthWatchService` applies Bloom prefilter → requests receipts → ABI decodes matched logs → produces `WatchEventNotification` or `BridgeEventClaim` (`evmrelay/src/eth/eth_watch_service.cpp`)
2. `evmrelay` RPC layer provides `RpcManager` with multi-endpoint pool, `RpcReceiptSource` for independent receipt verification (`evmrelay/src/eth/rpc_manager.cpp`)
3. Verified observations delivered to `EvmMessagingWatcher` (orchestrator in `src/watcher/impl/`) which manages message lifecycle, dedup, and response dispatch
4. `EvmMessagingWatcher` triggers mint verification → `src/account/` code uses `evmrelay` RPC endpoints to verify stored messages before constructing `MintTransaction` (`src/account/MintTransaction.cpp`)
5. `TransactionManager::Submit()` persists to GlobalDB + broadcasts via pubsub (`src/account/TransactionManager.cpp`)

**Boundary:** `evmrelay` provides raw Ethereum protocol services (peer discovery, event watching, RPC transport, receipt fetching). `src/watcher/` orchestrates what happens with those events. `src/account/` performs the mint transactions and uses evmrelay RPC endpoints for verification.

**State Management:**
- Account state: CRDT-replicated DAG block chain per account (keyed by address in GlobalDB)
- UTXO state: CRDT-backed set per account (add-wins OR-Set semantics)
- Blockchain state: BlockTree (in-memory tree + RocksDB-persisted headers/bodies)
- Processing state: SubTaskQueue (in-memory) + SubTaskResultStorage (GlobalDB-backed)
- Consensus state: CRDT-replicated subjects/proposals via Consensus class

## Key Abstractions

**`IComponent` Interface:**
- Purpose: Base interface for all injectable services in the DI container
- Examples: `src/singleton/IComponent.hpp`
- Pattern: Single virtual method `GetName()` → `std::string`; all major service classes inherit from it

**`CComponentFactory` DI Container:**
- Purpose: Thread-safe singleton registry that wires shared `IComponent` instances at startup
- Examples: `src/singleton/CComponentFactory.hpp`, `src/singleton/CComponentFactory.cpp`
- Pattern: `RegisterComponent<T>(args...)` → `GetComponent<T>()`; used by `app/integration/` factory headers

**`outcome::result<T>` Error Handling:**
- Purpose: Zero-overhead error propagation without exceptions
- Examples: `src/outcome/outcome.hpp` (adapter), used in every subsystem
- Pattern: `OUTCOME_TRY(auto val, func())` chains results; errors returned as `outcome::failure(ErrorEnum::Code)`; `enum class` errors must be registered via `OUTCOME_HPP_DECLARE_ERROR`

**Storage Faces (Interfaces):**
- Purpose: Abstract storage interfaces enabling swappable backends
- Examples: `src/storage/face/readable.hpp`, `src/storage/face/writeable.hpp`, `src/storage/face/generic_maps.hpp`
- Pattern: `Readable` (get), `Writeable` (put/remove), `GenericMap` (both), `Iterable` (cursor), `Batchable` (batch) — composed via multiple inheritance

**CRDT `CrdtDatastore`:**
- Purpose: Delta-based add-wins CRDT OR-Set replicated over IPFS
- Examples: `src/crdt/crdt_datastore.hpp`, `src/crdt/impl/crdt_datastore.cpp`
- Pattern: `Broadcaster` (abstract) + `DAGSyncer` (abstract) injected; `PubSubBroadcaster` for pubsub transport, `GraphsyncDAGSyncer` for IPFS block sync; high-level through `GlobalDB` facade

## Entry Points

**Node CLI Entry Point:**
- Location: `node/` (top-level `node` class, `node/cli.hpp`)
- Triggers: Command-line invocation; builds `AppConfiguration`, wires subsystems through `CComponentFactory`
- Responsibilities: Parse CLI args, instantiate `GeniusNode`, orchestrate `AppStateManager` lifecycle

**GeniusNode New() Factory:**
- Location: `src/account/GeniusNode.hpp`
- Triggers: Called by `node` entry point
- Responsibilities: Initialize account identity, setup libp2p networking, create CRDT/GlobalDB, instantiate Blockchain, wire processing services, start subscription watchers

**JSON-RPC API Server:**
- Location: `src/api/transport/impl/ws/` (WebSocket), HTTP listener
- Triggers: External HTTP/WS requests
- Responsibilities: Handle `account_balance`, `account_block_count`, `room_list`, `room_join`, etc.

**EvmMessagingWatcher (Bridge Orchestrator):**
- Location: `src/watcher/impl/evm_messaging_watcher.hpp`
- Triggers: Verified bridge event observations delivered from evmrelay
- Responsibilities: Orchestrate message handling lifecycle, dedup, trigger mint verification via evmrelay RPC endpoints, coordinate response dispatch
- Note: Current implementation is a placeholder using raw WebSocket `eth_subscribe`; being migrated to use evmrelay as the Ethereum protocol library

## Architectural Constraints

- **Threading:** All I/O runs on `Boost.Asio io_context` instances. `RpcThreadPool` drives JSON-RPC with configurable thread count. `MessagingWatcher` runs dedicated `boost::thread` per watcher. CRDT and pubsub operations are async via IPFS internal Asio contexts. The `AppStateManager` lifecycle runs on the main thread; all subsystems post work to Asio contexts. Long-latency operations (disk I/O, network) use coroutines (`boost::asio::awaitable<T>`).
- **Global state:** `CComponentFactory` is a `CSingleton` (Meyers singleton) serving as the global DI registry. `DevConfig gGeniusNodeConfig` is an extern global in `src/account/GeniusNode.hpp`. `CSingleton<T>` template provides global-instance accessor in `src/singleton/Singleton.hpp`.
- **Circular imports:** Not extensively detected, but forward declarations are used extensively (e.g., `class Blockchain;` in `src/crdt/crdt_datastore.hpp`, `class ValidatorRegistry;` in `src/blockchain/Blockchain.hpp`).
- **Shared ownership:** `std::shared_ptr` used pervasively for all major service objects (not `std::unique_ptr` despite code style guidance); objects extend `std::enable_shared_from_this` for safe `shared_ptr` access from member functions.
- **C++17 only:** No C++20 features allowed (e.g., no `boost::coroutines`, no designated initializers `{.field = value}`). Target MSVC, GCC, Clang compatibility.

## Anti-Patterns

### CSingleton as Global Service Locator

**What happens:** `CComponentFactory` uses `CSingleton<T>` pattern (Meyers singleton with placement-new initialization) accessed via `SINGLETONINSTANCE(T)` macro from `src/singleton/Singleton.hpp`. Components use it as a global registry to look up dependencies at runtime.

**Trade-off:** Singleton service locators are conventionally discouraged because they hide a class's dependencies and can complicate testing. In practice, this codebase works around those concerns by registering mock/fake components into `CComponentFactory` before tests run, and the registry is treated as a lightweight lookup table rather than stateful singletons. The pattern has served the codebase adequately.

**Guidance for new code:** Constructor injection is preferred where practical (makes dependencies explicit), but runtime lookups via `CComponentFactory::GetComponent<T>()` are acceptable given the project's existing patterns.

### Shared_ptr Everywhere

**What happens:** Nearly all major service objects use `std::shared_ptr` and extend `std::enable_shared_from_this`, often passing shared ownership when exclusive ownership would suffice.
**Why it's wrong:** There's overhead from reference counting and unclear ownership semantics. The code style guide says "prefer `unique_ptr` throughout, no `shared_ptr`".
**Do this instead:** New code should prefer `std::unique_ptr` where ownership is exclusive; use `std::shared_ptr` only where genuine shared ownership is required.

### Large Header Files

**What happens:** `GeniusNode.hpp` is 839 lines; `Blockchain.hpp` is 501 lines; `crdt_datastore.hpp` is 502 lines.
**Why it's wrong:** Slow compilation times, excessive recompilation on changes, harder to navigate.
**Do this instead:** Consider Pimpl idiom or interface split for large classes; move implementation details to `impl/` subdirectories.

## Error Handling

**Strategy:** `outcome::result<T>` for all fallible operations. No exceptions in hot paths. All public methods declared `noexcept` unless they genuinely must propagate C++ exceptions.

**Patterns:**
- Return success: `return outcome::success();` or `return outcome::success(value);`
- Propagate errors: `OUTCOME_TRY(auto value, someFunction());`
- Return failure: `return outcome::failure(MyError::SomeErrorCode);`
- Error types: `enum class` values registered with outcome framework via `OUTCOME_HPP_DECLARE_ERROR` and `OUTCOME_CPP_DEFINE_CATEGORY`
- Do NOT call `.message()` on plain enum errors — use `static_cast<int>()` instead

## Cross-Cutting Concerns

**Logging:** `base::Logger` class in `src/base/logger.hpp`; spdlog used for file/console sinks. Singleton logging backend via `CSingleton`.

**Validation:** Input validation in `src/account/InputValidators.hpp`; processing validation in `src/processing/processing_validation_core.cpp`; consensus validation in `src/blockchain/Consensus.hpp`.

**Authentication:** `ConsensusAuth` class in `src/blockchain/ConsensusAuth.hpp`; signature verification in ED25519/SR25519/Secp256k1 providers; platform-native secure storage in `src/local_secure_storage/`.

**Lifecycle:** `AppStateManager` FSM: Init → Prepare → ReadyToStart → Starting → Works → ShuttingDown → ReadyToStop. Components register callbacks (`atPrepare`, `atLaunch`, `atShutdown`) or use `takeControl(entity)`.

**Serialization:** SCALE codec (`src/scale/`) for blockchain data; Protobuf for message schemas (delta, broadcast, transactions, processing, proofs, blockchain).

---

*Architecture analysis: 2026-05-25*
