# Testing Patterns

**Analysis Date:** 2026-05-25

## Test Framework

**Runner:**
- Google Test (GTest) + Google Mock (GMock)
- Config: `cmake/functions.cmake` — custom `addtest()` function
- CMake links each test with `GTest::gtest_main` and `GTest::gmock_main`
- XML output configured: `--gtest_output=xml:${CMAKE_BINARY_DIR}/xunit/xunit-${test_name}.xml`
- Test binaries output to `${CMAKE_BINARY_DIR}/test_bin`

**Assertion Library:**
- GTest macros: `ASSERT_EQ`, `ASSERT_TRUE`, `ASSERT_FALSE`, `ASSERT_NE`, `ASSERT_NO_THROW`
- `EXPECT_*` variants for non-fatal assertions
- Custom outcome macros from `test/testutil/outcome.hpp`
- Custom wait condition macros from `test/testutil/wait_condition.hpp`

**Run Commands:**
```bash
# Build and run all tests (platform-specific build dir):
cd build/OSX/Debug && cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && ninja

# Run a specific test binary:
./build/OSX/Debug/test_bin/ed25519_provider_test

# Run tests via ctest:
cd build/OSX/Debug && ctest
```

## Test File Organization

**Location:**
- Co-located in `test/` directory tree mirroring `src/` structure:
  - `test/src/base/blob_test.cpp` → tests `src/base/blob.hpp`
  - `test/src/crypto/ed25519/ed25519_provider_test.cpp` → tests `src/crypto/ed25519/`
  - `test/src/processing/processing_engine_test.cpp` → tests `src/processing/processing_engine.hpp`

**Naming:**
- `*_test.cpp` — e.g., `blob_test.cpp`, `buffer_test.cpp`, `ed25519_provider_test.cpp`
- Some tests use PascalCase source files: `ProverTest.cpp`, `GeniusProofsTest.cpp` (inconsistency)

**Structure:**
```
test/
├── CMakeLists.txt           # Top-level: add_subdirectory(src) and add_subdirectory(testutil)
├── mock/
│   └── src/                 # Mock classes mirroring src/ structure
│       ├── crypto/
│       │   └── ed25519_provider_mock.hpp
│       ├── storage/
│       └── runtime/
├── src/
│   ├── CMakeLists.txt       # Subdirectories for each test module
│   ├── base/
│   │   ├── CMakeLists.txt
│   │   ├── blob_test.cpp
│   │   ├── buffer_test.cpp
│   │   ├── hexutil_test.cpp
│   │   └── scaled_integer_test.cpp
│   ├── crypto/
│   │   └── ed25519/
│   │       ├── CMakeLists.txt
│   │       └── ed25519_provider_test.cpp
│   ├── processing/
│   │   ├── processing_engine_test.cpp
│   │   ├── processing_service_test.hpp       # Shared test fixture base class
│   │   ├── processing_mock.hpp               # Local test mocks
│   │   ├── processing_subtask_validation_test.cpp
│   │   └── processing_subtask_queue_manager_test.cpp
│   ├── scale/
│   │   ├── scale_pair_test.cpp
│   │   ├── scale_fixed_test.cpp
│   │   └── ...
│   └── ...
└── testutil/
    ├── CMakeLists.txt
    ├── outcome.hpp              # Outcome test assertion macros
    ├── wait_condition.hpp       # Wait-condition test templates
    ├── literals.hpp             # User-defined literal operators for test data
    ├── color_support.hpp        # Terminal color detection for test output
    ├── mint_source_hash.hpp
    ├── sr25519_utils.hpp
    ├── primitives/
    │   ├── CMakeLists.txt
    │   ├── hash_creator.cpp
    │   └── mp_utils.hpp
    └── storage/
        ├── base_crdt_test.hpp
        ├── base_fs_test.hpp
        ├── base_rocksdb_test.hpp
        └── supergenius_trie_printer.hpp
```

## Test Structure

**Suite Organization:**

The codebase uses three primary test patterns:

### 1. Simple standalone tests (`TEST`)
```cpp
// From test/src/scale/scale_pair_test.cpp
TEST(Scale, encodePair) {
    uint8_t v1 = 1;
    uint32_t v2 = 2;
    ScaleEncoderStream s;
    ASSERT_NO_THROW((s << std::make_pair(v1, v2)));
    ASSERT_EQ(s.data(), (ByteArray{1, 2, 0, 0, 0}));
}

TEST(Scale, decodePair) {
    ByteArray bytes = {1, 2, 0, 0, 0};
    ScaleDecoderStream s(bytes);
    using pair_type = std::pair<uint8_t, uint32_t>;
    pair_type pair{};
    ASSERT_NO_THROW((s >> pair));
    ASSERT_EQ(pair.first, 1);
    ASSERT_EQ(pair.second, 2);
}
```

### 2. Test fixtures (`TEST_F`)
```cpp
// From test/src/crypto/ed25519/ed25519_provider_test.cpp
struct ED25519ProviderTest : public ::testing::Test {
    void SetUp() override {
        ed25519_provider = std::make_shared<ED25519ProviderImpl>();
        std::string_view m = "i am a message";
        message = std::vector<uint8_t>(m.begin(), m.end());
        hex_seed = "ccb4ec79...";
        hex_public_key = "939AA4B6...";
    }
    std::string_view hex_seed;
    std::string_view hex_public_key;
    gsl::span<uint8_t> message_span;
    std::vector<uint8_t> message;
    std::shared_ptr<ED25519Provider> ed25519_provider;
};

TEST_F(ED25519ProviderTest, GenerateKeysNotEqual) {
    for (auto i = 0; i < 10; ++i) {
        EXPECT_OUTCOME_TRUE(kp1, ed25519_provider->generateKeypair());
        EXPECT_OUTCOME_TRUE(kp2, ed25519_provider->generateKeypair());
        ASSERT_NE(kp1.public_key, kp2.public_key);
        ASSERT_NE(kp1.private_key, kp2.private_key);
    }
}
```

### 3. Inherited test fixtures (shared setup)
```cpp
// From test/src/processing/processing_subtask_queue_manager_test.cpp
class ProcessingSubTaskQueueManagerTest : public ProcessingServiceTest {
public:
    void SetUp() override {
        // Uses shared ProcessingServiceTest infrastructure
    }
    const std::string nodeId1 = "NODE_1";
    const std::string nodeId2 = "NODE_2";
};
```

### 4. Parameterized tests (`TEST_P`)
```cpp
// From test/src/base/scaled_integer_test.cpp
struct FixedPrecisionParam_s {
    std::string input;
    uint64_t precision;
    std::variant<uint64_t, std::errc> expected;
};

class FixedPrecisionTest : public ::testing::TestWithParam<FixedPrecisionParam_s> {};

TEST_P(FixedPrecisionTest, FromStringWithSpecifiedPrecision) {
    const auto &tc = GetParam();
    auto result = sgns::ScaledInteger::FromString(tc.input, tc.precision);
    if (std::holds_alternative<uint64_t>(tc.expected)) {
        uint64_t expected_val = std::get<uint64_t>(tc.expected);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), expected_val);
    } else {
        std::errc expected_err = std::get<std::errc>(tc.expected);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), std::make_error_code(expected_err));
    }
}

INSTANTIATE_TEST_SUITE_P(
    FixedPrecisionFromStringTests,
    FixedPrecisionTest,
    ::testing::Values(
        FixedPrecisionParam_s{"123.456", 9ULL, 123456000000ULL},
        FixedPrecisionParam_s{"abc", 9ULL, std::errc::invalid_argument},
        // ... more cases
    ));
```

**Setup/Teardown patterns:**
- `SetUp()` override for per-test initialization
- `TearDown()` override for cleanup (used in `ProcessingServiceTest`)
- Constructor-based initialization for simple data members
- `static` helper methods in test fixtures for creating test data: `CreateTestSubTask()`, `CreateValidResult()`

**Assertion patterns:**
- Use `ASSERT_*` (fatal) for conditions that invalidate the rest of the test
- Use `EXPECT_*` (non-fatal) for conditions where the test should continue
- `ASSERT_NO_THROW({ ... })` for verifying no exception is thrown (with error message via `<<`)
- `ASSERT_NO_THROW({ result.error(); })` with message for verifying an error occurred

## Mocking

**Framework:** Google Mock (GMock)

**Location:** `test/mock/src/` — organized to mirror `src/` structure

**Patterns:**
```cpp
// From test/mock/src/crypto/ed25519_provider_mock.hpp
class ED25519ProviderMock : public ED25519Provider {
public:
    MOCK_CONST_METHOD0(generateKeypair, outcome::result<ED25519Keypair>());
    MOCK_CONST_METHOD1(generateKeypair,
                       outcome::result<ED25519Keypair>(const ED25519Seed &));
    MOCK_CONST_METHOD2(sign,
                       outcome::result<ED25519Signature>(const ED25519Keypair &,
                                                         gsl::span<uint8_t>));
    MOCK_CONST_METHOD3(verify,
                       outcome::result<bool>(const ED25519Signature &signature,
                                             gsl::span<uint8_t> message,
                                             const ED25519PublicKey &public_key));
    MOCK_METHOD0(GetName, std::string());
};
```

**In-test mock classes:**
For complex tests, mocks are sometimes defined directly in the test file in an anonymous namespace:
```cpp
// From test/src/processing/processing_engine_test.cpp
namespace {
    class SubTaskQueueAccessorMock : public SubTaskQueueAccessor {
    public:
        bool ConnectToSubTaskQueue(...) override { /* ... */ }
        // ... method overrides
    private:
        std::list<SGProcessing::SubTask> m_subTasks;
    };
}
```

**What to Mock:**
- External dependencies (network, storage, crypto providers)
- Interfaces to isolate the unit under test
- Slow or non-deterministic components

**What NOT to Mock:**
- Value types / data classes (`Blob`, `Buffer`, `SubTask`)
- Utilities (`ScaledInteger`, `hexutil`)
- Test data is created directly via constructors or factory methods

## Fixtures and Factories

**Test Data creation:**
```cpp
// Inline initialization:
std::string hex32 = "00ff";
std::array<uint8_t, 2> expected{ 0, 255 };

// Factory methods:
auto result = Blob<2>::fromHex(hex32);

// User-defined literals for concise test data (test/testutil/literals.hpp):
auto buf = "hello"_buf;
auto hash = "abcdef..."_hash256;
auto hexVec = "deadbeef"_unhex;

// Helper methods in test fixture:
static SGProcessing::SubTask CreateTestSubTask(const std::string &subTaskId, int numChunks);
```

**Location:**
- `test/testutil/literals.hpp` — custom literal operators for test data construction
- `test/testutil/primitives/` — hash creators, multi-precision utilities
- `test/testutil/storage/` — base test classes for storage backends (filesystem, RocksDB, CRDT)

## Coverage

**Requirements:** No enforced coverage target detected. Coverage configuration not found in CMakeLists.txt.

**View Coverage:** Not configured in the build system. To add coverage:
```bash
# If adding manually:
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
```

## Test Types

**Unit Tests:**
- Location: `test/src/` in individual files
- Scope: One class/module per test file
- Approach: Direct instantiation of the class under test, no external services
- Examples: `blob_test.cpp`, `scale_pair_test.cpp`, `ed25519_provider_test.cpp`

**Integration Tests:**
- Location: `test/src/processing/`, `test/src/crdt/`, `test/src/multiaccount/`
- Scope: Multiple components interacting (e.g., processing engine + queue + pubsub)
- Approach: Shared fixture base classes (`ProcessingServiceTest`), real pubsub channels, mock or real processing cores
- Examples: `processing_subtask_validation_test.cpp`, `crdt_datastore_test.cpp`

**E2E Tests:**
- Not formally separated; some integration tests approach E2E scope (multi-node processing tests, account sync tests)
- Examples: `test/src/processing_multi/processing_multi_test.cpp`, `test/src/multiaccount/multi_account_sync.cpp`

## Common Patterns

**Async Testing (Wait Conditions):**
The project has a custom wait-condition framework that MUST be used instead of `std::this_thread::sleep_for` in tests.

From `test/testutil/wait_condition.hpp`:

```cpp
// Fatal assertion — terminates test on timeout:
ASSERT_WAIT_FOR_CONDITION(condition, timeout, description, actualDuration);

// Non-fatal assertion — reports failure but test continues:
EXPECT_WAIT_FOR_CONDITION(condition, timeout, description, actualDuration);

// With custom check interval:
ASSERT_WAIT_FOR_CONDITION_INTERVAL(condition, timeout, description, actualDuration, checkInterval);

// Usage example:
std::atomic<bool> done{false};
ASSERT_WAIT_FOR_CONDITION([&done]() { return done.load(); },
    std::chrono::milliseconds(5000), "Processing should complete");
```

The underlying `waitForCondition()` template is defined in `src/base/util.hpp` and polls the condition at a configurable interval. The test macros report failure at the caller's source location using `GTEST_MESSAGE_AT_`.

**⚠️ CRITICAL:** Never use `std::this_thread::sleep_for` in tests. Always use the wait-condition templates.

**Outcome Testing:**
Custom macros from `test/testutil/outcome.hpp` for testing `outcome::result<T>`:

```cpp
// Expect success and extract value:
EXPECT_OUTCOME_TRUE(kp, ed25519_provider->generateKeypair());
// ^^^ kp is now the ED25519Keypair value

// Expect success, discard value:
EXPECT_OUTCOME_TRUE_1(expression);

// Expect error:
EXPECT_OUTCOME_FALSE_1(ed25519_provider->sign(kp, message_span));
EXPECT_OUTCOME_ERROR(result, expression, expected_error_code);

// Fatal assertions:
ASSERT_OUTCOME_SUCCESS(variable, expression);
ASSERT_OUTCOME_SUCCESS_TRY(expression);   // Assert success, ignore value
ASSERT_OUTCOME_ERROR(expression, error);  // Assert specific error
ASSERT_OUTCOME_SOME_ERROR(expression);    // Assert any error

// Non-fatal assertions:
EXPECT_OUTCOME_SUCCESS(result, expression);
EXPECT_OUTCOME_ERROR(result, expression, error);
EXPECT_OUTCOME_SOME_ERROR(result, expression);

// With custom error message:
EXPECT_OUTCOME_TRUE_MSG(val, expr, "custom failure message");

// Exception expectations:
EXPECT_OUTCOME_RAISE(error_code, statement_that_should_throw);
```

**Error Testing:**
```cpp
// Verify error is returned (not thrown):
TEST(BlobTest, CreateFromNonHex) {
    std::string not_hex = "nothex";
    auto result = Blob<2>::fromHex(not_hex);
    ASSERT_NO_THROW({ result.error(); })
        << "fromHex returned a value instead of error";
}

// Verify specific error code:
EXPECT_OUTCOME_ERROR(result, someFunction(), ExpectedError::VALUE);
```

**Disabled Tests:**
```cpp
TEST_F(ED25519ProviderTest, DISABLED_SignWithInvalidKeyFails) { ... }
TEST_F(ED25519ProviderTest, DISABLED_VerifyInvalidKeyFail) { ... }
```

## CMake Test Configuration

Each test is declared using the custom `addtest()` function from `cmake/functions.cmake`:

```cmake
# In test/src/base/CMakeLists.txt:
addtest(buffer_test
    buffer_test.cpp
)
target_link_libraries(buffer_test
    buffer
)
```

The `addtest()` function:
1. Creates an executable with `add_executable()`
2. Links with `GTest::gtest_main` and `GTest::gmock_main`
3. Configures XML output to `${CMAKE_BINARY_DIR}/xunit/`
4. Registers with CTest via `add_test()`
5. Sets output directories to `${CMAKE_BINARY_DIR}/test_bin`
6. Disables clang-tidy for the test target via `disable_clang_tidy()`

## Test Utility Modules

| Utility | Path | Purpose |
|---------|------|---------|
| `outcome.hpp` | `test/testutil/outcome.hpp` | Macros for testing `outcome::result` values and errors |
| `wait_condition.hpp` | `test/testutil/wait_condition.hpp` | Polling-based async condition assertions |
| `literals.hpp` | `test/testutil/literals.hpp` | User-defined literal operators (`_buf`, `_hash256`, `_unhex`, `_v`, etc.) |
| `color_support.hpp` | `test/testutil/color_support.hpp` | Terminal color detection for test log output |
| `base_rocksdb_test.hpp` | `test/testutil/storage/base_rocksdb_test.hpp` | Base test fixture for RocksDB-backed storage tests |
| `base_fs_test.hpp` | `test/testutil/storage/base_fs_test.hpp` | Base test fixture for filesystem-backed storage tests |
| `base_crdt_test.hpp` | `test/testutil/storage/base_crdt_test.hpp` | Base test fixture for CRDT datastore tests |
| `mp_utils.hpp` | `test/testutil/primitives/mp_utils.hpp` | Multi-precision math test utilities |
| `hash_creator.cpp` | `test/testutil/primitives/hash_creator.cpp` | Hash generation test helpers |

---

*Testing analysis: 2026-05-25*
