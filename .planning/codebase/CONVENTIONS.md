# Coding Conventions

**Analysis Date:** 2026-05-27

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

**Files:**
- Header files: `.hpp` extension, names vary — both `CamelCase` (`GeniusAccount.hpp`) and `snake_case` (`buffer.hpp`, `outcome_throw.hpp`)
- Source files: `.cpp` extension, match their header name
- Libraries/directories: `snake_case` (`src/account/`, `src/local_secure_storage/`)

**Private member variables:**
- snake_case with trailing underscore: `data_` (in `Buffer`, `src/base/buffer.hpp`), `utxo_manager_`, `storage_`, `is_full_node_`, `logger_`
- This is consistent across the codebase

**Parameters:**
- Both `snake_case` and `camelCase` appear, but `snake_case` is the majority pattern (e.g., `base_path`, `token_id`, `full_node` in `GeniusAccount.hpp`)
- Some API-level code uses `a`-prefixed camelCase: `aKey`, `aDelta`, `aID` (in `src/crdt/crdt_set.hpp`)

**Namespaces:**
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

The codebase uses **both** patterns:
- **`#pragma once`** — newer code written since ~2024 (e.g., `src/account/GeniusAccount.hpp`, `src/account/UTXOManager.hpp`, `src/blockchain/Consensus.hpp`)
- **`#ifndef SUPERGENIUS_..._HPP`** — older infrastructure code (e.g., `src/base/buffer.hpp`, `src/base/logger.hpp`, `src/scale/*`, `src/storage/*`, `src/primitives/*`)
- No file uses both; choose one based on the directory you're adding to

## Import Organization

### Include Order (Observed Pattern)

```cpp
// 1. Project header matching the .cpp file
#include "base/buffer.hpp"

// 2. Other project headers
#include "base/hexutil.hpp"
#include "outcome/outcome.hpp"

// 3. Third-party headers (Boost, libp2p, gRPC, gtest)
#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <spdlog/spdlog.h>

// 4. Standard library headers
#include <string>
#include <vector>
```

Includes are manually ordered; `.clang-format` sets `SortIncludes: Never`.

### Path Style

- Project includes use quotes: `#include "account/GeniusAccount.hpp"`
- System includes use angle brackets: `#include <gtest/gtest.h>`
- Include paths are relative to `src/` directory (which is on the include path)

## Error Handling

### Primary Pattern: `outcome::result<T>`

The codebase uses `outcome::result<T>` (from `src/outcome/outcome.hpp`, which wraps `libp2p::outcome`) as the primary error-handling mechanism. This is used extensively — over 1300 occurrences in `src/`.

```cpp
// Declaration pattern (from src/base/hexutil.hpp)
outcome::result<std::vector<uint8_t>> unhex(std::string_view hex);

// Usage pattern
auto result = someFunction();
if (result.has_error()) {
    logger_->error("Error: {}", result.error().message());
    return result.error();
}
auto value = result.value();
```

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

```cpp
// In class definition (from src/account/UTXOManager.hpp)
base::Logger logger_ = base::createLogger("UTXOManager");

// Usage (from src/account/UTXOManager.cpp)
logger_->error("Error when loading UTXOs");
logger_->info("message");
```

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

```cpp
/**
 * @file       GeniusAccount.hpp
 * @brief      Header file of the Genius account class
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
```

```cpp
TokenID token;  ///< Token ID of the account
```

### Test Comments (Gherkin-style)

Tests use a structured comment format:
```cpp
/**
 * @given empty buffer
 * @when put different stuff in this buffer
 * @then result matches expectation
 */
TEST(Common, BufferPut) { ... }
```

## Function Design

### Size Guidelines

- Most source files are under 500 lines
- Large files exist and are noted as tech debt:
  - `src/account/TransactionManager.cpp` (~4829 lines)
  - `src/blockchain/Consensus.cpp` (~2762 lines)
  - `src/account/GeniusNode.cpp` (~1938 lines)
- Prefer smaller, focused files and functions

### Parameter Style

- Const references for input: `const TokenID &token_id`, `const std::string &address`
- Values for sinks: `std::string address` (when moved), `std::vector<uint8_t> data`
- Output via return value, not out-parameters (consistent with `outcome::result<T>` pattern)

```cpp
// Preferred: return the result
outcome::result<uint64_t> GetBalance(const TokenID &token_id) const;

// Not: pass by out-parameter
void GetBalance(const TokenID &token_id, uint64_t &balance_out); // anti-pattern
```

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

```cpp
class ClassName
{
public:
    // Types and constants
    using TypeAlias = ...;
    static constexpr int CONSTANT = ...;

    // Static factory methods
    static std::shared_ptr<ClassName> New(...);

    // Public interface
    outcome::result<T> PublicMethod(...) const;

protected:
    // For friend access

private:
    // Private implementation
    outcome::result<T> PrivateHelper(...);

    // Members (trailing underscore)
    Type member_;
};
```

## Platform-Specific Code

- Platform-specific implementations use `#if defined(ANDROID)` or similar preprocessor guards
- Platform files in `src/local_secure_storage/impl/`: `Linux.cpp`, `Windows.cpp`, `Apple.cpp`, `Android.cpp`
- Build configurations per platform in `build/{iOS,OSX,Windows,Linux,Android}/CMakeLists.txt`

## Code Patterns

### Factory Construction

The codebase uses static `New()` factory methods rather than public constructors:

```cpp
static std::shared_ptr<GeniusAccount> New(TokenID token_id, ...);
static outcome::result<std::shared_ptr<GlobalDB>> New(...);
```

### Self-Registration Pattern

Some classes use static self-registration:
```cpp
static bool Register() {
    RegisterDeserializer("transfer", &TransferTransaction::DeSerializeByteVector);
    return true;
}
static inline bool registered = Register();
```

### Singleton Component Factory

Located in `src/singleton/` — `CComponentFactory` provides dependency injection via `IComponent` interface.

---

*Convention analysis: 2026-05-27*
