# Coding Conventions

**Analysis Date:** 2026-05-25

## Naming Patterns

**Files:**
- Header files: `PascalCase.hpp` — e.g., `Buffer.hpp`, `ProcessingEngine.hpp`, `ScaledInteger.hpp`
- Source files: `PascalCase.cpp` — e.g., `buffer.cpp`, `processing_engine.cpp` (note: some sources use snake_case, others PascalCase; headers consistently PascalCase)
- Exception: `IComponent.hpp`, `IComponent` prefix for pure interface classes

**Classes / Structs:**
- PascalCase: `Buffer`, `ProcessingEngine`, `ScaledInteger`, `ED25519ProviderImpl`
- Pure interface classes use `I` prefix: `IComponent`
- Test fixtures: PascalCase — e.g., `ED25519ProviderTest`, `FixedPrecisionTest`, `ProcessingServiceTest`
- Parameter structs: PascalCase with `_s` suffix — e.g., `FixedPrecisionParam_s`, `ParseModeParam_s`

**Functions / Methods:**
- PascalCase: `GenerateKeypair()`, `ProcessSubTask()`, `CreateTestSubTask()`, `SetUp()`
- Static factory methods: `New()` or `fromHex()` — some follow PascalCase, others camelCase (inconsistency noted)

**Variables:**
- camelCase: `subTask`, `nodeId`, `checkInterval`, `startTime`, `hexSeed`
- Private member variables: `m_` prefix — e.g., `m_nodeId`, `m_processingCore`, `m_logger`, `m_subTaskQueueAccessor`
- Private member variables with trailing underscore: `value_`, `precision_`, `data_` (used in some classes like `ScaledInteger`, `Buffer`)

**Constants:**
- `kCamelCase` for constexpr constants: `kBridgeEventSubjectType`, `kNonceSubjectType`
- UPPER_CASE for enum members: `PRIVKEY_SIZE`, `PUBKEY_SIZE`, `SIGNATURE_SIZE`, `SEED_SIZE`
- UPPER_CASE for enum class values: `INCORRECT_LENGTH`, `FAILED_GENERATE_KEYPAIR`, `SIGN_UNKNOWN_ERROR`

**Types / Aliases:**
- PascalCase: `Hash256`, `Hash512`, `ED25519PrivateKey`, `ED25519PublicKey`, `ED25519Signature`
- Using `using` declarations, not `typedef`

**Enums:**
- `enum class` for strongly-typed enums: `BlobError`, `ED25519ProviderError`, `UnhexError`, `ParseMode`
- Plain `enum` for grouping constants in namespaces: `constants::ed25519::PRIVKEY_SIZE`
- PascalCase for enum class names, UPPER_CASE for enumerators

**Namespaces:**
- snake_case, nested with full indentation: `sgns::base`, `sgns::crypto`, `sgns::processing`, `sgns::storage`, `sgns::scale`, `sgns::crdt`, `sgns::test`
- Sub-namespaces: `sgns::crypto::constants::ed25519`, `sgns::storage::trie`, `sgns::storage::changes_trie`

**Header Guards:**
- `#ifndef SUPERGENIUS_<PATH>_HPP` / `#define` / `#endif` style: `SUPERGENIUS_BLOB_HPP`, `SUPERGENIUS_BUFFER_HPP`, `SUPERGENIUS_SRC_CRYPTO_ED25519_TYPES_HPP`
- Some older files use `#pragma once` (e.g., `ScaledInteger.hpp`) — mixed usage
- Some use underscore-prefixed guards: `_ICOMPONENT_HPP_`, `_UTIL_HPP`

## Code Style

**Formatting:**
- Tool: `clang-format` with `BasedOnStyle: Microsoft`
- Key settings (see `.clang-format`):
  - Indent: 4 spaces
  - Column limit: 120 characters
  - Brace style: Allman/Ullman (braces on their own lines, enforced by `BreakBeforeBraces: Allman` from Microsoft base)
  - Spaces inside parentheses for conditional statements and `Other`: `if ( condition )`
  - Trailing commas: `InsertTrailingCommas: Wrapped`
  - Align consecutive assignments, declarations, bitfields: enabled
  - Allow short functions/blocks on single line: `Empty` only
  - Always break template declarations (long templates go to next line)
  - Constructor initializers: break after colon
  - Sort includes: `Never` (manual include order preserved)
  - C++ standard: `c++17`
  - `InsertBraces: true` — always add braces to single-statement if/while/for
  - `RemoveParentheses: ReturnStatement` — remove unnecessary parens on return
  - `RemoveSemicolon: true` — remove redundant semicolons

**Linting:**
- Tool: `clang-tidy` (see `.clang-tidy`)
- Enabled checks: `boost-*`, `bugprone-*`, `cert-*`, `concurrency-*`, `cppcoreguidelines-*`, `google-*`, `hicpp-multiway-paths-covered`, `misc-*`, `modernize-*`, `performance-*`, `portability-*`, `readability-*`
- Disabled notable checks (project-specific allowances):
  - `-readability-identifier-length` — short identifiers allowed
  - `-readability-magic-numbers` — magic numbers tolerated
  - `-bugprone-easily-swappable-parameters` — not enforced
  - `-modernize-use-trailing-return-type` — classical return type style preferred
  - `-cppcoreguidelines-avoid-magic-numbers` — not enforced
  - `-cppcoreguidelines-non-private-member-variables-in-classes` — public members allowed in structs
  - `-misc-non-private-member-variables-in-classes` — with exception for all-public classes
- Naming rules enforced: `ClassCase: CamelCase`, `FunctionCase: CamelCase`, `EnumCase: CamelCase`, `EnumConstantCase: UPPER_CASE`, `ConstexprVariableCase: UPPER_CASE`, `TypeAliasCase: CamelCase`, `DefaultCase: lower_case`
- Format style: `file` (uses project's `.clang-format`)
- Test targets are excluded from clang-tidy via `disable_clang_tidy()` CMake function

## Import Organization

**Order (manual, no automatic sorting):**
1. Project header (matching .cpp to .hpp)
2. Google Test headers (`<gtest/gtest.h>`, `<gmock/gmock.h>`)
3. Standard library headers
4. Boost headers
5. Project utility headers (`testutil/`, `outcome/`, `base/`)
6. Project domain headers

**Path Aliases:**
- No explicit include path aliases — includes use paths relative to the `src/` directory root: `"base/blob.hpp"`, `"crypto/ed25519_types.hpp"`, `"outcome/outcome.hpp"`

**Typical patterns observed:**
```cpp
// In source files:
#include "processing/processing_engine.hpp"

#include <gtest/gtest.h>
#include <boost/functional/hash.hpp>
#include "testutil/outcome.hpp"
#include "base/logger.hpp"
```

## Error Handling

**Primary Pattern: `outcome::result<T>` (Boost.Outcome-based)**

The codebase uses a custom outcome wrapper (`src/outcome/outcome.hpp`) re-exporting `libp2p::outcome::result`:
```cpp
namespace outcome {
    using libp2p::outcome::result;
    using libp2p::outcome::success;
    using libp2p::outcome::failure;
}
```

**Error code definition:**
- `enum class` for error codes with explicit values, defined alongside the interface: `BlobError`, `ED25519ProviderError`, `UnhexError`
- Error codes must have a conversion to `std::error_code` (via Boost.System)
- Registered at bottom of header with `OUTCOME_HPP_DECLARE_ERROR_2(namespace, ErrorType)` macro — see `src/base/blob.hpp:228`, `src/crypto/ed25519_provider.hpp:58`, `src/base/hexutil.hpp:58`

**Error propagation:**
- `BOOST_OUTCOME_TRY(auto var, expression)` macro for early return on error — used extensively in `src/base/blob.hpp`, `src/storage/`, `src/proof/`
- `BOOST_OUTCOME_TRYV2(auto &&, expression)` — C++17 compatible variant

**Return patterns:**
```cpp
// Success return:
return blob;                               // implicit conversion from T to result<T>
return outcome::success();                 // for result<void>

// Error return:
return BlobError::INCORRECT_LENGTH;        // enum auto-converts to failure
return SomeError::VALUE;                   // error code

// Factory functions:
static outcome::result<Blob<size_>> fromString(std::string_view data) {
    if (data.size() != size_) {
        return BlobError::INCORRECT_LENGTH;
    }
    // ... success path
}
```

**Exception handling:**
- By default, functions should be `noexcept` unless explicitly required to throw
- `src/base/outcome_throw.hpp` provides `sgns::base::raise()` template for converting outcome errors to `boost::exception`
- Exceptions caught via `std::system_error` in test contexts (`EXPECT_OUTCOME_RAISE` macro)
- Some code in `src/base/util.hpp` uses `throw std::invalid_argument()` for validation — legacy pattern

## Logging

**Framework:** `spdlog` via `sgns::base::Logger` (typedef for `std::shared_ptr<spdlog::logger>`)

**Patterns:**
```cpp
// In class members:
base::Logger m_logger = base::createLogger("ProcessingEngine");

// Factory:
Logger createLogger(const std::string &tag, const std::string &basepath = "");
```

**Key file:** `src/base/logger.hpp` — defines `Logger` type and `createLogger` factory function. Platform-specific sink configuration for Android.

## Comments

**When to Comment:**
- Every public method / interface must have a Doxygen-compatible header
- Test cases use Given/When/Then format in Doxygen blocks

**Doxygen conventions:**
```cpp
/**
 * @brief Short description of what the function/class does.
 * @param param_name Description of the parameter.
 * @return Description of the return value.
 */

// Test case format:
/**
 * @given preconditions for the test
 * @when the action being tested
 * @then expected outcome
 */
```

**In-code comments:**
- `//` for single-line comments and explanations
- `/* */` not commonly used outside Doxygen
- `// NOLINT` used to suppress clang-tidy warnings (e.g., `src/base/blob.hpp:224`)
- `// TODO(author): description` for tracking pending work — many tagged with task IDs like `PRE-285`, `PRE-461`
- `///` rarely used — multi-line `/** */` preferred

**File headers:** Some files have file-level `@file/@author/@brief/@date/@copyright` Doxygen blocks — see `src/base/ScaledInteger.hpp`, `src/base/util.hpp`, `src/singleton/IComponent.hpp`

## Function Design

**Size:** Functions vary in size; complex processing logic can be lengthy. No strict limit enforced.

**Parameters:**
- Prefer `const &` for complex types, pass-by-value for small/trivial types
- In-out parameters avoided; return structs preferred
- Output parameters use gsl::span for byte ranges: `gsl::span<uint8_t> message`

**Return Values:**
- `outcome::result<T>` for fallible operations
- `void` for pure side-effect functions
- `bool` for simple success/failure where error details aren't needed

**Modifiers:**
- `[[nodiscard]]` on functions where ignoring the return value is likely an error: `toString()`, `toHex()`, `size()`, `data()`
- `const` member functions for non-mutating operations
- `noexcept` on functions guaranteed not to throw
- `override` on all overriding virtual functions
- `virtual` destructors on polymorphic base classes: `virtual ~ED25519Provider() override = default;`

## Module Design

**Exports:**
- Each module typically has a public header in `src/<module>/` exposing the interface
- Implementation details in `src/<module>/impl/` subdirectory
- Some modules use `_impl` suffix for implementation classes: `ED25519ProviderImpl`, `ProcessingCoreImpl`

**Module structure:**
```
src/<module>/
├── <module>_core.hpp      # Abstract interface
├── impl/
│   └── <module>_impl.hpp  # Concrete implementation
├── proto/                  # Protobuf definitions (if applicable)
└── CMakeLists.txt
```

**Barrel Files:**
- Not used — each header includes exactly what it needs
- Redundant includes not stripped (clang-tidy `misc-include-cleaner` is disabled)

## Class Design

**Inheritance:**
- Public inheritance models `is-a`: `Blob<size_> : public std::array<uint8_t, size_>` (`src/base/blob.hpp:27`)
- Interface inheritance via pure abstract classes: `ED25519Provider : public IComponent` (`src/crypto/ed25519_provider.hpp:18`)
- `std::enable_shared_from_this` used for classes that need to produce shared pointers to themselves: `ProcessingEngine : public std::enable_shared_from_this<ProcessingEngine>` (`src/processing/processing_engine.hpp:18`)

**Member ordering:**
- Public interface first, then private implementation
- Public section: constructors/destructors, then methods
- Private section: data members last (typically with `m_` prefix)

**Construction patterns:**
- Factory methods preferred over complex constructors: `ScaledInteger::New()` static methods return `outcome::result<std::shared_ptr<ScaledInteger>>`
- `= default` for default constructors/destructors/copy/move where applicable
- Private constructors with public static factory methods

**Template usage:**
- `extern template class Blob<N>` for explicit instantiation declarations to reduce compile times (`src/base/blob.hpp:167-170`)
- SFINAE with `std::enable_if_t<Stream::is_encoder_stream>` for conditional template operators

## CMake Conventions

**Library declarations:** `add_library(library_name ...)` with `supergenius_install(library_name)` for install targets

**Test declarations:** Custom `addtest(test_name source.cpp)` function from `cmake/functions.cmake` — adds Google Test executable, sets XML output, disables clang-tidy

**Typical test CMakeLists.txt:**
```cmake
addtest(ed25519_provider_test
    ed25519_provider_test.cpp
)
target_link_libraries(ed25519_provider_test
    ed25519_provider
)
```

**Proto compilation:** Custom `add_proto_library()` / `compile_proto_to_cpp()` functions for protobuf integration

## Cross-Cutting Patterns

**Component registration:** `IComponent` interface with `GetName()` method (`src/singleton/IComponent.hpp`) — used for runtime type identification

**RAII:** Smart pointers throughout — `std::shared_ptr` for shared ownership, `std::unique_ptr` in some cases. `std::make_shared` preferred over raw `new`.

**Const-correctness:** Strong preference for `const` — parameters, member functions, local variables, return types. `const &` for read-only complex type parameters.

**Template constraints:** `static_assert` for compile-time type validation in template functions (e.g., `src/base/util.hpp:70-73`).

---

*Convention analysis: 2026-05-25*
