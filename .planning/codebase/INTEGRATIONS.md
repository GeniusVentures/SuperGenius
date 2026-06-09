# External Integrations

**Analysis Date:** 2026-05-27

## APIs & External Services

**Cryptocurrency Price Data:**
- **CoinGecko API** - Real-time and historical cryptocurrency price retrieval
  - Endpoints used in `src/coinprices/coinprices.cpp`:
    - `https://api.coingecko.com/api/v3/simple/price` — Current prices for token IDs in USD
    - `https://api.coingecko.com/api/v3/coins/{id}/history` — Historical price at a specific date
    - `https://api.coingecko.com/api/v3/coins/{id}/market_chart/range` — Price range with timestamps
  - SDK/Client: Custom HTTP client using Boost.Asio + OpenSSL (`src/coinprices/coinprices.cpp`)
  - Data format: JSON parsed with RapidJSON
  - Rate limiting: Enforced via 1.1s delay between requests for historical data; free-tier 365-day lookback
  - Error handling via `boost::outcome` result type with `PriceError` enum (EmptyInput, NetworkError, JsonParseError, NoDataFound, RateLimitExceeded, DateTooOld)
  - Uses `FileManager` / `AsyncIOManager` for async HTTP requests with chunked transfer decoding

**EVM P2P Networks:**
- **Ethereum, Polygon, BSC, Base** — Direct P2P blockchain monitoring (no RPC required)
  - Implemented in: `evmrelay/` submodule
  - Protocol components:
    - **discv4** — Node discovery (UDP-based, `evmrelay/src/discv4/`, `evmrelay/include/discovery/`)
    - **RLPx** — Secure TCP transport with ECIES handshake, AES/HMAC frame ciphers (`evmrelay/src/rlpx/`, `evmrelay/src/rlp/`)
    - **ETH subprotocol** — Block headers, transactions, log filtering (`evmrelay/src/eth/`)
  - Supported chains (from `evmrelay/README.md`):
    - Ethereum Mainnet, Polygon, BSC, Base (mainnets and testnets)
    - GNUS.AI contract monitoring for Transfer events
  - Test CLI: `eth_watch` connects directly to EVM P2P networks to watch for contract events

## Data Storage

**Databases:**
- **RocksDB** — Primary persistent key-value storage engine
  - Location: `src/storage/rocksdb/`
  - Configured in `build/CommonBuildParameters.cmake` lines 119-120
  - Used for: blockchain state, blocks, trie storage
- **SQLite3** — Lightweight embedded relational database
  - Location: configured in `build/CommonBuildParameters.cmake` lines 224-233
  - Wrapper: SQLiteModernCpp
  - Used for: local metadata, configuration storage
- **In-Memory** — In-memory storage backend for testing/non-persistent use
  - Location: `src/storage/in_memory/`

**File Storage:**
- **IPFS** — Content-addressed decentralized file storage
  - Client libraries: `ipfs-lite-cpp`, `ipfs-bitswap-cpp` (via thirdparty)
  - Config in `build/CommonBuildParameters.cmake` lines 262-271
  - Examples: `example/ipfs_client/`, `example/ipfs_client2/`, `example/ipfs_dht.cpp`
  - Used for: CRDT data synchronization, processing task data

**Caching:**
- Custom trie-based caching via `src/storage/trie/` (polkadot-trie and supergenius-trie implementations)
- In-memory key-value maps throughout consensus and processing subsystems

## Peer-to-Peer Networking

**libp2p Stack:**
- **libp2p** — Full P2P networking layer
  - `src/storage/rocksdb/` and `src/crdt/globaldb/` for CRDT sync
  - Components used: basic_host, default_network, peer_repository, inmem_address_repository, inmem_key_repository, inmem_protocol_repository, kademlia (DHT), identify, ping
  - Example: `example/ipfs_pubsub/CMakeLists.txt` shows full linkage

**IPFS PubSub (GossipSub):**
- **ipfs_pubsub::GossipPubSub** — Decentralized publish-subscribe messaging
  - Used by: ConsensusManager (`src/blockchain/Consensus.hpp` line 31), ProcessingSubTaskQueueChannel (`src/processing/processing_subtask_queue_channel_pubsub.hpp`)
  - Topics: consensus channels (`consensus-channel-{topic}`), processing task queues
  - Message types: consensus proposals, votes, vote bundles, certificates, processing task requests

**IPFS Bitswap:**
- **ipfs-bitswap-cpp** — Content-addressed data exchange protocol

**GraphSync:**
- `src/crdt/graphsync_dagsyncer.hpp` — GraphSync-based DAG synchronization for CRDT data

**NAT Traversal:**
- **gnus_upnp** — UPnP for NAT traversal (`build/CommonBuildParameters.cmake` line 393)
  - Linked into `genius_node` target (`src/account/CMakeLists.txt` line 85)

## Authentication & Identity

**Auth Provider:**
- **Self-sovereign** — No third-party identity provider
  - **Ed25519** key pairs for node and account identity (`ed25519` dependency)
  - **secp256k1** for Ethereum-compatible signatures (`libsecp256k1` dependency)
  - Consensus validator identity managed via `ValidatorRegistry` (`src/blockchain/ValidatorRegistry.hpp`)
  - Signing callbacks passed as `std::function` to `ConsensusManager` (`src/blockchain/Consensus.hpp` line 62)
- **TrustWalletCore** — Multi-coin wallet for key management
  - `src/account/CMakeLists.txt` links: TrustWalletCore, TrezorCrypto, wallet_core_rs
  - Key generation: ElGamal, Ethereum key generators from `ProofSystem`
- **Local Secure Storage** — Platform-native credential storage
  - Location: `src/local_secure_storage/`
  - Platform implementations:
    - `impl/json/JSONSecureStorage.cpp` — JSON file-based (cross-platform fallback)
    - `impl/Apple.cpp` — iOS/macOS Keychain
    - `impl/Android.cpp` — Android Keystore (with Java helper `impl/KeyStoreHelper.java`)
    - `impl/Windows.cpp` — Windows DPAPI
    - `impl/Linux.cpp` — Linux secret service / file-based
  - ISecureStorage interface: `src/local_secure_storage/ISecureStorage.hpp`

## Monitoring & Observability

**Logging:**
- **spdlog** (v1.4.2) + **soralog** — Structured logging framework
  - Configured in `build/CommonBuildParameters.cmake` lines 133-140
  - Custom logger wrapper: `src/base/logger.hpp` / `src/base/logger.cpp`
  - Logger creation: `sgns::base::createLogger("ComponentName")`
  - Compile-time toggle: `SGNS_DEBUGLOGS` preprocessor definition
- **Boost.Log** — Alternative logging via Boost components

**Error Tracking:**
- Not detected — No external error tracking service (Sentry, Bugsnag, etc.)
- Internal error handling via `boost::outcome` result types (`outcome::result<T>`)
- Custom outcome error categories defined per module (e.g., `CoinGeckoPriceRetriever::PriceError`)

**Metrics:**
- Not detected — No Prometheus, StatsD, or custom metrics endpoints

## API Services

**gRPC / OpenAPI:**
- **gRPCForSuperGenius** submodule — Protocol definitions and code generation
  - REST API defined as OpenAPI v2 specs:
    - `gRPCForSuperGenius/openapi_yaml/SuperGenius-OpenAPI.yaml` (2,459 lines) — Core blockchain API
    - `gRPCForSuperGenius/openapi_yaml/SGProcessing-OpenAPI.yaml` (224 lines) — Processing/rooms API
  - OpenAPI converted to protobuf via `gnostic` (Go-based tool)
  - Protobuf compiled to C++ via `protoc` with `proto-include/google/` path
  - Generated C++ files: `SuperGenius_OpenAPI.pb.h`, `SuperGenius_OpenAPI.pb.cc`

**Core Blockchain API Endpoints** (from `SuperGenius-OpenAPI.yaml`):
- Account management: `/account_balance`, `/account_block_count`, `/account_create`, `/account_get`, `/account_history`, `/account_info`, `/account_key`, `/account_list`, `/account_move`, `/account_remove`, `/account_representative`, `/account_representative_set`, `/account_weight`, `/accounts_balances`, `/accounts_create`
- Wallet operations: wallet creation, account management within wallets
- Transaction operations: send, receive, pending transactions
- Block operations: block retrieval, block count
- Network operations: peers, representatives, status

**Processing API Endpoints** (from `SGProcessing-OpenAPI.yaml`):
- Room management: `/room_list`, `/room_get`, `/room_join`, `/room_leave`
- Messaging: `/broadcast_message`, `/send_message`
- Peer management: `/has_peer`

**Internal Protobuf Messages:**
- `SGTransaction.proto` — Transaction serialization (`src/account/proto/`)
- `SGAccountComm.proto` — Account communication messages
- `SGBlockchain.proto`, `SGBlocks.proto` — Blockchain block types (`src/blockchain/impl/proto/`)
- `Consensus.proto` — Consensus messages: Proposal, Vote, VoteBundle, Certificate, Subject
- `SGProcessing.proto` — Processing task definitions (`src/processing/proto/`)
- `SGProof.proto` — Zero-knowledge proof messages (`src/proof/proto/`)
- `delta.proto`, `heads.proto`, `bcast.proto` — CRDT protocols (`src/crdt/proto/`)
- `broadcast.proto` — Global DB broadcast messages (`src/crdt/globaldb/proto/`)

## CI/CD & Deployment

**Hosting:**
- Cross-platform native libraries — no web hosting
- Libraries installed via CMake `install()` targets to `CMAKE_INSTALL_PREFIX`

**CI Pipeline:**
- **GitHub Actions** — Build and release automation
  - `.github/workflows/cmake.yml` — Release Build CI (623 lines)
    - Triggers: push/PR to `develop`/`main`, manual `workflow_dispatch`
    - Platforms: Android (arm64-v8a, armeabi-v7a, x86_64), iOS, macOS (universal), Windows (x64, ARM64), Linux (x86_64, ARM64)
    - Configurations: Debug and Release
    - Build matrix with conditional platform selection
    - Uploads build artifacts with version/hash naming
  - `.github/workflows/build-release-tags.yml` — Release tag builds
  - `SGProcessingManager/.github/workflows/generate-headers.yml` — Auto-generate C++ headers from schema
  - `evmrelay/.github/workflows/cmake-multi-platform.yml` — EVM relay CI
  - `evmrelay/.github/workflows/sanitizers.yml`, `valgrind.yml`, `fuzz.yml`, `benchmarks.yml`

**Dependency Resolution:**
- Pre-built thirdparty libraries auto-downloaded from GitHub Releases (`GeniusVentures/thirdparty`) when not found locally
- zkLLVM auto-downloaded from GitHub Releases (`GeniusVentures/zkLLVM`) when not found locally
- Download logic in `build/CommonCompilerOptions.cmake` lines 76-125 (zkLLVM) and 146-188 (thirdparty)

## Environment Configuration

**Required env vars (build-time only, used by CMake):**
- `ANDROID_NDK_HOME` / `ANDROID_NDK_ROOT` — Android NDK path for cross-compilation
- `VULKAN_SDK` — Vulkan SDK path (falls back to thirdparty build)
- `THIRDPARTY_DIR` — Custom path to thirdparty dependencies
- `ZKLLVM_BUILD_DIR` — Custom path to zkLLVM build

**Secrets location:**
- `.env` file present (in `evmrelay/`) — contains Ethereum test wallet private keys (git-ignored)
- `test/src/local_secure_storage/json_storage/secure_storage.json` — Test storage fixture
- `test/src/transaction_sync/node*_0_2_0/secure_storage.json` — Test node fixtures
- No production secrets found in repository

## Webhooks & Callbacks

**Incoming:**
- Consensus pubsub message callback: `ConsensusManager::OnConsensusMessage()` (`src/blockchain/Consensus.hpp` line 631)
- Processing channel message callback: `ProcessingSubTaskQueueChannelPubSub::OnProcessingChannelMessage()` (`src/processing/processing_subtask_queue_channel_pubsub.hpp` line 76)
- CRDT data callback: `ConsensusManager::CertificateReceived()` (`src/blockchain/Consensus.hpp` line 597)
- `CRDTCallbackManager` for custom CRDT data filters (`src/crdt/crdt_callback_manager.hpp`)

**Outgoing:**
- CoinGecko API requests via `FileManager::LoadASync()` — async HTTP with callback
- Consensus messages broadcast via `ConsensusManager::Publish()` → `ipfs_pubsub::GossipPubSub`
- Processing task queues published via `ProcessingSubTaskQueueChannelPubSub::PublishQueue()`
- EVM P2P messages via `evmrelay` RLPx sessions

## External Code Generation Tools

**gnostic** (Go):
- Converts OpenAPI YAML specs to Protocol Buffer definitions
- Located: `gRPCForSuperGenius/` project
- Command: `gnostic --grpc-out=grpc-build openapi_yaml/SuperGenius-OpenAPI.yaml`

**QuickType** (Node.js):
- Generates C++ headers from JSON Schema
- Located: `SGProcessingManager/` project
- Input: `gnus-processing-schema.json`
- Output: `generated/SGNSProcMain.hpp` and related headers
- Command: `quicktype --src-lang schema --lang cpp --boost ... gnus-processing-schema.json`

---

*Integration audit: 2026-05-27*
