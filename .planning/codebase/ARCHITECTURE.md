<!-- refreshed: 2026-05-27 -->
# Architecture

**Analysis Date:** 2026-05-27

## System Overview

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│                        APPLICATION ENTRY POINTS                              │
│                     `node/`  │  `app/integration/`                           │
│              (CLI, AppDelegate, DI wiring factories)                         │
├──────────────────┬──────────────────┬───────────────────┬───────────────────┤
│  API Transport   │  Account System  │  Processing Grid  │  EVM Bridge       │
│ `src/api/`       │ `src/account/`   │ `src/processing/` │ `src/watcher/`    │
│ (JSON-RPC, WS)   │ (UTXO, TX, DAG)  │ (tasks, subtasks) │ (event watcher)   │
└────────┬─────────┴────────┬─────────┴──────────┬────────┴──────────┬────────┘
         │                  │                     │                    │
         ▼                  ▼                     ▼                    ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                           BLOCKCHAIN / CONSENSUS                             │
│                         `src/blockchain/`                                    │
│           (Genesis, Account Creation, ConsensusManager, ValidatorRegistry)   │
└────────────────────────────────┬─────────────────────────────────────────────┘
                                 │
                                 ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                       CRDT / GLOBAL DB (Distributed State)                   │
│                          `src/crdt/`, `src/crdt/globaldb/`                   │
│            (CrdtDatastore, GlobalDB, DAG sync, PubSub broadcast, heads)      │
└────────────────────────────────┬─────────────────────────────────────────────┘
                                 │
                    ┌────────────┼────────────┐
                    ▼            ▼            ▼
┌──────────────────────┐ ┌──────────────┐ ┌──────────────────┐
│   Storage Backend    │ │  Proof Sys   │ │  Cryptography    │
│   `src/storage/`     │ │ `src/proof/` │ │  `src/crypto/`   │
│ (RocksDB, Trie, Face)│ │(zkSNARK, Ckts)│ │(ED25519, VRF,   │
│                      │ │              │ │ Hasher, BIP39)   │
└──────────────────────┘ └──────────────┘ └──────────────────┘
                    ▲            ▲            ▲
                    └────────────┼────────────┘
                                 │
┌──────────────────────────────────────────────────────────────────────────────┐
│                         INFRASTRUCTURE / BASE                                │
│  `src/base/` (Buffer, Logger)  │  `src/singleton/` (DI)  │  `src/scale/`    │
│  `src/subscription/` (pub/sub) │  `src/primitives/`      │  `src/macro/`    │
└──────────────────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                            EXTERNAL SUBMODULES                               │
│  `GeniusKDF/` (KDF)  │  `ProofSystem/` (ElGamal, ECDSA, AES)                │
│  `SGProcessingManager/` (MNN ML)  │  `evmrelay/` (EVM RLP/Disc)             │
│  `gRPCForSuperGenius/` (OpenAPI)                                             │
└──────────────────────────────────────────────────────────────────────────────┘
```

SuperGenius is a **block-lattice crypto-token system** (inspired by Nano) extended with a distributed AI/ML job-processing network, CRDT-replicated state, zkSNARK proofs, and an EVM bridge. The primary namespace is `sgns`.

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| Account System | UTXO management, transactions (transfer, mint, escrow, processing), DAG block creation | `src/account/` |
| Blockchain | Genesis/account-creation block management, consensus proposals/certificates, validator registry | `src/blockchain/` |
| CRDT / GlobalDB | Distributed key-value store with causal consistency over IPFS pubsub/Graphsync | `src/crdt/` |
| Processing Grid | Distributed ML job scheduling, subtask queues, result validation, escrow release | `src/processing/` |
| Proof System | zkSNARK proof generation/verification (Groth16, PlonK, recursive/folding) | `src/proof/` |
| Storage | RocksDB, in-memory, Trie (MPT), changes trie, storage face interfaces | `src/storage/` |
| Cryptography | ED25519, SR25519, secp256k1, VRF, hashing, BIP39 mnemonic | `src/crypto/` |
| API Transport | HTTP/WebSocket JSON-RPC server with async Boost.Asio | `src/api/transport/` |
| Watcher / EVM Bridge | Monitors EVM chains for cross-chain minting events | `src/watcher/` |
| Coin Prices | CoinGecko REST API price retrieval | `src/coinprices/` |
| Secure Storage | Platform-native encrypted key storage (Keychain, Android Keystore, DPAPI) | `src/local_secure_storage/` |
| SCALE Codec | Binary serialization codec for all on-chain data | `src/scale/` |
| Primitives | Core shared data types (Block, Header, Extrinsic, Authority) | `src/primitives/` |
| Base | Buffer, Logger (spdlog), Blob, hex utilities, version | `src/base/` |
| Singleton / DI | IComponent interface, CComponentFactory registry, CSingleton template | `src/singleton/` |
| Subscription Engine | Templated publish/subscribe event bus | `src/subscription/` |

## Pattern Overview

**Overall:** Interface-Driven + Dependency Injection + CRDT State Replication

**Key Characteristics:**
- **Interface/Impl Separation**: Every subsystem has abstract interfaces in the main directory and concrete implementations in an `impl/` subdirectory. Storage layer uses `face/` for interface definitions.
- **Static `New()` Factory**: Classes have private constructors and expose `static std::shared_ptr<ClassName> New(...)` for construction, enforcing shared ownership.
- **`outcome::result<T>` Error Propagation**: All fallible functions return `outcome::result<T>`. Use `OUTCOME_TRY` to propagate. Error types are `enum class` values.
- **`IComponent` / `CComponentFactory` DI**: All injectable services inherit `IComponent` (`virtual std::string GetName() = 0`). `CComponentFactory` is a singleton registry keyed by type+optional variant string.
- **CRDT as State Backbone**: All persistent state (accounts, blockchain, processing queues) flows through `CrdtDatastore` / `GlobalDB`, replicated over IPFS pubsub and synced via Graphsync/MerkleDAG.
- **Protobuf Serialization**: All network messages and persistent data use Protocol Buffers (`.proto` files co-located with their subsystem in `proto/` subdirectories).

## Layers

**Application Layer:**
- Purpose: Node entry point, lifecycle management, DI wiring of all subsystems
- Location: `node/`, `app/integration/`
- Contains: CLI argument parsing, `AppStateManager` FSM, component factory headers (one per subsystem)
- Depends on: All `src/` subsystems

**API / Transport Layer:**
- Purpose: JSON-RPC over HTTP and WebSocket for external clients
- Location: `src/api/transport/`, `gRPCForSuperGenius/`
- Contains: `HttpListener`, `WsListener`, `WsSession`, `RpcThreadPool`, OpenAPI specs
- Depends on: Account, Blockchain, Processing subsystems
- Used by: External clients, SDKs

**Account / Transaction Layer:**
- Purpose: UTXO-based token ledger with block-lattice DAG structure
- Location: `src/account/`
- Contains: `GeniusAccount`, `GeniusNode`, `TransferTransaction`, `MintTransaction`, `ProcessingTransaction`, `EscrowTransaction`, `UTXOManager`, `TransactionManager`, `AccountMessenger`, `MigrationManager`
- Depends on: CRDT/GlobalDB, Storage, Cryptography, Proof System, Secure Storage
- Used by: API layer, Processing pipeline, EVM Bridge

**Blockchain / Consensus Layer:**
- Purpose: Genesis/account-creation bootstrap, weighted consensus with validator voting, block storage
- Location: `src/blockchain/`
- Contains: `Blockchain` (bootstrap controller), `ConsensusManager` (proposals/votes/certificates), `ValidatorRegistry`, `BlockTree`, `BlockStorage`, `BlockHeaderRepository`
- Depends on: CRDT/GlobalDB, PubSub, Cryptography, Account
- Used by: Account layer, Processing pipeline

**Processing Grid Layer:**
- Purpose: Distributed ML/AI job scheduling with subtask queues and result validation
- Location: `src/processing/`
- Contains: `ProcessingService`, `ProcessingNode`, `ProcessingEngine`, `ProcessingCore` (interface), `ProcessingSubTaskQueueManager`, `SubTaskQueueAccessor`, `ProcessingValidationCore`
- Depends on: CRDT/GlobalDB, PubSub, Account (escrow), SGProcessingManager
- Used by: Node entrypoint, Validator nodes

**CRDT / Distributed State Layer:**
- Purpose: Causally consistent distributed key-value store replicated over IPFS
- Location: `src/crdt/`, `src/crdt/globaldb/`
- Contains: `CrdtDatastore` (add-wins OR-Set, DAG workers), `GlobalDB` (high-level API), `CrdtSet`, `CrdtHeads`, `PubSubBroadcaster`, `GraphsyncDAGSyncer`, `CRDTCallbackManager`, `KeyPairFileStorage`
- Depends on: Storage (RocksDB), IPFS pubsub, Graphsync, Protobuf
- Used by: Blockchain, Account, Processing, Proof subsystems

**Storage Layer:**
- Purpose: Persistent and in-memory key-value storage with Merkle Patricia Trie
- Location: `src/storage/`
- Contains: Face interfaces (`src/storage/face/`: `Readable`, `Writeable`, `GenericStorage`, `Batchable`), RocksDB backend (`src/storage/rocksdb/`), In-Memory backend (`src/storage/in_memory/`), Trie/MPT (`src/storage/trie/`), Changes Trie (`src/storage/changes_trie/`)
- Depends on: Base (Buffer), Thirdparty (RocksDB library)
- Used by: CRDT, Blockchain, Account

**Proof System Layer:**
- Purpose: zkSNARK proof generation/verification using nil::crypto3 library
- Location: `src/proof/`, `ProofSystem/`
- Contains: `IBasicProof`, `GeniusProver`, `GeniusAssigner`, `TransferProof`, `RecursiveTransferProof`, `ProcessingProof`, circuits (`TransactionVerifierCircuit`, `RecursiveTransactionCircuit`)
- Depends on: nil::crypto3, ProofSystem submodule
- Used by: Account (transfer proofs), Processing (computation proofs)

**Crypto / Infrastructure Layer:**
- Purpose: Cryptographic primitives, utilities, DI container, event bus
- Location: `src/crypto/`, `src/base/`, `src/singleton/`, `src/subscription/`, `src/primitives/`, `src/scale/`
- Contains: ED25519/SR25519/secp256k1 providers, VRF, hashing (blake2, keccak, sha2, twox), BIP39, Buffer/Blob/Logger, CComponentFactory, SubscriptionEngine, core data types
- Depends on: Thirdparty (libsodium, spdlog)
- Used by: All layers

## Data Flow

### Primary Request Path (Token Transfer)

1. User calls `TransferTransaction::Build()` — selects UTXOs, creates outputs (`src/account/TransferTransaction.cpp`)
2. `UTXOManager::GetUTXOs()` queries CRDT-persisted UTXO set via `GlobalDB` (`src/account/UTXOManager.cpp` → `src/crdt/globaldb/globaldb.cpp`)
3. `GeniusProver::Generate()` produces zkSNARK transfer proof (`src/proof/GeniusProver.cpp`)
4. Account signs with `ED25519Provider::sign()` (`src/crypto/`)
5. `TransactionManager::Submit()` persists to `GlobalDB::Put()` and broadcasts via `AccountMessenger` over libp2p pubsub
6. `UTXOManager::MarkSpent()` updates the UTXO set state

### Primary Request Path (Distributed ML Job)

1. Task posted with escrow locked via `EscrowTransaction` (`src/account/EscrowTransaction.cpp`)
2. `ProcessingServiceImpl` listens on GridChannel for `ProcessingChannelRequest` from nodes (`src/processing/processing_service.cpp`)
3. `ProcessTaskSplitter::SplitTask()` → list of `SubTask` objects (`src/processing/processing_tasksplit.cpp`)
4. `SubTaskEnqueuerImpl::Enqueue()` → `ProcessingSubTaskQueueChannelPubSub` → IPFS pubsub (`src/processing/processing_subtask_queue_channel_pubsub.cpp`)
5. Each `ProcessingNode` worker picks up via `SubTaskQueueAccessorImpl::GetNextSubTask()` (`src/processing/impl/`)
6. `ProcessingEngine::Execute()` → `ProcessingCoreImpl::ProcessSubTask()` → `SGProcessingManager` (MNN ML inference) (`src/processing/processing_engine.cpp`, `SGProcessingManager/`)
7. `SubTaskResult` published to results channel and stored via `SubTaskResultStorageImpl` → `GlobalDB`
8. `ProcessingValidationCore::Validate()` verifies results, triggers `EscrowReleaseTransaction`

### EVM Minting Flow

1. `EvmMessagingWatcher` monitors EVM chain via WebSocket (`src/watcher/impl/evm_messaging_watcher.cpp`)
2. Parses log events with topic filters; triggers callback
3. `MintTransaction::Build()` creates on-chain mint transaction
4. `TransactionManager::Submit()` persists and broadcasts

**State Management:**
- All shared mutable state lives in `CrdtDatastore` / `GlobalDB` (CRDT-replicated over IPFS)
- RocksDB is the local persistence backend for CRDT data, block tree, and trie nodes
- In-memory storage used for ephemeral/temporary state and unit tests
- `AppStateManager` FSM manages global application lifecycle (Init → Prepare → ReadyToStart → Starting → Works → ShuttingDown → ReadyToStop)

## Key Abstractions

**Interface/Impl Pattern:**
- Purpose: Decouple contract from implementation for testability and swappable backends
- Examples: `Blockchain.hpp` ↔ `blockchain/impl/Blockchain.cpp`, `CrdtDatastore` ↔ `crdt/impl/crdt_datastore.cpp`, `processing_core.hpp` ↔ `impl/processing_core_impl.cpp`
- Pattern: Header declares abstract/interface class; `impl/` subdirectory contains concrete `.cpp`/`.hpp` pair

**Storage Face Interfaces:**
- Purpose: Abstract storage API allowing RocksDB, in-memory, or other backends
- Examples: `src/storage/face/readable.hpp`, `writeable.hpp`, `generic_storage.hpp`, `batchable.hpp`
- Pattern: Template interfaces parameterized on key/value types; concrete backends in `rocksdb/`, `in_memory/`

**Static `New()` Factory:**
- Purpose: Enforce `shared_ptr` ownership and hide construction complexity
- Examples: `Blockchain::New(global_db, account, pubsub, callback)` (`src/blockchain/Blockchain.hpp:78`), `CrdtDatastore::New(datastore, key, dagSyncer, broadcaster, options)` (`src/crdt/crdt_datastore.hpp:92`)
- Pattern: Private constructor, public static method returns `std::shared_ptr<T>`

**Protobuf `proto/` Co-location:**
- Purpose: Message schemas live with the subsystem that owns them
- Examples: `src/crdt/proto/delta.proto`, `src/blockchain/impl/proto/SGBlockchain.proto`, `src/account/proto/SGTransaction.proto`, `src/processing/proto/SGProcessing.proto`

## Entry Points

**Node CLI:**
- Location: `node/` (top-level node class, AppDelegate, CLI argument parsing)
- Triggers: Executable launched on host device
- Responsibilities: Reads config, wires all subsystems via `app/integration/` factories, drives `AppStateManager` lifecycle FSM

**API Transport (HTTP/WebSocket):**
- Location: `src/api/transport/`
- Triggers: Incoming HTTP/WS connections on configured bind address
- Responsibilities: JSON-RPC endpoint for external client SDKs (balance queries, block submission, processing room management)

**Processing Grid:**
- Location: `src/processing/processing_node.cpp`
- Triggers: PubSub message on grid channel (`ProcessingChannelRequest`, `NodeCreationIntent`)
- Responsibilities: Spawn `ProcessingEngine` instances, execute ML subtasks, publish results

**EVM Watcher:**
- Location: `src/watcher/impl/evm_messaging_watcher.cpp`
- Triggers: WebSocket event from EVM-compatible chain
- Responsibilities: Detect cross-chain minting events, trigger `MintTransaction`

## Architectural Constraints

- **Threading:** All I/O runs on `Boost.Asio io_context` instances. `RpcThreadPool` provides configurable threads for API. `MessagingWatcher` runs dedicated `boost::thread`. CRDT `GraphsyncDAGSyncer` uses IPFS pubsub's internal Asio context. Long-latency operations use coroutines (`boost::asio::awaitable<T>`). No `std::this_thread::sleep_for` in tests.
- **Global state:** `CComponentFactory` is a global singleton (via `CSingleton<CComponentFactory>`) holding the component registry. `CrdtDatastore` holds mutable CRDT set/heads state. `AppStateManager` has global lifecycle state machine.
- **Circular imports:** `Blockchain.hpp` includes `crdt/globaldb/globaldb.hpp` and `account/GeniusAccount.hpp`; `CrdtDatastore` forward-declares `Blockchain` and `ValidatorRegistry`. Account ↔ Blockchain ↔ CRDT form a tight bidirectional dependency triangle.
- **C++ Standard:** C++17 only. No C++20 features (e.g., no `boost::coroutines` which require C++20). Use `coroutines` module from thirdparty for async.
- **Exception Handling:** By default, no exceptions. All functions should be `noexcept` unless explicitly required to throw. Error propagation via `outcome::result<T>`.

## Anti-Patterns

### Tight Coupling via Direct Includes

**What happens:** Blockchain directly includes `GeniusAccount.hpp`, `globaldb.hpp`, `Consensus.hpp`, `crdt_callback_manager.hpp`, `sgns_version.hpp` — forming hard compile-time dependency chains.
**Why it's wrong:** Changing a subsidiary header forces recompilation of the entire Blockchain subsystem. Makes unit testing individual components harder.
**Do this instead:** Use forward declarations where possible (as `CrdtDatastore.hpp` does for `Blockchain` and `ValidatorRegistry`). Prefer abstract interfaces extracted into `face/` subdirectories (as done in `src/storage/face/`).

### Static Singleton Registry for DI

**What happens:** `CComponentFactory` is a global singleton (`CSingleton<CComponentFactory>`) used as the central component registry. All subsystems register and resolve dependencies through this global.
**Why it's wrong:** Global mutable state makes testing harder (tests must reset or work around global state), prevents parallel instantiation of separate component graphs, conflates configuration with compilation.
**Do this instead:** For new subsystems, prefer passing dependencies explicitly via constructor injection (as `Blockchain::New()` does with `global_db`, `account`, `pubsub`). Limit `CComponentFactory` usage to top-level wiring only.

### Overlapping Impl Patterns

**What happens:** Most subsystems use an `impl/` subdirectory for concrete implementations (e.g., `blockchain/impl/`, `crdt/impl/`, `processing/impl/`, `watcher/impl/`, `storage/trie/impl/`). But some subsystems place concrete classes at the same level as interfaces (e.g., `account/` has both `GeniusAccount.hpp` interface-like and concrete Transactions in the same directory).
**Why it's wrong:** Inconsistent location makes it unclear which classes are interfaces intended for extension vs. concrete implementations. Developers adding new code don't know whether to create a new `impl/` file or place alongside.
**Do this instead:** Standardize: all abstract interfaces at top of subsystem directory; all concrete implementations in `impl/` subdirectory. Follow the `ProcessingCore` ↔ `processing_core_impl.hpp` pattern consistently.

## Error Handling

**Strategy:** `outcome::result<T>` for all fallible operations. Never throw exceptions.

**Patterns:**
- Functions return `outcome::result<void>` for operations that can fail with no return value
- `OUTCOME_TRY(var, expr)` macro propagates errors up the call stack
- Error types are `enum class Error` nested inside the class (e.g., `Blockchain::Error`, `CrdtDatastore::Error`)
- Error enums registered with `OUTCOME_HPP_DECLARE_ERROR_2(namespace, EnumType)` at file scope
- Do not call `.message()` on plain enums — use `static_cast<int>()` or project-provided helpers

## Cross-Cutting Concerns

**Logging:** Uses `spdlog` via `sgns::base::createLogger(tag)`. Each class instantiates a `Logger logger_ = base::createLogger("ClassName")` member. Android platform uses `spdlog::sinks::android_sink`.

**Validation:** Each subsystem validates inputs at its boundary. `Blockchain` verifies genesis/account-creation block signatures. `ConsensusManager` validates proposals against validator weights. `ProcessingValidationCore` verifies computation results.

**Authentication:** Ed25519 key pairs for identity. Libp2p peer identity via `KeyPairFileStorage` for CRDT identity. Platform-specific secure storage for private keys (`ISecureStorage` interface with per-platform implementations).

---

*Architecture analysis: 2026-05-27*
