# SuperGenius Architecture

> Generated from Doxygen XML (1,207 XML files, 454 classes/structs), all source headers, all `.proto` files, and OpenAPI definitions.  
> Namespace prefix `sgns::` is used throughout the project. All public interfaces inherit `IComponent`.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Repository Layout](#2-repository-layout)
3. [Core Design Patterns](#3-core-design-patterns)
4. [Subsystems](#4-subsystems)
   - 4.1 [Account & Transaction System](#41-account--transaction-system)
   - 4.2 [Distributed Processing Pipeline](#42-distributed-processing-pipeline)
   - 4.3 [SGProcessingManager (ML Workers)](#43-sgprocessingmanager-ml-workers)
   - 4.4 [CRDT / GlobalDB](#44-crdt--globaldb)
   - 4.5 [Blockchain](#45-blockchain)
   - 4.6 [Proof System](#46-proof-system)
   - 4.7 [Cryptography](#47-cryptography)
   - 4.8 [Storage](#48-storage)
   - 4.9 [Runtime (WebAssembly)](#49-runtime-webassembly)
   - 4.10 [API Layer](#410-api-layer)
   - 4.11 [Application Lifecycle](#411-application-lifecycle)
   - 4.12 [Node Entrypoint](#412-node-entrypoint)
   - 4.13 [Singleton / Dependency Injection](#413-singleton--dependency-injection)
   - 4.14 [Subscription / Event Bus](#414-subscription--event-bus)
   - 4.15 [Watcher / EVM Bridge](#415-watcher--evm-bridge)
   - 4.16 [Coin Prices](#416-coin-prices)
   - 4.17 [Secure Local Storage](#417-secure-local-storage)
   - 4.18 [Scale Codec](#418-scale-codec)
   - 4.19 [Primitives](#419-primitives)
   - 4.20 [gRPC / OpenAPI Interface](#420-grpc--openapi-interface)
5. [Protobuf Message Schemas](#5-protobuf-message-schemas)
6. [Cross-Subsystem Data Flow](#6-cross-subsystem-data-flow)
7. [Threading Model](#7-threading-model)
8. [Build System](#8-build-system)
9. [Error Handling Convention](#9-error-handling-convention)

---

## 1. System Overview

SuperGenius is a **block-lattice crypto-token system** (inspired by Nano) extended with:

- A **UTXO-based token ledger** with escrow, minting, and transfer semantics.
- A **distributed AI/ML job-processing network** where nodes bid on and execute subtasks (chunked ML inference, image processing) in exchange for tokens.
- A **CRDT-replicated distributed database** (GlobalDB) over IPFS pubsub/Graphsync for consensus without a global chain.
- A **zkSNARK proof system** (via `nil::crypto3` / zkLLVM) for verifiable computation and private transfer proofs.
- A substrate-inspired **WebAssembly runtime** for on-chain logic.
- An **EVM bridge watcher** that listens for cross-chain minting events.

The primary namespace is `sgns`. All key interfaces extend `IComponent` and are resolved through `IComponentFactory` / `CComponentFactory`.

---

## 2. Repository Layout

```
SuperGenius/
├── src/                        # Core library source (namespace sgns::*)
│   ├── account/                # UTXO ledger, transactions, GeniusAccount/Node
│   ├── api/                    # HTTP/WebSocket JSON-RPC server
│   ├── application/            # AppStateManager, configuration
│   ├── base/                   # Buffer, Blob, Logger, Blob<N> utilities
│   ├── blockchain/             # BlockTree, BlockStorage, BlockHeaderRepository
│   ├── coinprices/             # CoinGecko price retriever
│   ├── crdt/                   # CRDT datastore, GlobalDB, PubSub broadcaster
│   ├── crypto/                 # ED25519, SR25519, Secp256k1, BIP39, VRF, hash helpers
│   ├── local_secure_storage/   # Platform-specific encrypted key storage
│   ├── macro/                  # Utility macros
│   ├── outcome/                # outcome::result<T> adapter
│   ├── primitives/             # Block, Extrinsic, Authority, Transaction types
│   ├── processing/             # Distributed task/subtask queue and execution
│   ├── proof/                  # zkSNARK provers, assigners, transfer/processing proofs
│   ├── runtime/                # WebAssembly execution via Binaryen
│   ├── scale/                  # SCALE binary codec
│   ├── singleton/              # IComponent / CComponentFactory DI container
│   ├── storage/                # RocksDB, in-memory, Trie/MPT
│   ├── subscription/           # Pub/sub event bus
│   └── watcher/                # EVM chain event watcher
├── node/                       # Node CLI entry point, IPFS lite store
├── app/                        # Application factories (DI wiring)
│   └── integration/            # One factory header per component
├── SGProcessingManager/        # ML inference engine (MNN-based processors)
│   ├── include/
│   │   ├── processingbase/     # ProcessingManager interface
│   │   ├── processors/         # MNN_Image, MNN_Audio, MNN_ML processors
│   │   ├── datasplitter/       # ImageSplitter
│   │   └── util/               # InputTypes, logger, sha256
│   └── generated/              # FlatBuffers-generated model config structs
├── ProofSystem/                # Standalone proof subsystem
│   ├── include/ProofSystem/    # AES, ECDH, ECDSAPublicKey, ElGamal, KDF, etc.
│   └── SGProofCircuits/        # zkSNARK circuit definitions (MPCVerifier, TxVerifier)
├── GeniusKDF/                  # Key derivation function (KDFGenerator)
├── gRPCForSuperGenius/         # OpenAPI REST + gRPC interface definitions
├── docs/doxygen/xml/           # Pre-built Doxygen XML (1207 files, source of truth)
├── doxygen/                    # Doxygen configuration
├── build/                      # Platform-specific CMake build directories
│   ├── OSX/ Linux/ Windows/ Android/ iOS/
└── cmake/                      # CMake helper modules
```

---

## 3. Core Design Patterns

### 3.1 `outcome::result<T>` — Error Propagation

All fallible functions return `outcome::result<T>` (never throw). Use `OUTCOME_TRY` to propagate.  
Error types are plain `enum class` values registered with `outcome`. Do not call `.message()` on plain enums — use `static_cast<int>()` or a project-provided helper.

### 3.2 `IComponent` / `CComponentFactory` — Dependency Injection

```cpp
// Every injectable service inherits IComponent:
class IComponent {
  virtual std::string GetName() const = 0;
};

// Factory base:
class IComponentFactory { ... };

// Concrete singleton registry:
class CComponentFactory : public IComponentFactory, public CSingleton<CComponentFactory> {
  // RegisterComponent<T>(), GetComponent<T>()
};
```

All subsystems expose their concrete types through a corresponding `*Factory` header in `app/integration/`. Component construction and wiring happen at application startup through `app/integration/` factories.

### 3.3 `AppStateManager` — Lifecycle

Components register lifecycle callbacks (`atPrepare`, `atLaunch`, `atShutdown`) or use `takeControl(entity)` which calls `entity.prepare() / entity.start() / entity.stop()`.  
States: `Init → Prepare → ReadyToStart → Starting → Works → ShuttingDown → ReadyToStop`.

### 3.4 `CSingleton<T>` — Meyers Singleton Wrapper

Provides `T::GetInstance()`. Used for `CComponentFactory` and the global logging backend.

### 3.5 SCALE Codec

Binary encoding for all blockchain data. `ScaleEncoderStream` / `ScaleDecoderStream` in `sgns::scale`. Compact-integer encoding in `sgns::scale::compact`.

---

## 4. Subsystems

---

### 4.1 Account & Transaction System

**Namespace:** `sgns`  
**Source:** `src/account/`  
**Proto:** `src/account/proto/SGTransaction.proto`, `SGAccountComm.proto`

This subsystem implements the block-lattice ledger: every account has its own chain of DAG blocks, linked by parent hashes.

#### Key Classes

| Class | Kind | Purpose |
|---|---|---|
| `GeniusAccount` | class | Represents a user account. Holds `Credentials` (public key + address), manages an account's block-lattice chain via CRDT GlobalDB. Methods: `GetBalance()`, `GetAddress()`, `CreateGenesisBlock()`, `CreateAccountBlock()`. |
| `GeniusAccount::Credentials` | struct | Contains `public_key` (bytes) + `address` (string). |
| `GeniusNode` | class | Extends account with network participation (validator/processor). Holds `PriceInfo`. Can be a processing node or validator node. |
| `GeniusNode::PriceInfo` | struct | Token pricing info for processing jobs. |
| `GeniusUTXO` | class | Represents an unspent transaction output. Wraps `SGTransaction::UTXO` protobuf. Fields: `output_idx`, `amount`, `hash`, `token`. |
| `IGeniusTransactions` | class (interface) | Base interface for all transaction types. |
| `TransferTransaction` | class | UTXO-based token transfer. Serialises to `SGTransaction::TransferTx` protobuf. Selects input UTXOs, creates change outputs. |
| `MintTransaction` | class | Creates new tokens. Serialises to `SGTransaction::MintTx`. Fields: `chain_id`, `token_id`, `amount`. |
| `ProcessingTransaction` | class | Records a distributed processing job on-chain. Serialises to `SGTransaction::ProcessingTx`. References `job_cid`, `subtask_cids`, `node_addresses`, `mpc_magic_key`. |
| `EscrowTransaction` | class | Locks tokens in escrow for processing payment. Serialises to `SGTransaction::EscrowTx`. Fields: `amount`, `dev_addr`, `peers_cut`. |
| `EscrowReleaseTransaction` | class | Releases escrow funds after job completion. Serialises to `SGTransaction::EscrowReleaseTx`. Fields: `release_amount`, `release_address`, `escrow_source`, `original_escrow_hash`. |
| `UTXOManager` | class | Manages the UTXO set for an account. Tracks spent/unspent outputs in GlobalDB. Methods: `GetUTXOs()`, `GetBalance()`, `MarkSpent()`. |
| `TransactionManager` | class | Orchestrates transaction lifecycle: build → sign → broadcast → track. Tracks `TrackedTx` (state machine). Enum `State` for transaction status. |
| `TransactionManager::TrackedTx` | struct | Holds a transaction and its current state. |
| `AccountMessenger` | class | P2P messaging layer for accounts. Sends/receives signed requests over libp2p pubsub. Handles: nonce sync, block requests, UTXO queries, transaction requests, head requests. |
| `AccountMessenger::InterfaceMethods` | struct | Callbacks for each message type. |
| `AccountMessenger::RequestTask` | struct | Pending request with timeout/retry logic. |
| `MigrationManager` | class | Manages on-disk data migrations. Runs `IMigrationStep` implementations in order. |
| `IMigrationStep` | class | Interface: `Execute()` + `GetVersion()`. |
| `Migration0_2_0To1_0_0` | class | Migration step for schema v0.2.0 → v1.0.0. |
| `Migration1_0_0To3_4_0` | class | Migration step for schema v1.0.0 → v3.4.0. |
| `Migration3_4_0To3_5_0` | class | Migration step. |
| `Migration3_5_0To3_6_0` | class | Migration step. |

#### DAG Block Structure (from `SGTransaction.proto`)

Every transaction is a `DAGStruct`:
- `type` — transaction type string
- `previous_hash` — parent block hash (block-lattice link)
- `source_addr` — sender address
- `nonce` — sequence number
- `timestamp`
- `uncle_hash` — secondary parent (for merging forks)
- `data_hash` — hash of transaction payload
- `signature` — sender's signature

#### AccountMessenger Protocol (from `SGAccountComm.proto`)

All messages are signed (`SignedXxx` wrapper with `bytes signature`). Message types:
- `NonceRequest` / `NonceResponse` — nonce synchronisation
- `BlockRequest` / `BlockResponse` — fetch blocks by index or CID
- `BlockCidRequest` — fetch block by CID
- `UTXORequest` / `UTXOResponse` — query UTXO set for an address
- `TransactionRequest` — fetch transaction by hash
- `HeadRequest` — request CRDT head broadcast on specified topics

#### Key Structs

```cpp
struct InputUTXOInfo  { bytes tx_id_hash; uint32 output_index; bytes signature; };
struct OutputDestInfo { uint64 encrypted_amount; bytes dest_addr; bytes token_id; };
```

---

### 4.2 Distributed Processing Pipeline

**Namespace:** `sgns::processing`  
**Source:** `src/processing/`  
**Proto:** `src/processing/proto/SGProcessing.proto`

The processing pipeline enables a **decentralised compute network** where:
1. A *task originator* posts a job (IPFS CID + JSON parameters).
2. The job is split into subtasks by `ProcessTaskSplitter`.
3. Processing nodes pick up subtasks from a queue, execute them, publish results.
4. A validation node verifies results and releases escrow.

#### Key Interfaces & Classes

| Class | Kind | Purpose |
|---|---|---|
| `ProcessingCore` | class (interface) | Pure-virtual processing core. `SplitTask(task, subtasks)`, `ProcessSubTask(subtask, result)`. |
| `ProcessingCoreImpl` | class | Concrete implementation. Delegates to `SGProcessingManager`'s processors. |
| `ProcessingEngine` | class | Manages execution of subtasks on a node. Dequeues subtasks from `SubTaskQueueAccessor`, calls `ProcessingCore::ProcessSubTask()`, publishes `SubTaskResult` via pubsub. |
| `ProcessingNode` | class | Represents a peer in the processing network. Subscribes to the grid channel, responds to `NodeCreationIntent`, creates `ProcessingEngine` instances. |
| `ProcessingServiceImpl` | class | Top-level service coordinating the processing grid. Manages `ProcessingStatus` (per-task state). Listens on grid channel for `ProcessingChannelRequest`/`Response`. |
| `ProcessingServiceImpl::ProcessingStatus` | struct | Tracks a processing job: channel ID, queue manager, result storage, validation state. |
| `ProcessTaskSplitter` | class | Interface: `SplitTask(task) → vector<SubTask>`. |
| `ProcessingTaskQueueImpl` | class | Implements `ProcessingTaskQueue`. Stores pending `Task` messages in GlobalDB. |
| `ProcessingSubTaskQueue` | class | In-memory queue of `SubTask` items for a specific job. Handles locking, timeout, ownership. |
| `ProcessingSubTaskQueueManager` | class | Manages distributed ownership of a `ProcessingSubTaskQueue`. Handles `OwnershipRequest` protocol. |
| `ProcessingSubTaskQueueManager::OwnershipRequest` | struct | Contains `node_id` + `request_timestamp`. |
| `SubTaskQueueAccessor` | class (interface) | Provides access to the next available subtask for a processing node. |
| `SubTaskQueueAccessorImpl` | class | Concrete accessor: interacts with `ProcessingSubTaskQueueChannelPubSub` to get/return subtasks. |
| `ProcessingSubTaskQueueChannel` | class (interface) | Transport abstraction for subtask queue messages. `RequestQueueOwnership()`, `PublishQueue()`. |
| `ProcessingSubTaskQueueChannelPubSub` | class | Implements channel over IPFS GossipPubSub. `Listen()` returns a future for subscription readiness. `GetActiveNodesCount()` / `GetActiveNodes()`. |
| `SubTaskEnqueuer` | class (interface) | Interface for enqueuing subtasks into the queue. |
| `SubTaskEnqueuerImpl` | class | Concrete: publishes subtasks to `ProcessingSubTaskQueueChannelPubSub`. |
| `SubTaskResultStorage` | class (interface) | Stores `SubTaskResult` items indexed by subtask ID. |
| `SubTaskResultStorageImpl` | class | Concrete: persists results in GlobalDB/IPFS. |
| `ProcessingValidationCore` | class | Validates assembled subtask results, verifies hashes, triggers escrow release. |

#### Processing Queue Protocol (from `SGProcessing.proto`)

| Message | Purpose |
|---|---|
| `Task` | Job descriptor: `ipfs_block_id`, `json_data`, `random_seed`, `results_channel`, `escrow_path` |
| `SubTask` | Unit of work: `ipfsblock`, `json_data`, list of `ProcessingChunk`, `subtaskid` |
| `ProcessingChunk` | Chunk descriptor: `chunkid`, `n_subchunks` |
| `ProcessingQueue` | Distributed queue state: items (with locks/timestamps), owner, pending ownership requests, processed IDs |
| `SubTaskQueue` | `ProcessingQueue` + `SubTaskCollection` |
| `SubTaskResult` | Result: `result_hash`, `chunk_hashes`, `ipfs_results_data_id`, `subtaskid`, `node_address`, `token_id` |
| `GridChannelMessage` | Grid coordination: `ProcessingChannelRequest`, `ProcessingChannelResponse`, `NodeCreationIntent` |
| `ProcessingChannelMessage` | Queue channel: `SubTaskQueue` or `SubTaskQueueRequest` |

#### Processing Flow

```
Task posted (IPFS CID)
    │
    ▼
ProcessingServiceImpl  ──listens──▶  GridChannel (pubsub)
    │                                    │
    │  SplitTask                         ◀── ProcessingChannelRequest from nodes
    ▼
ProcessTaskSplitter  ──▶  SubTaskQueue (ProcessingSubTaskQueueManager)
                                │
                    ────────────┼────────────
                   │                         │
             ProcessingNode A          ProcessingNode B
                   │                         │
           SubTaskQueueAccessor       SubTaskQueueAccessor
                   │                         │
          ProcessingEngine            ProcessingEngine
                   │                         │
           ProcessingCore              ProcessingCore
         (ProcessingCoreImpl)        (ProcessingCoreImpl)
                   │                         │
       SGProcessingManager            SGProcessingManager
                   │
                   ▼
            SubTaskResult  ──▶  results_channel (pubsub)
                   │
                   ▼
        ProcessingValidationCore  ──▶  EscrowReleaseTransaction
```

---

### 4.3 SGProcessingManager (ML Workers)

**Namespace:** `sgns::sgprocessing`, `sgns::sgprocmanager`, `sgns::sgprocmanagersha`  
**Source:** `SGProcessingManager/`

Provides concrete `ProcessingCore` implementations using the **MNN (Mobile Neural Network)** inference library.

#### Key Classes

| Class | Kind | Purpose |
|---|---|---|
| `ProcessingManager` | class | Base interface for ML job management. `ProcessJob(subtask) → result`. |
| `ProcessingProcessor` | class | Abstract base for all MNN processors. Defines `Process(input) → output`. |
| `MNN_Image` (`sgns::sgprocessing::MNN_Image`) | class | Processes image data through an MNN model. |
| `MNN_Audio` (`sgns::sgprocessing::MNN_Audio`) | class | Processes audio data through an MNN model. |
| `MNN_ML` (`sgns::sgprocessing::MNN_ML`) | class | Generic ML inference processor. |
| `ImageSplitter` | class | Splits an image into tiles/chunks for parallel processing. Produces `ProcessingChunk` descriptors. |
| `InputTypes` | class | Utility for decoding job input type metadata from JSON. |

#### Generated Model Configs (`SGProcessingManager/generated/`)

FlatBuffers-generated C++ structs describe ML model topology:
- `ModelConfig`, `ModelNode`, `ModelFormat` — model structure
- `Pass`, `PassType`, `PassIoBinding` — computation passes
- `Dimensions`, `DataType`, `DataTransform` — tensor metadata
- `ShaderConfig`, `ShaderType` — GPU shader configuration
- `OptimizerConfig`, `LossFunction` — training parameters
- `Params`, `Parameter`, `ParameterType` — hyperparameters
- `IoDeclaration`, `InputFormat` — I/O specification
- `Uniform`, `Generators`, `Constraints` — shader uniforms

---

### 4.4 CRDT / GlobalDB

**Namespace:** `sgns::crdt`  
**Source:** `src/crdt/`  
**Proto:** `src/crdt/proto/delta.proto`, `src/crdt/proto/bcast.proto`, `src/crdt/globaldb/proto/broadcast.proto`

The CRDT layer provides a **distributed key-value store** with causal consistency, replicated over IPFS pubsub and Graphsync. It is the persistence backbone for account blocks, UTXO sets, and processing queue state.

#### Key Classes

| Class | Kind | Purpose |
|---|---|---|
| `GlobalDB` | class | High-level distributed database. Wraps `CrdtDatastore`. Provides `Put(key, value)`, `Get(key)`, `Remove(key)`, `Query(prefix)`. Namespace-scoped: each "database" is an IPFS pubsub topic prefix. |
| `CrdtDatastore` | class | Core CRDT implementation (add-wins set, delta-based). Merges incoming deltas, tracks DAG of changes. Has `DagWorker` and `RootCIDJob` internal workers. |
| `CrdtOptions` | struct | Configuration: topic, rebroadcast interval, DAG sync options. |
| `CrdtSet` | class | The underlying add-wins OR-Set. Tracks element additions and tombstones. |
| `CrdtHeads` | class | Tracks current CRDT heads (tip CIDs of the DAG). |
| `AtomicTransaction` | class | Batches CRDT operations. Holds list of `PendingOperation`. `Commit()` applies atomically. |
| `AtomicTransaction::PendingOperation` | struct | Single add or remove operation. |
| `Broadcaster` | class (interface) | Abstract: `Broadcast(delta)`. |
| `PubSubBroadcaster` | class | Broadcasts CRDT deltas over IPFS GossipPubSub topic. |
| `PubSubBroadcasterExt` | class | Extended broadcaster with connection management and peer filtering. |
| `DAGSyncer` | class (interface) | Abstract: `HasBlock(cid)`, `GetBlock(cid)`, `AddBlock(block)`. |
| `GraphsyncDAGSyncer` | class | Implements DAGSyncer over IPFS Graphsync protocol. Has an `LRUCIDCache` to avoid redundant fetches and a `BlacklistEntry` for misbehaving peers. |
| `HierarchicalKey` | class | CRDT key with namespace hierarchy (path-based, `/`-delimited). |
| `CRDTCallbackManager` | class | Manages put/remove callbacks fired when CRDT state changes. |
| `CRDTDataFilter` | class | Filters CRDT query results by key prefix. |
| `KeyPairFileStorage` | class | Stores libp2p key pairs for CRDT identity on disk. |

#### CRDT Delta Proto (`delta.proto`)

```
Delta  { repeated Element elements; repeated Element tombstones; uint64 priority; }
Element { string key; string id; bytes value; }
```

#### Broadcast Proto (`broadcast.proto`)

```
BroadcastMessage { PeerInfo peer { bytes id; repeated bytes addrs }; bytes data; }
```

---

### 4.5 Blockchain

**Namespace:** `sgns::blockchain`  
**Source:** `src/blockchain/`  
**Proto:** `src/blockchain/impl/proto/SGBlockchain.proto`, `SGBlocks.proto`, `ValidatorRegistry.proto`

A substrate-inspired blockchain layer providing finality, block storage, and block production consensus.

#### Key Classes

| Class | Kind | Purpose |
|---|---|---|
| `BlockTree` | struct (interface, extends `IComponent`) | Tracks all blocks in fork-aware tree. Key methods: `getBlockHeader()`, `getBlockBody()`, `addBlockHeader()`, `addBlockBody()`, `addBlock()`, `finalize()`, `getChainByBlock()`, `longestPath()`. |
| `BlockTreeImpl` | class | Concrete in-memory + persistent `BlockTree`. Has `TreeNode` (per-block) and `TreeMeta` (tree metadata). |
| `BlockStorage` | class (interface, extends `IComponent`) | Persistent block storage. `getGenesisBlockHash()`, `getLastFinalizedBlockHash()`, `getBlockHeader/Body/Data()`, `putBlockHeader/Block()`, `putJustification()`, `removeBlock()`. |
| `KeyValueBlockStorage` | class | Implements `BlockStorage` over RocksDB via `BufferStorage`. |
| `BlockHeaderRepository` | class (interface, extends `IComponent`) | Index by hash ↔ number. `getNumberByHash()`, `getHashByNumber()`, `getBlockHeader()`, `putBlockHeader()`, `removeBlockHeader()`, `getBlockStatus()`. |
| `KeyValueBlockHeaderRepository` | class | Implements `BlockHeaderRepository` over key-value storage. |
| `ValidatorRegistry` | class | Manages validator set. `WeightConfig` holds per-validator weights. |
| `Blockchain` | class | Top-level blockchain controller. `BlockchainCIDs` struct holds CIDs for genesis, account blocks. |
| `BlockchainCIDs` | struct | Genesis block CID + account creation block CID. |

#### Block Structures (from `SGBlockchain.proto`)

- `GenesisBlock` — `chain_id`, `timestamp`, `hash`, `creator_public_key`, `signature`, `version`
- `AccountCreationBlock` — `account_address`, `genesis_block_cid`, `timestamp`, `version`, `signature`, `hash`
- `GenesisBlockResponse` — `success`, `error_message`, `genesis_block`

#### Blockchain Status Enum

```cpp
enum class BlockStatus { InChain, Unknown };
```

---

### 4.6 Proof System

**Namespace:** `sgns` (proof classes), `nil::crypto3` (underlying library)  
**Source:** `src/proof/`, `ProofSystem/`, `ProofSystem/SGProofCircuits/`  
**Proto:** `src/proof/proto/SGProof.proto`

The proof system uses **zkSNARK circuits** compiled with **zkLLVM** via the `nil::crypto3` library. It supports Groth16, PlonK, HyperPlonK, and recursive/folding proofs (Nova/HyperNova). See `ProofSystem/docs/WHIR-Architecture-upgrade.md` for the planned migration to WHIR (hash-based PCS).

#### Core Proof Interfaces and Classes

| Class | Kind | Purpose |
|---|---|---|
| `IBasicProof` | class (interface) | Base interface for all proof types. `Generate()`, `Verify()`, `Serialize()`, `Deserialize()`. |
| `GeniusProver` | class | Orchestrates zkSNARK proof generation. Wraps `nil::crypto3` prover APIs. Returns `GeniusProof` (snark bytes + public inputs). |
| `GeniusProver::GeniusProof` | struct | Holds serialised proof bytes and public input vector. |
| `GeniusAssigner` | class | Generates the arithmetic circuit assignment table (witness). Returns `AssignerOutput` with `TableVectors`. |
| `GeniusAssigner::AssignerOutput` | struct | Contains the assigned table for the prover. |
| `GeniusAssigner::TableVectors` | struct | Column vectors of the assignment table. |
| `TransferProof` | class | zk-proof for confidential token transfers. Uses Pedersen commitments over elliptic curve. |
| `RecursiveTransferProof` | class | Recursive/folding proof that aggregates multiple `TransferProof`s. |
| `ProcessingProof` | class | Proof of correct distributed computation result. |
| `NilFileHelper` | class | Utilities for loading/saving nil::crypto3 proving/verifying key files. |

#### Proof Circuits (`src/proof/circuits/`, `ProofSystem/SGProofCircuits/`)

| Circuit | Purpose |
|---|---|
| `TransactionVerifierCircuit` | Verifies a transfer transaction with balance constraints |
| `TransactionVerifierCircuitTOTP` | Transaction verification with TOTP (time-based one-time password) factor |
| `TransactionValidator` | Validates that inputs/outputs balance and signatures are valid |
| `MPCVerifierCircuit` | Verifies MPC (multi-party computation) protocol execution |
| `RecursiveTransactionCircuit` | Recursive circuit that folds/aggregates multiple transaction proofs |

#### Proof Serialisation (`SGProof.proto`)

```
BaseProofData   { bytes snark; string type; }
BaseProofProto  { BaseProofData proof_data; }
TransferProofPublicInputs { bytes balance_commitment; bytes amount_commitment; bytes new_balance_commitment; bytes generator; repeated uint64 ranges; }
TransferProofProto { BaseProofData proof_data; TransferProofPublicInputs public_input; }
```

#### ProofSystem Library (`ProofSystem/include/ProofSystem/`)

| Class | Purpose |
|---|---|
| `Encryption` | Abstract base for encryption schemes |
| `AESEncryption` | AES-256 symmetric encryption (extends `Encryption`) |
| `ECDHEncryption` | ECDH key exchange based encryption (extends `Encryption`) |
| `ECDSAPublicKey` | ECDSA public key container with verification |
| `ECElGamalKeyGenerator` | EC-based ElGamal key pair generation. `ECElGamalPoint` is the ciphertext type |
| `ElGamalKeyGenerator` | Classic ElGamal key generation (`KeyGenerator::ElGamal`). Has `Params`, `PrivateKey`, `PublicKey` nested types |
| `BitcoinKeyGenerator` | Bitcoin ECDSA key pair generation. `BitcoinECDSAPublicKey` inner class |
| `EthereumKeyGenerator` | Ethereum ECDSA key pair generation. `EthereumECDSAPublicKey` inner class |
| `KDFGenerator` (ProofSystem) | Key derivation using nil::crypto3 PBKDF |
| `PrimeNumbers` | Prime generation/testing utilities. `BabyStepGiantStep` inner class for discrete log |
| `Crypto3Util` | Utility struct for nil::crypto3 type conversions |

#### GeniusKDF (`GeniusKDF/src/KDFGenerator/`)

| Class | Purpose |
|---|---|
| `KDFGenerator` | Standalone KDF library (namespace `KeyGenerator`). Derives keys using PBKDF2/Scrypt-style hashing. Separate build target, can be used independently. |

---

### 4.7 Cryptography

**Namespace:** `sgns::crypto`  
**Source:** `src/crypto/`

| Class | Kind | Purpose |
|---|---|---|
| `ED25519Provider` | interface | Ed25519 sign/verify/generate. `generateKeypair()`, `sign()`, `verify()`. |
| `ED25519ProviderImpl` | class | Concrete Ed25519 using libsodium. |
| `ED25519Keypair` | struct | `private_key` + `public_key` bytes. |
| `SR25519Provider` | interface | Schnorr/Ristretto SR25519. `generateKeypair()`, `sign()`, `verify()`. |
| `SR25519ProviderImpl` | class | Concrete SR25519. |
| `SR25519Keypair` | struct | SR25519 key pair bytes. |
| `Secp256k1Provider` | interface | secp256k1 ECDSA. `generateKeypair()`, `sign()`, `recoverPublickey()`. |
| `Secp256k1ProviderImpl` | class | Concrete secp256k1. |
| `VRFProvider` | interface | Verifiable Random Function. `sign()` → `VRFOutput`, `verify()` → `VRFVerifyOutput`. |
| `VRFProviderImpl` | class | Concrete VRF (Ristretto-VRF). |
| `VRFOutput` | struct | VRF output bytes + proof bytes. |
| `VRFVerifyOutput` | struct | VRF verification result + output. |
| Hash helpers | free functions | Multi-algorithm hashing (blake2b, keccak, sha2, twox). |
| `CryptoStore` | interface | Key store: list, generate, get keypairs by key type. |
| `CryptoStoreImpl` | class | Concrete in-memory + file-backed key store. |
| `Bip39Provider` | interface | BIP39 mnemonic generation and seed derivation. |
| `Bip39ProviderImpl` | class | Concrete BIP39. |
| `Bip39::Dictionary` | class | Word list abstraction (English default). |
| `Bip39::EntropyAccumulator` | class | Accumulates entropy bits. |
| `Bip39::Mnemonic` | struct | Mnemonic word list + entropy. |
| `Pbkdf2Provider` | interface | PBKDF2 key derivation. |
| `Pbkdf2ProviderImpl` | class | Concrete PBKDF2. |

#### Crypto Constants

- `sgns::crypto::constants::ed25519` — key/signature sizes
- `sgns::crypto::constants::sr25519` — key/signature sizes
- `sgns::crypto::constants::sr25519::vrf` — VRF output/proof sizes
- `sgns::crypto::secp256k1::constants` — key/signature sizes
- `sgns::crypto::key_types` — key type IDs for the key store

---

### 4.8 Storage

**Namespace:** `sgns::storage`  
**Source:** `src/storage/`

#### Storage Faces (Interfaces) — `src/storage/face/`

| Interface | Purpose |
|---|---|
| `Readable` | `get(key) → value` |
| `Writeable` | `put(key, value)`, `remove(key)` |
| `ReadOnlyMap` | Extends `Readable` |
| `GenericMap` | Extends `Readable` + `Writeable` |
| `GenericStorage` | `GenericMap` + cursor + batch |
| `Iterable` | `cursor() → MapCursor` |
| `MapCursor` | `seekToFirst()`, `next()`, `isValid()`, `key()`, `value()` |
| `Batchable` | `batch() → WriteBatch` |
| `WriteBatch` | `put()`, `remove()`, `commit()` |
| `BatchWriteMap` | `GenericMap` + `Batchable` |

#### RocksDB — `src/storage/rocksdb/`

| Class | Purpose |
|---|---|
| `rocksdb` | Implements `BufferStorage` (= `BatchWriteMap<Buffer, Buffer>`). Factory: `rocksdb::create(path, options)`. Supports prefix queries: `query(prefix)`, wildcard queries `query(prefix_base, middle_part, remainder_prefix)`. |
| `rocksdb::Batch` | Atomic write batch. |
| `rocksdb::Cursor` | RocksDB iterator adapter. |

#### In-Memory Storage — `src/storage/in_memory/`

| Class | Purpose |
|---|---|
| `InMemoryStorage` | `std::map`-backed storage. Used in tests and ephemeral contexts. |
| `InMemoryBatch` | In-memory write batch. |

#### Trie / Patricia Merkle Tree — `src/storage/trie/`

| Class | Purpose |
|---|---|
| `TrieStorage` | Interface: `getPersistentBatch()`, `getEphemeralBatch()`, `getPersistentBatchAt(root)`, `getEphemeralBatchAt(root)`, `getRootHash()`. |
| `TrieStorageImpl` | Concrete `TrieStorage`. |
| `TrieStorageBackend` | Raw byte-level backend for trie nodes (wraps RocksDB). |
| `TrieStorageBackendImpl` | Concrete backend. |
| `TrieStorageBackendBatch` | Batched writes to backend. |
| `SuperGeniusTrie` | Interface: Merkle Patricia Trie with `put()`, `get()`, `remove()`, `getRoot()`. |
| `SuperGeniusTrieImpl` | Concrete trie. Uses `SuperGeniusNode` (branch/leaf/dummy). |
| `SuperGeniusTrieFactory` | Interface: `createEmpty()`, `createFromRoot(hash)`. |
| `SuperGeniusTrieFactoryImpl` | Concrete factory. |
| `SuperGeniusTrieCursor` | Trie iterator. `TriePathEntry` struct tracks path during traversal. |
| `SuperGeniusCodec` | Encodes/decodes trie nodes to/from bytes (compact encoding). Extends `Codec`. |
| `SuperGeniusNode` | Node variant: `BranchNode` / `LeafNode` / `DummyNode`. |
| `PersistentTrieBatch` | Write-back trie batch: changes flushed on `commit()`. |
| `PersistentTrieBatchImpl` | Concrete. |
| `EphemeralTrieBatch` | In-memory only, never committed. |
| `EphemeralTrieBatchImpl` | Concrete. |
| `TopperTrieBatch` | Batches on top of another batch (overlay semantics). |
| `TopperTrieBatchImpl` | Concrete. |
| `TrieSerializer` | Serialises/deserialises the full trie to/from storage backend. |
| `TrieSerializerImpl` | Concrete. |
| `KeyNibbles` | Nibble-encoded key type. |
| `BufferStream` | Byte stream helper for serialisation. |

#### Changes Trie — `src/storage/changes_trie/`

Tracks which storage keys were modified in each block (for light client proofs).

| Class | Purpose |
|---|---|
| `ChangesTracker` | Interface: records extrinsic-level storage changes. |
| `StorageChangesTrackerImpl` | Concrete tracker. |
| `ChangesTrie` | Merkle trie of changes. Nested key types: `BlocksChangesKey`, `ChildChangesKey`, `ExtrinsicsChangesKey`, `KeyIndex`. |
| `ChangesTrieConfig` | Configuration: digest intervals. |

---

### 4.9 Runtime (WebAssembly)

**Namespace:** `sgns::runtime`, `sgns::runtime::binaryen`  
**Source:** `src/runtime/`

The runtime executes on-chain WebAssembly modules using the **Binaryen** interpreter.

#### Runtime Interfaces

| Interface | Purpose |
|---|---|
| `Core` | `version()`, `execute_block()`, `initialise_block()`, `authorities()` |
| `BlockBuilder` | `apply_extrinsic()`, `finalise_block()`, `inherent_extrinsics()`, `check_inherents()`, `random_seed()` |
| `TaggedTransactionQueue` | `validate_transaction()` |
| `ProductionApi` | Block production helpers |
| `FinalityApi` | Finality-related queries |
| `Metadata` | Runtime metadata |
| `OffchainWorker` | Off-chain worker execution |
| `ParachainHost` | Parachain validation host |
| `WasmProvider` | `getStateCode() → Buffer` (WASM bytecode) |
| `TrieStorageProvider` | Trie state access for runtime |

#### Binaryen Implementations

| Class | Purpose |
|---|---|
| `WasmModule` | Interface: single WASM module |
| `WasmModuleImpl` | Concrete Binaryen module |
| `WasmModuleFactory` | Interface: `createModule(code)` |
| `WasmModuleFactoryImpl` | Concrete factory |
| `WasmModuleInstance` | Interface: running WASM instance |
| `WasmModuleInstanceImpl` | Concrete instance |
| `WasmExecutor` | Calls WASM exported functions |
| `WasmMemoryImpl` | Linear WASM memory (implements `WasmMemory`) |
| `RuntimeExternalInterface` | Binaryen host function implementation (imports) |
| `RuntimeEnvironment` | Bundle of module, memory, and external interface |
| `RuntimeManager` | Caches and manages runtime instances |
| `CoreImpl` | `Core` over Binaryen |
| `CoreFactoryImpl` | Factory for `CoreImpl` |
| `BlockBuilderImpl` | `BlockBuilder` over Binaryen |
| `ProductionApiImpl` | `ProductionApi` over Binaryen |
| `FinalityApiImpl` | `FinalityApi` over Binaryen |
| `MetadataImpl` | `Metadata` over Binaryen |
| `OffchainWorkerImpl` | `OffchainWorker` over Binaryen |
| `ParachainHostImpl` | `ParachainHost` over Binaryen |
| `TaggedTransactionQueueImpl` | `TaggedTransactionQueue` over Binaryen |
| `TrieStorageProviderImpl` | `TrieStorageProvider` over Binaryen |
| `ConstWasmProvider` | `WasmProvider` from a fixed buffer |
| `StorageWasmProvider` | `WasmProvider` that reads WASM from trie storage |
| `FinalityApiDummy` | `FinalityApi` no-op stub (runtime::dummy) |
| `WasmResult` | Return value container from WASM call |

---

### 4.10 API Layer

**Namespace:** `sgns::api`  
**Source:** `src/api/`

Exposes JSON-RPC over HTTP and WebSocket.

#### Interfaces

| Class | Purpose |
|---|---|
| `AuthorApi` | Extrinsic submission: `submitExtrinsic()`, `pendingExtrinsics()`, `removeExtrinsic()` |
| `Listener` | Transport listener (server). `Configuration` has bind address/port. |
| `Session` | Active client connection |

#### Implementations

| Class | Purpose |
|---|---|
| `HttpListenerImpl` | Boost.Asio HTTP server. |
| `HttpSession` | Single HTTP request/response. `Configuration`: max request body size. |
| `WsListenerImpl` | Boost.Asio WebSocket server. |
| `WsSession` | Active WebSocket connection. `Configuration`: max frame size. |
| `WsClientImpl` | WebSocket client (used by `EvmMessagingWatcher`). |
| `RpcContext` | IO context for RPC thread pool. |
| `RpcThreadPool` | Thread pool for RPC handlers. `Configuration`: thread count. |

---

### 4.11 Application Lifecycle

**Namespace:** `sgns::application`  
**Source:** `src/application/`

#### Application Types

| Class | Purpose |
|---|---|
| `SgnsApplication` | Base application class |
| `BlockProducingNodeApplication` | Full block-producing node |
| `ValidatingNodeApplication` | Validator-only node |
| `SyncingNodeApplication` | Sync-only (light) node |
| `BridgingNodeApplication` | Cross-chain bridge node |
| `AppStateManager` | Lifecycle FSM (see §3.3) |
| `AppStateManagerImpl` | Concrete implementation |
| `AppConfiguration` | Interface for node configuration (ports, peers, key files, etc.) |
| `AppConfigurationImpl` | Reads JSON/YAML config. `SegmentHandler` processes config sections. |
| `ConfigurationStorage` | Persists application configuration |
| `ConfigurationStorageImpl` | JSON-backed configuration persistence |
| `KeyStorage` | Interface for key file access |
| `LocalKeyStorage` | File-system-backed key storage |
| `AppStateException` | Thrown when illegal state transition occurs |

#### DI Factories (`app/integration/`)

One header per component, each providing a `Create()` method used by `CComponentFactory`:

`ApiServiceFactory`, `AppConfigurationFactory`, `AppStateManagerFactory`, `AuthorApiFactory`, `AuthorityManagerFactory`, `AuthorityUpdateObserverFactory`, `BlockBuilderManager`, `BlockExecutorFactory`, `BlockHeaderRepositoryFactory`, `BlockStorageFactory`, `BlockTreeFactory`, `BlockValidatorFactory`, `BufferStorageFactory`, `ChainApiFactory`, `ConfigurationStorageFactory`, `ED25519KeyPairFactory`, `ED25519ProviderFactory`, `EnvironmentFactory`, `EpochStorageFactory`, `ExtrinsicGossiperFactory`, `ExtrinsicObserverFactory`, `FinalityFactory`, `HasherFactory`, `JRpcProcessorFactory`, `JRpcServerFactory`, `KeyStorageFactory`, `ListenerFactory`, `OwnPeerInfoFactory`, `PoolModeratorFactory`, `ProductionConfigurationFactory`, `ProductionFactory`, `ProductionLotteryFactory`, `ProductionSynchronizerFactory`, `ProposerFactory`, `RouterFactory`, `RpcContextFactory`, `RpcThreadPoolFactory`, `SR25519KeypairFactory`, `SR25519ProviderFactory`, `StateApiFactory`, `SteadyClockFactory`, `StorageChangesTrackerFactory`, `SyncProtocolObserverFactory`, `SystemApiFactory`, `SystemClockFactory`, `TranscationPoolFactory`, `TrieSerializerFactory`, `TrieStorageBackendFactory`, `TrieStorageFactory`, `VRFProviderFactory`

---

### 4.12 Node Entrypoint

**Namespace:** `sgns` (top-level)  
**Source:** `node/`

| Class | Purpose |
|---|---|
| `node` | Top-level node class. Wires all subsystems together, drives `AppStateManager`. |
| `AppDelegate` | OS-level application delegate (lifecycle hooks from OS). |
| `ipfs_lite_store` | IPFS-lite block store adapter. `IPFS_val` struct for raw block bytes. |
| CLI (`node/cli.hpp`) | Command-line argument parsing; builds `AppConfiguration`. |
| `node/config/` | Default node configuration files. |

---

### 4.13 Singleton / Dependency Injection

**Namespace:** `sgns` (top-level), `sgns::singleton` (implicitly)  
**Source:** `src/singleton/`

| Class | Purpose |
|---|---|
| `IComponent` | Base interface for all injectable components. `GetName() → string`. |
| `IComponentFactory` | Factory interface: `RegisterComponent<T>()`, `GetComponent<T>()`. |
| `CComponentFactory` | Concrete singleton factory. Extends `CSingleton<CComponentFactory>`. Thread-safe registry of shared `IComponent` instances. |
| `CSingleton<T>` | CRTP Meyers singleton. `T::GetInstance() → T&`. |

---

### 4.14 Subscription / Event Bus

**Namespace:** `sgns::subscription`  
**Source:** `src/subscription/`

| Class | Purpose |
|---|---|
| `SubscriptionEngine<EventT>` | Templated publish/subscribe engine. `subscribe()`, `unsubscribe()`, `notify(event)`. |
| `Subscriber<EventT>` | Subscriber handle. Holds callback and subscription ID. |

Used throughout the system for internal event propagation (storage changes, block finality, extrinsic pool events).

---

### 4.15 Watcher / EVM Bridge

**Namespace:** `sgns::watcher`, `sgns::evmwatcher`  
**Source:** `src/watcher/`

| Class | Purpose |
|---|---|
| `MessagingWatcher` | Abstract base for message watchers. Manages a background `boost::thread`. `startWatching()`, `stopWatching()`. Static registry: `addWatcher()`, `startAll()`, `stopAll()`. |
| `EvmMessagingWatcher` | Monitors Ethereum-compatible chains for smart contract events via WebSocket (`WsClientImpl`). Config: `ChainConfig` (rpc_url, chain_id, chain_name, ws_url). Filters: `TopicFilter` (topic hash). Used to detect cross-chain minting events and trigger `MintTransaction`. |
| `EvmMessagingWatcher::ChainConfig` | Chain connection parameters. |

---

### 4.16 Coin Prices

**Namespace:** `sgns`  
**Source:** `src/coinprices/`

| Class | Purpose |
|---|---|
| `CoinGeckoPriceRetriever` | Queries the CoinGecko REST API for current and historical token prices. `getCurrentPrices(tokenIds)`, `getHistoricalPrices(tokenIds, timestamps)`, `getHistoricalPriceRange(tokenIds, from, to)`. Returns `map<string, double>`. Error enum: `EmptyInput`, `NetworkError`, `JsonParseError`, `NoDataFound`, `RateLimitExceeded`, `DateTooOld`. Uses Boost.Asio for HTTP. |

---

### 4.17 Secure Local Storage

**Namespace:** `sgns`  
**Source:** `src/local_secure_storage/`

Stores sensitive key material (private keys, credentials) in platform-native secure stores.

| Class | Platform | Purpose |
|---|---|---|
| `ISecureStorage` | — | Interface: `Load(key)`, `Save(key, buffer)`, `DeleteKey(key)`. Returns `outcome::result`. |
| `JSONBackend` | All | JSON file-backed storage (fallback). |
| `JSONSecureStorage` | All | `ISecureStorage` over JSON files. |
| `AppleSecureStorage` | macOS/iOS | Keychain-backed secure storage. |
| `AndroidSecureStorage` | Android | Android Keystore-backed. |
| `LinuxSecureStorage` | Linux | Linux Secret Service (libsecret) or file-backed. |
| `WindowsSecureStorage` | Windows | Windows DPAPI-backed. |

Key type: `SecureBufferType = std::string`.

The `ai::gnus::sdk::KeyStoreHelper` class provides a cross-platform facade.

---

### 4.18 Scale Codec

**Namespace:** `sgns::scale`  
**Source:** `src/scale/`

SCALE (Simple Concatenated Aggregate Little-Endian) binary codec — the serialisation format for all on-chain data.

| Class | Purpose |
|---|---|
| `ScaleEncoderStream` | Output stream: `operator<<` for all primitive types, vectors, optionals, variants. |
| `ScaleDecoderStream` | Input stream: `operator>>` for all types. |
| `compact::EncodingCategoryLimits` | Compact integer category boundaries. |
| `detail::TupleEncoder` / `TupleDecoder` | Tuple serialisation helpers. |
| `detail::VariantEncoder` / `VariantDecoder` | `std::variant` serialisation helpers. |

---

### 4.19 Primitives

**Namespace:** `sgns::primitives`  
**Source:** `src/primitives/`

Core data types shared across all subsystems:

| Type | Kind | Description |
|---|---|---|
| `Block` | struct | `header` + `body` (extrinsics) |
| `BlockHeader` | struct | `parent_hash`, `number`, `state_root`, `extrinsics_root`, `digest` |
| `BlockData` | struct | `header` + optional `body`, `justification`, etc. |
| `Extrinsic` | struct | Raw bytes of an extrinsic (transaction) |
| `Transaction` | struct | Decoded transaction |
| `ValidTransaction` | struct | Validated transaction with priority/longevity |
| `Justification` | struct | Finality justification bytes |
| `Authority` | struct | Validator authority (ID + weight) |
| `AuthorityId` | struct | Authority public key |
| `AuthorityList` | struct | `vector<Authority>` |
| `AuthorityListChange` | struct | Scheduled authority set change |
| `ProductionConfiguration` | struct | Slot duration, epoch length, authorities |
| `Version` | struct | Runtime version metadata |
| `InherentData` | struct | Block inherent data (timestamp, etc.) |
| `CheckInherentsResult` | struct | Result of inherent checks |
| `Session` | struct | Session parameters |
| `Seal` | struct | Block seal (VRF proof + signature) |
| `PreRuntime` | struct | Pre-runtime digest item |
| `ScheduledChange` | struct | Scheduled authority change with delay |
| `ForcedChange` | struct | Forced authority change |
| `Pause` / `Resume` | struct | Production pause/resume events |
| `DelayInChain` | struct | Delay counted in blocks |
| `OnDisabled` | struct | Authority disable event |
| `Subscriber` | struct | Subscription handle |
| `detail::BlockInfoT<T>` | struct | Block info template (hash + number) |
| `detail::DigestItemCommon` | struct | Common digest item fields |
| `parachain::Relay` | struct | Parachain relay data |

**Base types** (`src/base/`):

| Type | Purpose |
|---|---|
| `base::Blob<N>` | Fixed-size byte array. Hash specialisations: `Hash256`, `Hash512`. |
| `base::Buffer` | Dynamic byte array with hex encoding. |
| `base::Wrapper<T, Tag>` | Strong typedef wrapper. |

---

### 4.20 gRPC / OpenAPI Interface

**Source:** `gRPCForSuperGenius/`

Two OpenAPI REST specifications define the external HTTP interface:

#### `SuperGenius-OpenAPI.yaml` — Token / Account API

| Endpoint | Method | Purpose |
|---|---|---|
| `/account_balance` | GET | Get token balance for an account |
| `/account_block_count` | GET | Get number of blocks for an account |
| (and more) | | Account and token management endpoints |

#### `SGProcessing-OpenAPI.yaml` — Processing Grid API

| Endpoint | Method | Purpose |
|---|---|---|
| `/room_list` | GET | List all active processing rooms/channels |
| `/room_get` | GET | Create or get a processing room by topic |
| `/room_join` | GET | Join a processing room with a wallet |
| `/room_leave` | GET | Leave a processing room |
| `/broadcast_message` | GET | Broadcast message to all peers in a room |
| `/send_message` | GET | Send direct message to a specific peer |
| `/has_peer` | GET | Check if a peer is in the room |

The `SuperGenius_OpenAPIImpl` class implements the generated server stubs.

---

## 5. Protobuf Message Schemas

Summary of all `.proto` files in the project:

| File | Package | Key Messages |
|---|---|---|
| `src/account/proto/SGTransaction.proto` | `SGTransaction` | `DAGStruct`, `TransferTx`, `ProcessingTx`, `MintTx`, `EscrowTx`, `EscrowReleaseTx`, `UTXOTxParams`, `UTXO`, `UTXOList` |
| `src/account/proto/SGAccountComm.proto` | `accountComm` | `AccountMessage` (oneof: nonce, block, UTXO, tx requests/responses), all as `SignedXxx` wrappers |
| `src/blockchain/impl/proto/SGBlockchain.proto` | `sgns.blockchain` | `GenesisBlock`, `AccountCreationBlock`, `GenesisBlockResponse` |
| `src/processing/proto/SGProcessing.proto` | `SGProcessing` | `Task`, `SubTask`, `ProcessingChunk`, `ProcessingQueue`, `SubTaskQueue`, `SubTaskResult`, `GridChannelMessage`, `ProcessingChannelMessage` |
| `src/proof/proto/SGProof.proto` | `SGProof` | `BaseProofData`, `BaseProofProto`, `TransferProofPublicInputs`, `TransferProofProto` |
| `src/crdt/proto/delta.proto` | `sgns.crdt.pb` | `Delta` (elements + tombstones), `Element` |
| `src/crdt/proto/bcast.proto` | `sgns.crdt.pb` | Broadcast wrapper |
| `src/crdt/globaldb/proto/broadcast.proto` | `sgns.crdt.broadcasting` | `BroadcastMessage` (peer info + data) |

---

## 6. Cross-Subsystem Data Flow

### Token Transfer Flow

```
User calls TransferTransaction::Build()
    │
    ├─ UTXOManager::GetUTXOs() ──▶ GlobalDB (CRDT) ──▶ RocksDB
    ├─ GeniusProver::Generate() ──▶ TransferProof (zkSNARK)
    ├─ ED25519Provider::sign()
    │
    ▼
TransactionManager::Submit()
    │
    ├─ AccountMessenger::Broadcast() ──▶ libp2p pubsub
    │
    ├─ GlobalDB::Put(tx_hash, TransferTx proto)
    │
    └─ UTXOManager::MarkSpent()
```

### Distributed Processing Flow

```
Job submitted (escrow locked via EscrowTransaction)
    │
    ▼
ProcessingServiceImpl listens on GridChannel
    │
    ├─ ProcessTaskSplitter::SplitTask() ──▶ list of SubTask
    │
    ├─ SubTaskEnqueuerImpl::Enqueue() ──▶ ProcessingSubTaskQueueChannelPubSub ──▶ IPFS pubsub
    │
    └─ ProcessingNode (each worker):
          │
          ├─ SubTaskQueueAccessorImpl::GetNextSubTask()
          │
          ├─ ProcessingEngine::Execute()
          │     └─ ProcessingCoreImpl::ProcessSubTask()
          │           └─ SGProcessingManager (MNN inference)
          │
          ├─ SubTaskResult published to results_channel
          │
          └─ SubTaskResultStorageImpl::Store() ──▶ GlobalDB
                │
                ▼
          ProcessingValidationCore::Validate()
                │
                └─ EscrowReleaseTransaction::Submit() ──▶ TransactionManager
```

### EVM Minting Flow

```
EVM smart contract event emitted
    │
    ▼
EvmMessagingWatcher (WebSocket to EVM node)
    │
    ├─ Parses log: contract_address + topic filters
    │
    └─ Callback → MintTransaction::Build()
          │
          └─ TransactionManager::Submit() ──▶ GlobalDB + pubsub
```

---

## 7. Threading Model

- All I/O runs on **Boost.Asio `io_context`** instances.
- The `RpcThreadPool` drives the JSON-RPC API with a configurable thread count.
- `MessagingWatcher` and `EvmMessagingWatcher` run a dedicated `boost::thread` per watcher.
- `ProcessingSubTaskQueueChannelPubSub` and CRDT `GraphsyncDAGSyncer` are async, using IPFS pubsub's internal Boost.Asio context.
- Long-latency operations (disk I/O, network) should use **coroutines** (`boost::asio::awaitable<T>`). See `AGENT_MISTAKES.md` M013 for `co_spawn` constraints.
- The `AppStateManager` lifecycle runs on the main thread; all subsystems post work to Asio contexts.
- **No `std::this_thread::sleep_for` in tests** — use condition variables or polling templates.

---

## 8. Build System

**CMake + Ninja** per platform. Each platform has its own build directory.

```
build/<Platform>/<BuildType>/   e.g.  build/OSX/Debug/
```

Build pattern:
```bash
cd build/<Platform>/<BuildType>
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=<BuildType>
ninja
```

**Supported platforms:** `OSX`, `Linux`, `Windows`, `Android` (armeabi-v7a, arm64-v8a, x86_64), `iOS`.

**Key CMake variables:**
- `THIRDPARTY_DIR` — path to the `thirdparty` repo (resolved automatically if sibling directory)
- `TESTING=ON|OFF` — enable unit tests
- `ABI_SUBFOLDER_NAME` — ABI subfolder for cross-compilation (e.g. `aarch64`)

**Sub-project builds** (separate CMake roots, same pattern):
- `GeniusKDF/` — standalone KDF library
- `ProofSystem/` — standalone proof system
- `ProofSystem/SGProofCircuits/` — standalone circuit library
- `SGProcessingManager/` — standalone ML processing library

**Testing:** `ctest -C <BuildType>` from build directory. Test names use `ctest -R <name>`.

---

## 9. Error Handling Convention

All fallible public functions return `outcome::result<T>`. The pattern is:

```cpp
// Return success:
return outcome::success();

// Return a value:
return outcome::success(value);

// Propagate errors:
OUTCOME_TRY(auto value, someFunction());

// Return an error:
return outcome::failure(MyError::SomeErrorCode);
```

Error types are `enum class` values. They must be registered with the outcome framework via `OUTCOME_HPP_DECLARE_ERROR` and `OUTCOME_CPP_DEFINE_CATEGORY`. Do **not** call `.message()` on a plain enum — use `static_cast<int>()` or the project's category `to_string()`.

Exceptions are **not used** in hot paths. All public methods are declared `noexcept` unless the function genuinely must propagate C++ exceptions (rare). Use `outcome::result` instead.
