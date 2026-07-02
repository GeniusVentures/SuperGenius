<!-- GSD:project-start source:PROJECT.md -->
## Project

**SuperGenius**

SuperGenius is a C++17 blockchain/crypto platform providing an account system (UTXO + DAG), consensus, a processing grid for distributed compute tasks, an EVM bridge, and a JSON-RPC + WebSocket API. It targets native node operators (full/light/archive) and ships cross-platform keystore support (Android NDK / iOS). The primary entry point and orchestration facade is `GeniusNode` in `src/account/`.

This milestone is an **interface refactor of `GeniusNode`** — not new product capability. It cleans up the node construction API and moves runtime knobs into configuration files where they belong.

**Core Value:** **Constructing a `GeniusNode` must be a single, self-documenting call driven by config files** — no more overloaded factory methods carrying boolean network/role flags that are really config concerns. If this refactor lands clean and all 18 call sites compile and tests pass, the milestone succeeds.

### Constraints

- **Tech stack**: C++17, CMake, RapidJSON, Boost, libp2p, git submodules — no new dependencies this milestone (use existing `std::variant` + RapidJSON).
- **Compatibility**: deployed nodes have `network_config.json` / `sgns_config.json` **without** the new keys — they must keep working via defaults; no hard-fail on missing keys.
- **Non-functional**: no behavior change for existing configurations — pure interface/config-location refactor. Tests stay green.
- **Scope boundary**: the `NodeType` enum stops at the `GeniusNode` boundary this milestone (derived bool passed downstream).
<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->
## Technology Stack

## Languages
- C++ (C++17) - Entire core project: blockchain, consensus, processing, networking, crypto, storage
- Python 3 - Test data generation scripts in `test/src/processing_datatypes/` (create models, convert NIfTI)
- Java - Android keystore support in `src/local_secure_storage/impl/KeyStoreHelper.java`
- JavaScript - Static documentation search in `docs/hdoc/`
- Go - Used indirectly by `gRPCForSuperGenius` for gnostic/OpenAPI-to-protobuf toolchain
## Runtime
- C++17 compiler required (GCC, Clang, or MSVC)
- No managed runtime — compiled to native binaries/libraries
- Cross-compilation via CMake toolchains for Android (NDK r27b) and iOS
- Git submodules (preferred dependency model — `thirdparty` repo at `../thirdparty/`)
- CMake `find_package` with pre-built thirdparty libraries
- Third-party dependency versions managed in `build/CommonBuildParameters.cmake`
- npm (for QuickType code generation in `SGProcessingManager`)
## Frameworks
- Boost 1.85.0 - Foundational framework (Asio, JSON, Outcome, DI, Log, ProgramOptions, Filesystem, etc.)
- Protocol Buffers (protobuf) - Message serialization across all subsystems
- libp2p - Peer-to-peer networking stack (Kademlia DHT, Identify, Ping, GossipSub, basic host)
- Boost.Asio - Async I/O for HTTP and network communication (`src/coinprices/coinprices.cpp`)
- c-ares - Async DNS resolution (`build/CommonBuildParameters.cmake` lines 236-248)
- MNN (Mobile Neural Network) - On-device ML inference engine
- Vulkan (via MoltenVK on macOS) - GPU compute for processing tasks
- zkLLVM - Zero-knowledge proof compiler infrastructure
- Google Test (GTest) + Google Mock (GMock) - Unit testing framework
- CMake 3.22+ - Build system generator
- Ninja - Build executor (Linux/macOS)
- Visual Studio 17 2022 - Build executor (Windows)
- clang-format / clang-tidy - Code formatting and static analysis (`.clang-format`, `.clang-tidy`, `.clangd` present)
## Key Dependencies
| Package | Version | Purpose |
|---------|---------|---------|
| Boost | 1.85.0 | Foundation: Asio, DI, JSON, Outcome, Log, Filesystem, Thread, Coroutine, etc. |
| Protocol Buffers | (via thirdparty) | Binary serialization for all internal and gRPC messages |
| OpenSSL | (via thirdparty) | TLS, cryptographic primitives |
| RocksDB | (via thirdparty) | Persistent key-value storage engine |
| libp2p | (via thirdparty) | P2P networking: host, Kademlia DHT, GossipSub, Identify |
| zkLLVM | (auto-downloaded) | ZK proof compiler infrastructure |
| MNN | (via thirdparty) | Mobile neural network inference engine |
| fmt | (via thirdparty) | Modern C++ string formatting |
| spdlog | 1.4.2 (via thirdparty) | Fast C++ logging library |
| soralog | (via thirdparty) | Structured logging on top of spdlog |
| RapidJSON | (via thirdparty) | JSON parsing (CoinGecko API responses) |
| nlohmann/json | (via thirdparty) | Modern C++ JSON library |
| yaml-cpp | (via thirdparty) | YAML parsing |
| GSL (Microsoft.GSL) | (via thirdparty) | Guidelines Support Library |
| Package | Version | Purpose |
|---------|---------|---------|
| SQLite3 | (via thirdparty) | Lightweight embedded database |
| SQLiteModernCpp | (via thirdparty) | Modern C++ wrapper for SQLite |
| Snappy | (via thirdparty) | Compression library (used by RocksDB) |
| zlib | (via thirdparty) | Compression library |
| xxHash | (via thirdparty) | Fast non-cryptographic hashing |
| tsl_hat_trie | (via thirdparty) | Hat-trie data structure |
| libssh2 | (via thirdparty) | SSH2 protocol library |
| stb | (via thirdparty) | Single-file public domain libraries (image loading) |
| Package | Version | Purpose |
|---------|---------|---------|
| ipfs-lite-cpp | (via thirdparty) | Lightweight IPFS client (content-addressed storage) |
| ipfs-pubsub | (via thirdparty) | IPFS pubsub (GossipSub messaging) |
| ipfs-bitswap-cpp | (via thirdparty) | IPFS Bitswap protocol (data exchange) |
| Package | Version | Purpose |
|---------|---------|---------|
| ed25519 | (via thirdparty) | Ed25519 digital signatures |
| secp256k1 | (via thirdparty) | ECDSA on secp256k1 curve (Bitcoin/Ethereum compatible) |
| TrezorCrypto | (via thirdparty) | Hardware-wallet compatible crypto primitives |
| wallet_core_rs | (via thirdparty) | Rust-based wallet core bindings |
| TrustWalletCore | (via thirdparty) | Cross-platform multi-coin wallet library |
- `gRPCForSuperGenius` - gRPC and OpenAPI protocol definitions
- `GeniusKDF` - Key derivation function module
- `ProofSystem` - Zero-knowledge proof circuit system
- `SGProcessingManager` - Processing pipeline schema and C++ headers
- `evmrelay` - EVM P2P relay (discv4, RLPx, ETH protocol)
- `docs` - Documentation submodule (`sg-docs`)
## Configuration
- Build type: `CMAKE_BUILD_TYPE` (Debug, Release, RelWithDebInfo)
- Testing: `-DTESTING=ON|OFF` (maps to `BUILD_TESTING`)
- Network: `SGNS_NETWORK` — when not "release", `DEV_NET` compile definition added (`cmake/version.cmake`)
- Proofs: `BUILD_WITH_PROOFS` compile definition (`src/account/CMakeLists.txt`)
- Debug logs: `SGNS_DEBUGLOGS` and `SGNS_DEBUG` compile definitions
- Custom zkLLVM and thirdparty paths via `ZKLLVM_BUILD_DIR`, `THIRDPARTY_DIR`, `THIRDPARTY_BUILD_DIR`
- Cross-compilation: `ANDROID_ABI`, `ABI_SUBFOLDER_NAME`, `IOS` variable
- `cmake/version.cmake` — Project version (3.7.0), git-derived version metadata
- `cmake/functions.cmake` — `addtest()`, `add_proto_library()`, `compile_proto_to_cpp()`
- `cmake/install.cmake` — Install rules
- `cmake/config.cmake.in` — CMake package config template
- `build/CommonCompilerOptions.cmake` — Compiler flags, standards, version info
- `build/CommonBuildParameters.cmake` — All third-party dependency resolution
- `build/CompilationFlags.cmake` — Platform-specific compilation flags
- `.clang-format`, `.clang-tidy`, `.clangd` at repo root
- `.clang-tidy` disabled for generated protobuf code via `disable_clang_tidy()` in `cmake/functions.cmake`
## Platform Requirements
- macOS, Linux, or Windows host
- CMake 3.22+, Ninja or Visual Studio 2022
- Git with submodule support: `git submodule update --init --recursive`
- Python 3 (for test data generation scripts)
- Thirdparty dependencies pre-built or auto-downloaded from GitHub Releases
- Linux (x86_64, aarch64/ARM)
- Windows (x86_64 — MSVC, Windows 10+)
- macOS (universal: x86_64 + ARM64, deployment target 13.0)
- iOS (cross-compiled from macOS)
- Android (armeabi-v7a, arm64-v8a, x86_64 — NDK r27b)
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

## Language Standard
- **C++17** (`.clang-format`: `Standard: c++17`)
- Build system: CMake
- Compilation database: `.build/` (referenced from `.clangd`)
## Naming Patterns
### Enforced by `.clang-tidy` (`readability-identifier-naming`)
| Category | Case | Example |
|----------|------|---------|
| Default (variables, parameters, members) | `lower_case` | `base_path`, `token_id` |
| Classes | `CamelCase` | `Buffer`, `GeniusAccount`, `UTXOManager` |
| Functions | `CamelCase` | `createLogger()`, `GetBalance()`, `PutUTXO()` |
| Enums | `CamelCase` | `UnhexError`, `UTXOState` |
| Enum constants | `UPPER_CASE` | `NOT_ENOUGH_INPUT`, `UTXO_READY` |
| Constexpr variables | `UPPER_CASE` | `SIGNATURE_EXP_SIZE`, `DB_PREFIX` |
| Type aliases / typedefs | `CamelCase` | `using Logger = ...`, `using SignFunc = ...` |
### Observed in Source Code
- Header files: `.hpp` extension, names vary — both `CamelCase` (`GeniusAccount.hpp`) and `snake_case` (`buffer.hpp`, `outcome_throw.hpp`)
- Source files: `.cpp` extension, match their header name
- Libraries/directories: `snake_case` (`src/account/`, `src/local_secure_storage/`)
- snake_case with trailing underscore: `data_` (in `Buffer`, `src/base/buffer.hpp`), `utxo_manager_`, `storage_`, `is_full_node_`, `logger_`
- This is consistent across the codebase
- Both `snake_case` and `camelCase` appear, but `snake_case` is the majority pattern (e.g., `base_path`, `token_id`, `full_node` in `GeniusAccount.hpp`)
- Some API-level code uses `a`-prefixed camelCase: `aKey`, `aDelta`, `aID` (in `src/crdt/crdt_set.hpp`)
- Root namespace: `sgns`
- Sub-namespaces: `sgns::base`, `sgns::crypto`, `sgns::scale`, `sgns::storage`, `sgns::crdt`
- Test utility namespace: `test`
- Alias `namespace fs = boost::filesystem;` used in test utilities
## Code Style
### Formatting (`.clang-format`)
- **Base style:** Microsoft with heavy customization
- **Column limit:** 120 characters
- **Indentation:** 4 spaces
- **Braces:** `InsertBraces: true` — always use braces, even for single-line blocks
- **Brace wrapping:** After case labels (`AfterCaseLabel: true`), before lambda body (`BeforeLambdaBody: true`)
- **Namespaces:** Indented (`NamespaceIndentation: All`)
- **Constructor initializers:** Break after colon (`BreakConstructorInitializers: AfterColon`), pack on next line (`PackConstructorInitializers: NextLine`)
- **Argument/parameter packing:** Disabled (`BinPackArguments: false`, `BinPackParameters: false`) — each argument on its own line when wrapping
- **Access modifiers:** Offset -4 (`AccessModifierOffset: -4`) — `public:` at column 4 instead of 8
- **Includes:** Not sorted (`SortIncludes: Never`) — order is manual
- **Trailing commas:** Inserted when wrapping (`InsertTrailingCommas: Wrapped`)
- **Short functions/blocks:** Only empty ones on single line (`AllowShortFunctionsOnASingleLine: Empty`, `AllowShortBlocksOnASingleLine: Empty`)
- **Spaces in parentheses:** Custom — spaces inside conditionals and other parens
- **Template declarations:** Always break (`AlwaysBreakTemplateDeclarations: true`)
- **Case labels:** Indented (`IndentCaseLabels: true`), case blocks not indented (`IndentCaseBlocks: false`)
### Linting (`.clang-tidy`)
- **Checks enabled:** `boost-*`, `bugprone-*`, `cert-*`, `concurrency-*`, `cppcoreguidelines-*`, `misc-*`, `modernize-*`, `performance-*`, `portability-*`, `readability-*`, plus selective Google/HICPP rules
- **Notable disabled checks:** `readability-magic-numbers`, `readability-identifier-length`, `modernize-use-trailing-return-type`, `modernize-use-default-member-init`, `cppcoreguidelines-avoid-magic-numbers`, `bugprone-easily-swappable-parameters`
- **Print replacement:** `fmt::print` / `fmt::println` (via `modernize-use-std-print`)
- **Format style:** Uses file (reads `.clang-format`)
- **Clang-tidy disabled on:** Generated protobuf code (via `disable_clang_tidy()` in `cmake/functions.cmake`), test targets (same function applied in `addtest()`)
### #pragma once vs Header Guards
- **`#pragma once`** — newer code written since ~2024 (e.g., `src/account/GeniusAccount.hpp`, `src/account/UTXOManager.hpp`, `src/blockchain/Consensus.hpp`)
- **`#ifndef SUPERGENIUS_..._HPP`** — older infrastructure code (e.g., `src/base/buffer.hpp`, `src/base/logger.hpp`, `src/scale/*`, `src/storage/*`, `src/primitives/*`)
- No file uses both; choose one based on the directory you're adding to
## Import Organization
### Include Order (Observed Pattern)
#include "base/buffer.hpp"
#include "base/hexutil.hpp"
#include "outcome/outcome.hpp"
#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
### Path Style
- Project includes use quotes: `#include "account/GeniusAccount.hpp"`
- System includes use angle brackets: `#include <gtest/gtest.h>`
- Include paths are relative to `src/` directory (which is on the include path)
## Error Handling
### Primary Pattern: `outcome::result<T>`
### Error Types
- Error codes use strongly-typed `enum class` with custom error categories
- Registered via `OUTCOME_HPP_DECLARE_ERROR_2(sgns::base, UnhexError)` macro (in `src/base/hexutil.hpp`)
- Example: `src/base/hexutil.hpp` defines `UnhexError` enum with `NOT_ENOUGH_INPUT`, `NON_HEX_INPUT`, etc.
### Throwing (Rare)
- Used only when calling code cannot propagate outcomes (e.g., static initializers, constructors)
- Implemented via `sgns::base::raise()` in `src/base/outcome_throw.hpp` — converts outcome errors to `boost::system_error` exceptions
- Use `EXPECT_OUTCOME_RAISE` macro in tests to verify these exceptions
### Return Value Conventions
- Functions that can fail return `outcome::result<T>` or `outcome::result<void>`
- Functions that cannot fail return plain types, often marked `noexcept`
- `[[nodiscard]]` used on functions where ignoring the return value is a bug
- `std::optional` used for "value may or may not be present" (not an error condition)
## Logging
### Framework
- **Library:** spdlog (via `src/base/logger.hpp`)
- **Type alias:** `using Logger = std::shared_ptr<spdlog::logger>;` (in `sgns::base`)
- **Creation:** `base::Logger logger_ = base::createLogger("TagName");`
### Pattern
- Logger pattern: `[YYYY-MM-DD HH:MM:SS][level][tag] message`
- Debug pattern: `[YYYY-MM-DD HH:MM:SS.µs][th:thread_id][level][tag] message`
- Loggers are created once and stored as class members
- Each module/class typically has its own named logger
## Comments and Documentation
### Doxygen
- **Enforced:** This codebase uses Doxygen-style documentation on all public APIs
- **File headers:** `@file`, `@brief`, `@date`, `@author` at top of header files
- **Functions:** `@brief`, `@param[in]`, `@param[out]`, `@return` for all public methods
- **Members:** Inline `///<` descriptions after member declarations
### Test Comments (Gherkin-style)
## Function Design
### Size Guidelines
- Most source files are under 500 lines
- Large files exist and are noted as tech debt:
- Prefer smaller, focused files and functions
### Parameter Style
- Const references for input: `const TokenID &token_id`, `const std::string &address`
- Values for sinks: `std::string address` (when moved), `std::vector<uint8_t> data`
- Output via return value, not out-parameters (consistent with `outcome::result<T>` pattern)
### Return Values
- Prefer `outcome::result<T>` for fallible operations
- Prefer `std::optional<T>` for optional returns
- Use `[[nodiscard]]` to enforce checking
## Module Design
### Header/Source Split
- All non-trivial classes follow `.hpp`/`.cpp` split
- Inline implementations allowed for simple accessors and templates
- Templates defined in `.hpp` or dedicated `_impl.hpp` files
### Exports
- Public API defined in headers under `src/<module>/` (e.g., `src/account/UTXOManager.hpp`)
- Implementation in matching `.cpp` files
- Internal implementation details go in `src/<module>/impl/` subdirectory
### Class Layout
## Platform-Specific Code
- Platform-specific implementations use `#if defined(ANDROID)` or similar preprocessor guards
- Platform files in `src/local_secure_storage/impl/`: `Linux.cpp`, `Windows.cpp`, `Apple.cpp`, `Android.cpp`
- Build configurations per platform in `build/{iOS,OSX,Windows,Linux,Android}/CMakeLists.txt`
## Code Patterns
### Factory Construction
### Self-Registration Pattern
### Singleton Component Factory
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

## System Overview
```text
```
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
- **Interface/Impl Separation**: Every subsystem has abstract interfaces in the main directory and concrete implementations in an `impl/` subdirectory. Storage layer uses `face/` for interface definitions.
- **Static `New()` Factory**: Classes have private constructors and expose `static std::shared_ptr<ClassName> New(...)` for construction, enforcing shared ownership.
- **`outcome::result<T>` Error Propagation**: All fallible functions return `outcome::result<T>`. Use `OUTCOME_TRY` to propagate. Error types are `enum class` values.
- **`IComponent` / `CComponentFactory` DI**: All injectable services inherit `IComponent` (`virtual std::string GetName() = 0`). `CComponentFactory` is a singleton registry keyed by type+optional variant string.
- **CRDT as State Backbone**: All persistent state (accounts, blockchain, processing queues) flows through `CrdtDatastore` / `GlobalDB`, replicated over IPFS pubsub and synced via Graphsync/MerkleDAG.
- **Protobuf Serialization**: All network messages and persistent data use Protocol Buffers (`.proto` files co-located with their subsystem in `proto/` subdirectories).
## Layers
- Purpose: Node lifecycle management, DI wiring of all subsystems
- Location: `src/account/GeniusNode.cpp` (1953 lines), example entry points in `example/`
- Contains: Node state machine FSM (`NodeState` enum), inline constructor wiring, `InitNetwork()`/`InitLoggers()`
- Depends on: All `src/` subsystems
- Purpose: JSON-RPC over HTTP and WebSocket for external clients
- Location: `src/api/transport/`, `gRPCForSuperGenius/`
- Contains: `HttpListener`, `WsListener`, `WsSession`, `RpcThreadPool`, OpenAPI specs
- Depends on: Account, Blockchain, Processing subsystems
- Used by: External clients, SDKs
- Purpose: UTXO-based token ledger with block-lattice DAG structure
- Location: `src/account/`
- Contains: `GeniusAccount`, `GeniusNode`, `TransferTransaction`, `MintTransaction`, `ProcessingTransaction`, `EscrowTransaction`, `UTXOManager`, `TransactionManager`, `AccountMessenger`, `MigrationManager`
- Depends on: CRDT/GlobalDB, Storage, Cryptography, Proof System, Secure Storage
- Used by: API layer, Processing pipeline, EVM Bridge
- Purpose: Genesis/account-creation bootstrap, weighted consensus with validator voting, block storage
- Location: `src/blockchain/`
- Contains: `Blockchain` (bootstrap controller), `ConsensusManager` (proposals/votes/certificates), `ValidatorRegistry`, `BlockTree`, `BlockStorage`, `BlockHeaderRepository`
- Depends on: CRDT/GlobalDB, PubSub, Cryptography, Account
- Used by: Account layer, Processing pipeline
- Purpose: Distributed ML/AI job scheduling with subtask queues and result validation
- Location: `src/processing/`
- Contains: `ProcessingService`, `ProcessingNode`, `ProcessingEngine`, `ProcessingCore` (interface), `ProcessingSubTaskQueueManager`, `SubTaskQueueAccessor`, `ProcessingValidationCore`
- Depends on: CRDT/GlobalDB, PubSub, Account (escrow), SGProcessingManager
- Used by: Node entrypoint, Validator nodes
- Purpose: Causally consistent distributed key-value store replicated over IPFS
- Location: `src/crdt/`, `src/crdt/globaldb/`
- Contains: `CrdtDatastore` (add-wins OR-Set, DAG workers), `GlobalDB` (high-level API), `CrdtSet`, `CrdtHeads`, `PubSubBroadcaster`, `GraphsyncDAGSyncer`, `CRDTCallbackManager`, `KeyPairFileStorage`
- Depends on: Storage (RocksDB), IPFS pubsub, Graphsync, Protobuf
- Used by: Blockchain, Account, Processing, Proof subsystems
- Purpose: Persistent and in-memory key-value storage with Merkle Patricia Trie
- Location: `src/storage/`
- Contains: Face interfaces (`src/storage/face/`: `Readable`, `Writeable`, `GenericStorage`, `Batchable`), RocksDB backend (`src/storage/rocksdb/`), In-Memory backend (`src/storage/in_memory/`), Trie/MPT (`src/storage/trie/`), Changes Trie (`src/storage/changes_trie/`)
- Depends on: Base (Buffer), Thirdparty (RocksDB library)
- Used by: CRDT, Blockchain, Account
- Purpose: zkSNARK proof generation/verification using nil::crypto3 library
- Location: `src/proof/`, `ProofSystem/`
- Contains: `IBasicProof`, `GeniusProver`, `GeniusAssigner`, `TransferProof`, `RecursiveTransferProof`, `ProcessingProof`, circuits (`TransactionVerifierCircuit`, `RecursiveTransactionCircuit`)
- Depends on: nil::crypto3, ProofSystem submodule
- Used by: Account (transfer proofs), Processing (computation proofs)
- Purpose: Cryptographic primitives, utilities, DI container, event bus
- Location: `src/crypto/`, `src/base/`, `src/singleton/`, `src/subscription/`, `src/primitives/`, `src/scale/`
- Contains: ED25519/SR25519/secp256k1 providers, VRF, hashing (blake2, keccak, sha2, twox), BIP39, Buffer/Blob/Logger, CComponentFactory, SubscriptionEngine, core data types
- Depends on: Thirdparty (libsodium, spdlog)
- Used by: All layers
## Data Flow
### Primary Request Path (Token Transfer)
### Primary Request Path (Distributed ML Job)
### EVM Minting Flow
- All shared mutable state lives in `CrdtDatastore` / `GlobalDB` (CRDT-replicated over IPFS)
- RocksDB is the local persistence backend for CRDT data, block tree, and trie nodes
- In-memory storage used for ephemeral/temporary state and unit tests
- `AppStateManager` FSM manages global application lifecycle (Init → Prepare → ReadyToStart → Starting → Works → ShuttingDown → ReadyToStop)
## Key Abstractions
- Purpose: Decouple contract from implementation for testability and swappable backends
- Examples: `Blockchain.hpp` ↔ `blockchain/impl/Blockchain.cpp`, `CrdtDatastore` ↔ `crdt/impl/crdt_datastore.cpp`, `processing_core.hpp` ↔ `impl/processing_core_impl.cpp`
- Pattern: Header declares abstract/interface class; `impl/` subdirectory contains concrete `.cpp`/`.hpp` pair
- Purpose: Abstract storage API allowing RocksDB, in-memory, or other backends
- Examples: `src/storage/face/readable.hpp`, `writeable.hpp`, `generic_storage.hpp`, `batchable.hpp`
- Pattern: Template interfaces parameterized on key/value types; concrete backends in `rocksdb/`, `in_memory/`
- Purpose: Enforce `shared_ptr` ownership and hide construction complexity
- Examples: `Blockchain::New(global_db, account, pubsub, callback)` (`src/blockchain/Blockchain.hpp:78`), `CrdtDatastore::New(datastore, key, dagSyncer, broadcaster, options)` (`src/crdt/crdt_datastore.hpp:92`)
- Pattern: Private constructor, public static method returns `std::shared_ptr<T>`
- Purpose: Message schemas live with the subsystem that owns them
- Examples: `src/crdt/proto/delta.proto`, `src/blockchain/impl/proto/SGBlockchain.proto`, `src/account/proto/SGTransaction.proto`, `src/processing/proto/SGProcessing.proto`
## Entry Points
- Location: `example/node_test/NodeExample.cpp` (751 lines, primary example), plus 9 other examples in `example/`
- Triggers: Executable launched on host device (each example has its own `main()`)
- Responsibilities: Calls `GeniusNode::New()`, runs hand-rolled CLI loop. `GeniusNode` (`src/account/GeniusNode.cpp`) handles all subsystem wiring inline through its state machine FSM
- Location: `src/api/transport/`
- Triggers: Incoming HTTP/WS connections on configured bind address
- Responsibilities: JSON-RPC endpoint for external client SDKs (balance queries, block submission, processing room management)
- Location: `src/processing/processing_node.cpp`
- Triggers: PubSub message on grid channel (`ProcessingChannelRequest`, `NodeCreationIntent`)
- Responsibilities: Spawn `ProcessingEngine` instances, execute ML subtasks, publish results
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
### Static Singleton Registry for DI
### Overlapping Impl Patterns
## Error Handling
- Functions return `outcome::result<void>` for operations that can fail with no return value
- `OUTCOME_TRY(var, expr)` macro propagates errors up the call stack
- Error types are `enum class Error` nested inside the class (e.g., `Blockchain::Error`, `CrdtDatastore::Error`)
- Error enums registered with `OUTCOME_HPP_DECLARE_ERROR_2(namespace, EnumType)` at file scope
- Do not call `.message()` on plain enums — use `static_cast<int>()` or project-provided helpers
## Cross-Cutting Concerns
<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->
## Project Skills

No project skills found. Add skills to any of: `.claude/skills/`, `.agents/skills/`, `.cursor/skills/`, `.github/skills/`, or `.codex/skills/` with a `SKILL.md` index file.
<!-- GSD:skills-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
