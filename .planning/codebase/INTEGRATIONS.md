# External Integrations

**Analysis Date:** 2026-05-25

## APIs & External Services

### Ethereum / EVM Networks

The `evmrelay/` submodule provides the Ethereum protocol library with three responsibilities:
- **Watcher Service** — P2P peer discovery (discv4/discv5), RLPx transport, ETH subprotocol event watching, bridge event types
- **Public RPC List Provider** — Ingestion of public RPC endpoint metadata from chain lists
- **RPC Connection Maker** — Multi-endpoint RPC pool (`RpcManager`), HTTP transport, receipt verification

evmrelay is consumed by SuperGenius as a library:
- `src/watcher/` orchestrator receives verified observations from evmrelay and manages message handling lifecycle
- `src/account/` uses evmrelay's `RpcManager` to independently verify bridge events via multiple RPC endpoints before minting

### CoinGecko Price API

The `src/coinprices/` module provides real-time cryptocurrency price data:

- **Service:** CoinGecko API v3 (`api.coingecko.com`)
  - SDK/Client: Custom `CoinGeckoPriceRetriever` class (`src/coinprices/coinprices.hpp`)
  - HTTP via `FileManager::LoadASync()` using Boost.ASIO
  - Endpoints used:
    - `/api/v3/simple/price` - Current prices in USD
    - `/api/v3/coins/{id}/history` - Historical price at a specific date
    - `/api/v3/coins/{id}/market_chart/range` - Price range with timestamps
  - Error handling: `PriceError` enum (EmptyInput, NetworkError, JsonParseError, NoDataFound, RateLimitExceeded, DateTooOld)
  - Rate limiting: 1100ms delay between token requests
  - Free tier limitation: 365-day historical data window
  - JSON parsing: RapidJSON
  - Auth: No API key required (CoinGecko free tier)

### IPFS / libp2p Network

Decentralized storage and peer-to-peer messaging:

- **IPFS Storage** (via `ipfs-lite-cpp`):
  - Content-addressed storage using CIDs
  - MerkleDAG service for content integrity
  - GraphSync for efficient data synchronization
  - Bitswap for block exchange protocol
  - Used by CRDT datastore: `src/crdt/CMakeLists.txt` (links `ipfs-lite-cpp::cid`, `ipfs-lite-cpp::ipfs_merkledag_service`, `ipfs-lite-cpp::graphsync`)
- **IPFS PubSub** (via `ipfs-pubsub`):
  - Publish/subscribe messaging over IPFS
  - Used by: `src/processing/CMakeLists.txt` (processing subtask queue channel)
  - Example apps: `example/ipfs_pubsub/`, `example/ipfs_client/`, `example/ipfs_client2/`
- **libp2p** - Peer-to-peer networking framework
  - Node discovery, connection management, protocol multiplexing
  - Config: `build/CommonBuildParameters.cmake` lines 240-241

### GPU Compute (Vulkan / MoltenVK)

- **Vulkan API** - Cross-platform GPU compute for AI/ML processing
  - macOS: MoltenVK (Vulkan over Metal) via `build/CommonBuildParameters.cmake` lines 30-31
  - Used by: `src/processing/CMakeLists.txt` (links `Vulkan::Vulkan`)
  - Apple framework dependencies: CoreFoundation, CoreGraphics, CoreServices, IOKit, IOSurface, Metal, QuartzCore, AppKit/UIKit

### Artificial Intelligence (MNN)

- **MNN** (Mobile Neural Network) - AI/ML inference engine
  - Used by: `src/processing/CMakeLists.txt` (links `MNN::MNN`)
  - Config: `build/CommonBuildParameters.cmake` lines 92-103

### Zero-Knowledge Proofs (zkLLVM)

- **zkLLVM** - ZK circuit compiler and proof system
  - Built on LLVM infrastructure
  - Auto-downloaded from GitHub Releases (`GeniusVentures/zkLLVM`) if not locally available
  - Config: `build/CommonCompilerOptions.cmake` lines 57-126
  - crypto3 library suite: algebra, block, blueprint, codec, math, multiprecision, pkpad, pubkey, random, zk
  - marshalling libs: core, crypto3_algebra, crypto3_multiprecision, crypto3_zk
  - Used by: `src/proof/` - proof generation (GeniusProver, TransferProof, ProcessingProof)
  - Links: LLVMIRReader, LLVMCore, LLVMSupport (from `src/proof/CMakeLists.txt` line 12-15)

### gRPC / OpenAPI

- **gRPC** - Service communication via protocol buffers
  - API defined in `gRPCForSuperGenius/openapi_yaml/SuperGenius-OpenAPI.yaml` (2459 lines)
  - Processing API: `gRPCForSuperGenius/openapi_yaml/SGProcessing-OpenAPI.yaml`
  - Binary proto: `gRPCForSuperGenius/sg.pb`
  - Proto-to-code via protoc (`PROTOC_EXECUTABLE`)
  - API routes include: `/account_balance`, `/account_block_count`, `/account_create`, `/account_get`, `/account_history`, `/account_info`, `/block`, `/block_count`, `/block_create`, `/bootstrap`, `/delegators`, `/send`, `/receive`, `/wallet_*`, and many more
  - Transport: `src/api/transport/impl/ws/ws_client_impl.cpp` (WebSocket-based)

### Wallet Services (TrustWallet)

- **TrustWallet Core** - Multi-chain wallet operations
  - `TrezorCrypto` - Cryptographic operations for wallet keys
  - `wallet_core_rs` - Rust-based wallet core
  - Used by: `src/account/` - GeniusAccount, GeniusNode, UTXOManager, TransactionManager
  - Apple: requires CoreFoundation + Security frameworks

## Data Storage

**Databases:**
- **RocksDB** - Primary persistent blockchain state database
  - Client: `src/storage/rocksdb/rocksdb.hpp` (custom C++ wrapper)
  - Features: batch operations, cursor/iterator, Snappy compression
  - Used by: blockchain state, CRDT data store, trie storage
  - Config: `build/CommonBuildParameters.cmake` lines 118-121
- **SQLite3** - Secure local storage for wallet data, configuration
  - Client: SQLiteModernCpp (`src/local_secure_storage/`)
  - Used for secure key-value storage (`SecureStorage.hpp`, `ISecureStorage.hpp`)
  - Config: `build/CommonBuildParameters.cmake` lines 224-233
- **In-Memory Storage** - `src/storage/in_memory/` for testing and transient data

**File Storage:**
- IPFS (content-addressed, decentralized) - primary distributed storage
- Local filesystem for configuration and secure storage

**Caching:**
- In-memory trie structures for blockchain state

## Authentication & Identity

**Auth Provider:**
- Custom cryptographic identity based on Ed25519 and secp256k1 keypairs
- Deterministic key derivation: `GeniusKDF/` (Genesis Key Derivation Function submodule)
- Wallet-based account system with mnemonic seed phrases
- Signed messages (protobuf): `SGAccountComm.proto` - NonceRequest/Response, BlockRequest/Response all signed
- Node-to-node: libp2p identity via cryptographic keypairs
- Ethereum bridge: secp256k1 signatures (`evmrelay/src/eth/secp256k1_utility.cpp`)

## Monitoring & Observability

**Error Tracking:**
- `outcome::result<T>` pattern for error propagation throughout the codebase
- `boost::outcome` library provides monadic error handling
- Custom error enums per module (e.g., `PriceError`, `block_tree_error`)

**Logs:**
- Multiple logging frameworks: spdlog v1.4.2, soralog, Boost.Log
- `src/base/logger.hpp` / `src/base/logger.cpp` - Centralized logger creation (`base::createLogger()`)
- `m_logger` member pattern used across modules
- Debug logging enabled via `SGNS_DEBUGLOGS` define (non-Release builds)
- Log levels: debug, warn, error

## CI/CD & Deployment

**Hosting:**
- Multi-platform native libraries (macOS, Linux, Windows, Android, iOS)
- No web-hosted service detected; library is embedded in applications

**CI Pipeline:**
- GitHub Actions (`.github/` directory present)
- Android built on Linux CI host
- Thirdparty dependencies auto-downloaded from GitHub Releases if not locally present

## Environment Configuration

**Required env vars:**
- `THIRDPARTY_DIR` - Path to thirdparty build directory (auto-detected from `../thirdparty/`)
- `ANDROID_NDK_HOME` / `ANDROID_NDK_ROOT` / `CMAKE_ANDROID_NDK` - Android NDK (Android only)
- `VULKAN_SDK` - Vulkan SDK path (auto-detected from thirdparty)
- Ethereum RPC endpoint URLs (injected via environment in `evmrelay/src/eth/rpc_manager.cpp`)

**Secrets location:**
- Environment variables (Ethereum RPC API keys, etc.)
- `evmrelay/examples/.env` file (not read for security)
- `test/src/local_secure_storage/json_storage/secure_storage.json` - Test secure storage data

## Webhooks & Callbacks

**Incoming:**
- gRPC API endpoints for node management (account queries, block retrieval, transaction submission)
- IPFS PubSub message handlers for processing task distribution
- Discv4/Discv5 node discovery responses

**Outgoing:**
- Ethereum JSON-RPC calls to EVM nodes (`eth_getLogs`, `eth_getTransactionReceipt`, etc.)
- Ethereum devp2p handshake and RLPx protocol messages
- CoinGecko API price queries
- IPFS content retrieval and GraphSync data synchronization
- IPFS PubSub message publishing (processing results)
- libp2p peer connections and protocol messages

---

*Integration audit: 2026-05-25*
