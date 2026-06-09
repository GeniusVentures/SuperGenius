# Technology Stack

**Analysis Date:** 2026-05-27

## Languages

**Primary:**
- C++ (C++17) - Entire core project: blockchain, consensus, processing, networking, crypto, storage
  - Standard set via `CMAKE_CXX_STANDARD 17` with `CMAKE_CXX_STANDARD_REQUIRED ON` in `build/CommonCompilerOptions.cmake`

**Secondary:**
- Python 3 - Test data generation scripts in `test/src/processing_datatypes/` (create models, convert NIfTI)
- Java - Android keystore support in `src/local_secure_storage/impl/KeyStoreHelper.java`
- JavaScript - Static documentation search in `docs/hdoc/`
- Go - Used indirectly by `gRPCForSuperGenius` for gnostic/OpenAPI-to-protobuf toolchain

## Runtime

**Environment:**
- C++17 compiler required (GCC, Clang, or MSVC)
- No managed runtime — compiled to native binaries/libraries
- Cross-compilation via CMake toolchains for Android (NDK r27b) and iOS

**Package Manager:**
- Git submodules (preferred dependency model — `thirdparty` repo at `../thirdparty/`)
- CMake `find_package` with pre-built thirdparty libraries
- Third-party dependency versions managed in `build/CommonBuildParameters.cmake`
- npm (for QuickType code generation in `SGProcessingManager`)

## Frameworks

**Core:**
- Boost 1.85.0 - Foundational framework (Asio, JSON, Outcome, DI, Log, ProgramOptions, Filesystem, etc.)
  - Configured in `build/CommonBuildParameters.cmake` lines 1-221
  - Components used: container, date_time, filesystem, random, regex, system, thread, log, log_setup, program_options, unit_test_framework, json, context, coroutine, Boost.DI
- Protocol Buffers (protobuf) - Message serialization across all subsystems
  - Configured in `build/CommonBuildParameters.cmake` lines 41-87
  - `.proto` files located in: `src/account/proto/`, `src/blockchain/impl/proto/`, `src/crdt/proto/`, `src/crdt/globaldb/proto/`, `src/processing/proto/`, `src/proof/proto/`
  - gRPC proto includes in `gRPCForSuperGenius/proto-include/`

**Networking:**
- libp2p - Peer-to-peer networking stack (Kademlia DHT, Identify, Ping, GossipSub, basic host)
  - Configured in `build/CommonBuildParameters.cmake` lines 240-248
- Boost.Asio - Async I/O for HTTP and network communication (`src/coinprices/coinprices.cpp`)
- c-ares - Async DNS resolution (`build/CommonBuildParameters.cmake` lines 236-248)

**AI/ML:**
- MNN (Mobile Neural Network) - On-device ML inference engine
  - Configured in `build/CommonBuildParameters.cmake` lines 92-103
  - Used by the processing subsystem for ML task execution

**Graphics:**
- Vulkan (via MoltenVK on macOS) - GPU compute for processing tasks
  - Configured in `build/CommonBuildParameters.cmake` lines 251-259
  - macOS uses MoltenVK bridge (`build/OSX/CMakeLists.txt` lines 30-31)

**Zero-Knowledge Proofs:**
- zkLLVM - Zero-knowledge proof compiler infrastructure
  - Downloaded automatically from GitHub Releases (`build/CommonCompilerOptions.cmake` lines 57-126)
  - crypto3 libraries: algebra, block, blueprint, codec, math, multiprecision, pkpad, pubkey, random, zk
  - marshalling libraries: core, crypto3_algebra, crypto3_multiprecision, crypto3_zk

**Testing:**
- Google Test (GTest) + Google Mock (GMock) - Unit testing framework
  - Configured in `build/CommonBuildParameters.cmake` lines 13-38
  - Test registration via `addtest()` macro in `cmake/functions.cmake`
  - XML output for CI in `xunit/` directory
  - Run with: `ctest -C [Debug|Release]`

**Build/Dev:**
- CMake 3.22+ - Build system generator
  - Platform-specific entry points in `build/{Linux,Windows,OSX,Android,iOS}/CMakeLists.txt`
  - Shared configuration in `build/CommonBuildParameters.cmake` and `build/CommonCompilerOptions.cmake`
  - Helper functions in `cmake/functions.cmake`
- Ninja - Build executor (Linux/macOS)
- Visual Studio 17 2022 - Build executor (Windows)
- clang-format / clang-tidy - Code formatting and static analysis (`.clang-format`, `.clang-tidy`, `.clangd` present)

## Key Dependencies

**Critical (required for build):**

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

**Infrastructure:**

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

**IPFS Stack:**

| Package | Version | Purpose |
|---------|---------|---------|
| ipfs-lite-cpp | (via thirdparty) | Lightweight IPFS client (content-addressed storage) |
| ipfs-pubsub | (via thirdparty) | IPFS pubsub (GossipSub messaging) |
| ipfs-bitswap-cpp | (via thirdparty) | IPFS Bitswap protocol (data exchange) |

**Crypto Stack:**

| Package | Version | Purpose |
|---------|---------|---------|
| ed25519 | (via thirdparty) | Ed25519 digital signatures |
| secp256k1 | (via thirdparty) | ECDSA on secp256k1 curve (Bitcoin/Ethereum compatible) |
| TrezorCrypto | (via thirdparty) | Hardware-wallet compatible crypto primitives |
| wallet_core_rs | (via thirdparty) | Rust-based wallet core bindings |
| TrustWalletCore | (via thirdparty) | Cross-platform multi-coin wallet library |

**Submodules (Git):**
- `gRPCForSuperGenius` - gRPC and OpenAPI protocol definitions
- `GeniusKDF` - Key derivation function module
- `ProofSystem` - Zero-knowledge proof circuit system
- `SGProcessingManager` - Processing pipeline schema and C++ headers
- `evmrelay` - EVM P2P relay (discv4, RLPx, ETH protocol)
- `docs` - Documentation submodule (`sg-docs`)

## Configuration

**Environment:**
- Build type: `CMAKE_BUILD_TYPE` (Debug, Release, RelWithDebInfo)
- Testing: `-DTESTING=ON|OFF` (maps to `BUILD_TESTING`)
- Network: `SGNS_NETWORK` — when not "release", `DEV_NET` compile definition added (`cmake/version.cmake`)
- Proofs: `BUILD_WITH_PROOFS` compile definition (`src/account/CMakeLists.txt`)
- Debug logs: `SGNS_DEBUGLOGS` and `SGNS_DEBUG` compile definitions
- Custom zkLLVM and thirdparty paths via `ZKLLVM_BUILD_DIR`, `THIRDPARTY_DIR`, `THIRDPARTY_BUILD_DIR`
- Cross-compilation: `ANDROID_ABI`, `ABI_SUBFOLDER_NAME`, `IOS` variable

**Build:**
- `cmake/version.cmake` — Project version (3.7.0), git-derived version metadata
- `cmake/functions.cmake` — `addtest()`, `add_proto_library()`, `compile_proto_to_cpp()`
- `cmake/install.cmake` — Install rules
- `cmake/config.cmake.in` — CMake package config template
- `build/CommonCompilerOptions.cmake` — Compiler flags, standards, version info
  - Package version: 21.0.0-pre.12, Vendor: Genius Ventures
- `build/CommonBuildParameters.cmake` — All third-party dependency resolution
- `build/CompilationFlags.cmake` — Platform-specific compilation flags

**Static Analysis:**
- `.clang-format`, `.clang-tidy`, `.clangd` at repo root
- `.clang-tidy` disabled for generated protobuf code via `disable_clang_tidy()` in `cmake/functions.cmake`

## Platform Requirements

**Development:**
- macOS, Linux, or Windows host
- CMake 3.22+, Ninja or Visual Studio 2022
- Git with submodule support: `git submodule update --init --recursive`
- Python 3 (for test data generation scripts)
- Thirdparty dependencies pre-built or auto-downloaded from GitHub Releases

**Production - Target Platforms:**
- Linux (x86_64, aarch64/ARM)
- Windows (x86_64 — MSVC, Windows 10+)
- macOS (universal: x86_64 + ARM64, deployment target 13.0)
- iOS (cross-compiled from macOS)
- Android (armeabi-v7a, arm64-v8a, x86_64 — NDK r27b)

---

*Stack analysis: 2026-05-27*
