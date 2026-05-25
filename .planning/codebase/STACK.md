# Technology Stack

**Analysis Date:** 2026-05-25

## Languages

**Primary:**
- C++17 - Core blockchain node, networking, storage, crypto, proof system, processing engine, EVM relay
- C (via some third-party dependencies)

**Secondary:**
- JavaScript/TypeScript - zkPOC smart contract testing and deployment (`zkPOC/`)
- Solidity - Smart contracts in zkPOC (`zkPOC/`)
- Go - gRPC/OpenAPI tooling (`gRPCForSuperGenius/`), EVM relay's go-ethereum integration (`evmrelay/go-ethereum/`)
- Rust - TrustWallet `wallet_core_rs` library (via thirdparty)

## Runtime

**Environment:**
- C++17 compiler toolchain (Clang, GCC, MSVC 2022)
- CMake 3.22+ build system
- Ninja generator (primary); Visual Studio 17 2022 generator (Windows)

**Package Manager:**
- CMake FetchContent / submodule-based dependency management
- Thirdparty repository (`github.com/GeniusVentures/thirdparty`) provides pre-built dependencies
- Lockfile: Not applicable (build from thirdparty)

## Frameworks

**Core:**
- Boost 1.85.0 - Extensive use: asio, beast, json, log, program_options, coroutine, context, filesystem, date_time, random, regex, system, thread, container, unit_test_framework
  - Config: `build/CommonBuildParameters.cmake` (lines 2-9)
- Protobuf - Serialization for gRPC, CRDT deltas, processing definitions, proof data, account messages
  - Version resolved via thirdparty prebuilt

**Testing:**
- Google Test (GTest) - Unit testing framework
  - Config: enabled via `-DBUILD_TESTING=ON` / `-DTESTING=ON`
  - Located: `build/CommonBuildParameters.cmake` (line 13)
  - Run: `ctest -C [Debug|Release]`

**Build/Dev:**
- CMake 3.22+ - Cross-platform build system
- Ninja - Build executor (macOS/Linux)
- Visual Studio 17 2022 - Build system (Windows)
- Android NDK r27b - Android cross-compilation
- clang-format (Microsoft-based style) - `/.clang-format`
- clang-tidy - Static analysis (`/.clang-tidy`)
- clangd - Language server (`/.clangd`)

## Key Dependencies

**Critical (thirdparty prebuilt):**
- `Boost 1.85.0` - Concurrency, networking (Beast/ASIO), logging, JSON, coroutines, serialization
- `Protobuf` - Binary serialization protocol for gRPC and inter-node messaging
- `OpenSSL` - TLS, cryptographic primitives (`build/CommonBuildParameters.cmake` lines 106-112)
- `RocksDB` - Persistent key-value storage for blockchain state (`src/storage/rocksdb/`)
- `libp2p` v0.1.2 - Peer-to-peer networking (`build/CommonBuildParameters.cmake` lines 240-241)
- `ipfs-lite-cpp` + `ipfs-pubsub` + `ipfs-bitswap-cpp` - IPFS content-addressed storage and messaging
- `MNN` - AI/ML inference engine (`build/CommonBuildParameters.cmake` lines 92-96)
- `Vulkan` / MoltenVK - GPU compute for processing (`build/CommonBuildParameters.cmake` lines 251-259)
- `zkLLVM` - Zero-knowledge proof circuit compiler (`build/CommonBuildParameters.cmake` lines 57-126, 324-389)
- `LLVM` - Low-level compiler infrastructure for proof generation (`src/proof/CMakeLists.txt`)
- `TrustWalletCore` + `TrezorCrypto` + `wallet_core_rs` - Wallet operations, key management (`build/CommonBuildParameters.cmake` lines 400-425)
- `libsecp256k1` - secp256k1 elliptic curve cryptography (Ethereum)
- `ed25519` - Ed25519 signature scheme (`build/CommonBuildParameters.cmake` line 274)
- `SQLite3` + `SQLiteModernCpp` - Local secure storage (`build/CommonBuildParameters.cmake` lines 224-233)
- `libssh2` - SSH protocol (`build/CommonBuildParameters.cmake` lines 305-307)
- `gnus_upnp` - UPnP NAT traversal (`build/CommonBuildParameters.cmake` lines 393-394)

**Supporting:**
- `fmt` - Modern C++ string formatting
- `spdlog` v1.4.2 + `soralog` - Logging frameworks
- `rapidjson` + `nlohmann_json` - JSON parsing
- `xxHash` - Fast hash function
- `Snappy` - Compression for RocksDB
- `zlib` - Compression
- `yaml-cpp` - YAML configuration parsing
- `tsl_hat_trie` - Hat-trie data structure
- `Boost.DI` - Dependency injection
- `Microsoft.GSL` - Guidelines Support Library
- `c-ares` - Async DNS resolution
- `stb` - Single-file image/texture loading
- `AsyncIOManager` - Async I/O operations

## Configuration

**Environment:**
- All dependencies resolved via `THIRDPARTY_DIR` or auto-detected from `../thirdparty/`
- `ZKLLVM_BUILD_DIR` - Path to zkLLVM prebuilt (or auto-downloaded from GitHub Releases)
- `SGNS_NETWORK` - Network mode (`release` or development with `DEV_NET`)
- Build type: `CMAKE_BUILD_TYPE` (`Debug`, `Release`, `RelWithDebInfo`)
- Optional: `SGNS_STACKTRACE_BACKTRACE` for POSIX stacktraces
- Optional: `SANITIZE_CODE` for sanitizer builds
- Optional: `ABI_SUBFOLDER_NAME` for cross-ABI builds (e.g., `aarch64`)
- `.env` file present in `evmrelay/examples/` - contains environment configuration (not read)

**Build:**
- `build/<Platform>/CMakeLists.txt` - Platform-specific top-level CMake (OSX, Linux, Windows, Android, iOS)
- `build/CommonBuildParameters.cmake` (547 lines) - All thirdparty dependency paths and configuration
- `build/CommonCompilerOptions.cmake` (195 lines) - Compiler standard, flags, toolchain setup
- `build/CompilationFlags.cmake` (38 lines) - Warning flags per compiler
- `cmake/version.cmake` (56 lines) - Git-based versioning: project version 3.7.0, git tag/branch/commit
- `cmake/functions.cmake` - Helper CMake functions
- Output: `compile_commands.json` generated for IDE support

## Platform Requirements

**Development:**
- macOS 13.0+ (x86_64 + ARM universal binary)
- Linux (x86_64, aarch64)
- Windows 10+ (MSVC 2022, Win32 target 0x0A00)
- CMake 3.22+, Ninja, Git
- Thirdparty build directory (sibling `../thirdparty/`)

**Production:**
- Desktop: macOS (fat binary), Linux (x86_64/aarch64), Windows
- Mobile: Android (armeabi-v7a, arm64-v8a, x86_64), iOS
- Native C++ library deployable on all targets

---

*Stack analysis: 2026-05-25*
