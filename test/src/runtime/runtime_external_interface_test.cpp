
#include "runtime/binaryen/runtime_external_interface.hpp"

#include <binaryen/wasm-s-parser.h>
#include <boost/format.hpp>
#include <crypto/crypto_store/key_type.hpp>
#include <runtime/wasm_result.hpp>
#include "src/runtime/mock_memory.hpp"
#include "mock/src/extensions/extension_factory_mock.hpp"
#include "mock/src/extensions/extension_mock.hpp"
#include "mock/src/runtime/trie_storage_provider_mock.hpp"
#include "mock/src/storage/trie/trie_batches_mock.hpp"
#include "mock/src/storage/trie/trie_storage_mock.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

using sgns::crypto::key_types::kProd;
using sgns::extensions::Extension;
using sgns::extensions::ExtensionFactoryMock;
using sgns::extensions::ExtensionMock;
using sgns::runtime::MockMemory;
using sgns::runtime::TrieStorageProviderMock;
using sgns::runtime::WasmEnum;
using sgns::runtime::WasmLogLevel;
using sgns::runtime::WasmOffset;
using sgns::runtime::WasmPointer;
using sgns::runtime::WasmResult;
using sgns::runtime::WasmSize;
using sgns::runtime::WasmSpan;
using sgns::runtime::binaryen::RuntimeExternalInterface;
using sgns::storage::trie::PersistentTrieBatchMock;
using wasm::Element;
using wasm::Module;
using wasm::ModuleInstance;
using wasm::SExpressionParser;
using wasm::SExpressionWasmBuilder;

  std::ostream &operator<<(std::ostream &s,
                           const sgns::runtime::WasmResult &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<sgns::base::Buffer> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<void> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const boost::optional<unsigned int> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const boost::optional<std::shared_ptr<sgns::storage::trie::PersistentTrieBatch>> &data) {
    return s;
  }
/// extend Runtime external interface by adding wasm assertion functions
class TestableExternalInterface : public RuntimeExternalInterface {
 public:
  using RuntimeExternalInterface::RuntimeExternalInterface;

  wasm::Literal callImport(wasm::Function *import,
                           wasm::LiteralList &arguments) override {
    if (import->module == "env" && import->base == "assert") {
      EXPECT_TRUE(arguments.at(0).geti32());
      return wasm::Literal();
    }
    if (import->module == "env" && import->base == "assert_eq_i32") {
      EXPECT_EQ(arguments.at(0).geti32(), arguments.at(1).geti32());
      return wasm::Literal();
    }
    if (import->module == "env" && import->base == "assert_eq_i64") {
      EXPECT_EQ(arguments.at(0).geti64(), arguments.at(1).geti64());
      return wasm::Literal();
    }
    return RuntimeExternalInterface::callImport(import, arguments);
  }
};

class REITest : public ::testing::Test {
 public:
  void SetUp() override {
    memory_ = std::make_shared<MockMemory>();
    extension_ = std::make_unique<ExtensionMock>();
    extension_factory_ = std::make_shared<ExtensionFactoryMock>();
    storage_provider_ = std::make_shared<TrieStorageProviderMock>();
    EXPECT_CALL(*extension_factory_, createExtension(_, _))
        .WillRepeatedly(Invoke([this](auto&, auto&) -> std::unique_ptr<Extension> {
          if (extension_) {
            auto ext = std::move(extension_);
            extension_ = std::make_unique<ExtensionMock>();
            return std::unique_ptr<Extension>(std::move(ext));
          } else {
            extension_ = std::make_unique<ExtensionMock>();
            return std::unique_ptr<Extension>(
                std::make_unique<ExtensionMock>());
          }
        }));
  }

  void executeWasm(std::string call_code) {
    std::string code = (boost::format(wasm_template_) % call_code).str();

    // parse wast
    Module wasm{};

    // clang-8 doesn't know char * std::string::data(),
    // it returns only const char *
    char *data = const_cast<char *>(code.data());
    SExpressionParser parser(data);
    Element &root = *parser.root;
    ASSERT_GT(root.size(), 0);
    ASSERT_NE(root[0], nullptr);
    SExpressionWasmBuilder builder(wasm, *root[0]);
    EXPECT_CALL(*extension_, memory()).WillRepeatedly(Return(memory_));

    TestableExternalInterface rei(extension_factory_, storage_provider_);

    // interpret module
    ModuleInstance instance(wasm, &rei);
  }

 protected:
  std::shared_ptr<MockMemory> memory_;
  std::unique_ptr<ExtensionMock> extension_;
  std::shared_ptr<ExtensionFactoryMock> extension_factory_;
  std::shared_ptr<TrieStorageProviderMock> storage_provider_;

  // clang-format off
  const std::string wasm_template_ =
      "(module\n"
      "  (type (;0;) (func (param i32 i32)))\n"
      "  (type (;1;) (func (param i32)))\n"
      "  (type (;2;) (func (param i32 i32 i32) (result i32)))\n"
      "  (type (;3;) (func (param i32 i32) (result i32)))\n"
      "  (type (;4;) (func (param i32 i32 i32 i32 i32) (result i32)))\n"
      "  (type (;5;) (func (param i32 i32 i32)))\n"
      "  (type (;6;) (func (param i32 i32 i32 i32)))\n"
      "  (type (;7;) (func (param i64)))\n"
      "  (type (;8;) (func (param i32) (result i32)))\n"
      "  (type (;9;) (func (param i32 i32 i32 i32) (result i32)))\n"
      "  (type (;10;) (func (param i32 i32 i64 i32) (result i32)))\n"
      "  (type (;11;) (func))\n"
      "  (type (;12;) (func (param i32 i64 i64)))\n"
      "  (type (;13;) (func (param i64 i32) (result i32)))\n"
      "  (type (;14;) (func (param i32) (result i64)))\n"
      "  (type (;15;) (func (param i64 i32)))\n"
      "  (type (;16;) (func (param i32 i32 i64 i64)))\n"
      "  (type (;17;) (func (param i32 i64 i64 i64)))\n"
      "  (type (;18;) (func (param i64 i64)))\n"
      "  (type (;19;) (func (param i32 i32 i32 i32 i32)))\n"
      "  (type (;20;) (func (param i32 i64)))\n"
      "  (type (;21;) (func (param i32 i64 i32 i32 i64)))\n"
      "  (type (;22;) (func (param i32 i32 i32 i64 i64)))\n"
      "  (type (;23;) (func (param i32 i32) (result i64)))\n"
      "  (type (;24;) (func (param i32 i64 i64 i64 i64)))\n"
      "  (type (;25;) (func (param i32 i64 i64 i32)))\n"
      "  (type (;26;) (func (param i32 i64 i64 i64 i64 i32)))\n"
      "  (type (;27;) (func (result i64)))\n"
      "  (type (;28;) (func (param i32 i32 i32)))\n"
      "  (type (;29;) (func (param i64) (result i64)))\n"
      "  (type (;30;) (func (param i32 i64) (result i32)))\n"
      "  (type (;31;) (func (param i32 i32 i64) (result i64)))\n"
      "  (type (;32;) (func (param i32 i64 i32) (result i32)))\n"
      "  (type (;33;) (func (param i64 i64 i32) (result i64)))\n"
      "  (type (;34;) (func (param i64) (result i32)))\n"
      "  (type (;35;) (func (result i32)))\n"
      "  (import \"env\" \"ext_get_storage_into\" (func $ext_get_storage_into (type 4)))\n"
      "  (import \"env\" \"ext_get_allocated_storage\" (func $ext_get_allocated_storage (type 2)))\n"
      "  (import \"env\" \"ext_blake2_128\" (func $ext_blake2_128 (type 5)))\n"
      "  (import \"env\" \"ext_blake2_256\" (func $ext_blake2_256 (type 5)))\n"
      "  (import \"env\" \"ext_keccak_256\" (func $ext_keccak_256 (type 28)))\n"
      "  (import \"env\" \"ext_blake2_256_enumerated_trie_root\" (func $ext_blake2_256_enumerated_trie_root (type 6)))\n"
      "  (import \"env\" \"ext_print_utf8\" (func $ext_print_utf8 (type 0)))\n"
      "  (import \"env\" \"ext_print_num\" (func $ext_print_num (type 7)))\n"
      "  (import \"env\" \"ext_malloc\" (func $ext_malloc (type 8)))\n"
      "  (import \"env\" \"ext_free\" (func $ext_free (type 1)))\n"
      "  (import \"env\" \"ext_twox_128\" (func $ext_twox_128 (type 5)))\n"
      "  (import \"env\" \"ext_twox_256\" (func $ext_twox_256 (type 5)))\n"
      "  (import \"env\" \"ext_clear_storage\" (func $ext_clear_storage (type 0)))\n"
      "  (import \"env\" \"ext_set_storage\" (func $ext_set_storage (type 6)))\n"
      "  (import \"env\" \"ext_clear_prefix\" (func $ext_clear_prefix (type 0)))\n"
      "  (import \"env\" \"ext_exists_storage\" (func $ext_exists_storage (type 3)))\n"
      "  (import \"env\" \"ext_sr25519_verify\" (func $ext_sr25519_verify (type 9)))\n"
      "  (import \"env\" \"ext_ed25519_verify\" (func $ext_ed25519_verify (type 9)))\n"
      "  (import \"env\" \"ext_storage_root\" (func $ext_storage_root (type 1)))\n"
      "  (import \"env\" \"ext_storage_changes_root\" (func $ext_storage_changes_root (type 2)))\n"
      "  (import \"env\" \"ext_print_hex\" (func $ext_print_hex (type 0)))\n"
      "  (import \"env\" \"ext_logging_log_version_1\" (func $ext_logging_log_version_1 (type 12)))\n"
      "  (import \"env\" \"ext_chain_id\" (func $ext_chain_id (type 27)))\n"

      /// version 1
      "  (import \"env\" \"ext_crypto_ed25519_public_keys_version_1\" (func $ext_crypto_ed25519_public_keys_version_1 (type 29)))\n"
      "  (import \"env\" \"ext_crypto_ed25519_generate_version_1\" (func $ext_crypto_ed25519_generate_version_1 (type 30)))\n"
      "  (import \"env\" \"ext_crypto_ed25519_sign_version_1\" (func $ext_crypto_ed25519_sign_version_1 (type 31)))\n"
      "  (import \"env\" \"ext_crypto_ed25519_verify_version_1\" (func $ext_crypto_ed25519_verify_version_1 (type 32)))\n"
      "  (import \"env\" \"ext_crypto_sr25519_public_keys_version_1\" (func $ext_crypto_sr25519_public_keys_version_1 (type 29)))\n"
      "  (import \"env\" \"ext_crypto_sr25519_generate_version_1\" (func $ext_crypto_sr25519_generate_version_1 (type 30)))\n"
      "  (import \"env\" \"ext_crypto_sr25519_sign_version_1\" (func $ext_crypto_sr25519_sign_version_1 (type 31)))\n"
      "  (import \"env\" \"ext_crypto_sr25519_verify_version_2\" (func $ext_crypto_sr25519_verify_version_2 (type 32)))\n"
      "  (import \"env\" \"ext_crypto_secp256k1_ecdsa_recover_version_1\" (func $ext_crypto_secp256k1_ecdsa_recover_version_1 (type 31)))\n"
      "  (import \"env\" \"ext_crypto_secp256k1_ecdsa_recover_compressed_version_1\" (func $ext_crypto_secp256k1_ecdsa_recover_compressed_version_1 (type 31)))\n"

      /// hashing methods
      "  (import \"env\" \"ext_hashing_keccak_256_version_1\" (func $ext_hashing_keccak_256_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_hashing_sha2_256_version_1\" (func $ext_hashing_sha2_256_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_hashing_blake2_128_version_1\" (func $ext_hashing_blake2_128_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_hashing_blake2_256_version_1\" (func $ext_hashing_blake2_256_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_hashing_twox_256_version_1\" (func $ext_hashing_twox_256_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_hashing_twox_128_version_1\" (func $ext_hashing_twox_128_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_hashing_twox_64_version_1\" (func $ext_hashing_twox_64_version_1 (type 34)))\n"

      /// allocator methods
      "  (import \"env\" \"ext_allocator_malloc_version_1\" (func $ext_allocator_malloc_version_1 (type 8)))\n"
      "  (import \"env\" \"ext_allocator_free_version_1\" (func $ext_allocator_free_version_1 (type 1)))\n"

      /// storage methods
      "  (import \"env\" \"ext_storage_set_version_1\" (func $ext_storage_set_version_1 (type 18)))\n"
      "  (import \"env\" \"ext_storage_get_version_1\" (func $ext_storage_get_version_1 (type 29)))\n"
      "  (import \"env\" \"ext_storage_clear_version_1\" (func $ext_storage_clear_version_1 (type 7)))\n"
      "  (import \"env\" \"ext_storage_exists_version_1\" (func $ext_storage_exists_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_storage_read_version_1\" (func $ext_storage_read_version_1 (type 33)))\n"
      "  (import \"env\" \"ext_storage_clear_prefix_version_1\" (func $ext_storage_clear_prefix_version_1 (type 7)))\n"
      "  (import \"env\" \"ext_storage_changes_root_version_1\" (func $ext_storage_changes_root_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_storage_root_version_1\" (func $ext_storage_root_version_1 (type 35)))\n"
      "  (import \"env\" \"ext_storage_next_key_version_1\" (func $ext_storage_next_key_version_1 (type 29)))\n"

      /// trie methods
      "  (import \"env\" \"ext_trie_blake2_256_root_version_1\" (func $ext_trie_blake2_256_root_version_1 (type 34)))\n"
      "  (import \"env\" \"ext_trie_blake2_256_ordered_root_version_1\" (func $ext_trie_blake2_256_ordered_root_version_1 (type 34)))\n"

      /// assertions to check output in wasm
      "  (import \"env\" \"assert\" (func $assert (param i32)))\n"
      "  (import \"env\" \"assert_eq_i32\" (func $assert_eq_i32 (param i32 i32)))\n"
      "  (import \"env\" \"assert_eq_i64\" (func $assert_eq_i64 (param i64 i64)))\n"

      /// below is start function with import function call defined in test case
      "  (type $v (func))\n"
      "  (start $starter)\n"
      "  (func $starter (; 11 ;) (type 11)\n"
      "%s" // to plug actual call
      "  )\n"
      ")";
  // clang-format on
};

/**
 * For all tests:
 * @given runtime external interface with mocked externals
 * @when external function is invoked with provided arguments from WASM
 * @then corresponding host function is invoked with provided arguments
 */

TEST_F(REITest, ext_malloc_Test) {
  WasmSize size = 42;
  WasmPointer ptr = 123;
  EXPECT_CALL(*extension_, ext_malloc(size)).WillOnce(Return(ptr));
  auto execute_code = (boost::format("    (call $assert_eq_i32\n"
                                     "      (call $ext_malloc\n"
                                     "        (i32.const %d)\n"
                                     "      )\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % size % ptr)
                          .str();
  SCOPED_TRACE("ext_malloc_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_free_Test) {
  WasmPointer ptr = 123;
  EXPECT_CALL(*extension_, ext_free(ptr)).Times(1);
  auto execute_code = (boost::format("    (call $ext_free\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % ptr)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_clear_prefix_Test) {
  WasmPointer prefix_ptr = 123;
  WasmSize prefix_size = 1233;

  EXPECT_CALL(*extension_, ext_clear_prefix(prefix_ptr, prefix_size)).Times(1);
  auto execute_code = (boost::format("    (call $ext_clear_prefix\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % prefix_ptr % prefix_size)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_clear_storage_Test) {
  WasmPointer key_ptr = 123;
  WasmSize key_size = 1233;

  EXPECT_CALL(*extension_, ext_clear_storage(key_ptr, key_size)).Times(1);
  auto execute_code = (boost::format("    (call $ext_clear_storage\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % key_ptr % key_size)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_exists_storage_Test) {
  WasmPointer key_ptr = 123;
  WasmSize key_size = 1233;

  WasmSize expected_res = 1;

  EXPECT_CALL(*extension_, ext_exists_storage(key_ptr, key_size))
      .WillOnce(Return(expected_res));
  auto execute_code = (boost::format("    (call $assert_eq_i32\n"
                                     "      (call $ext_exists_storage\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "      )\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % key_ptr % key_size % expected_res)
                          .str();
  SCOPED_TRACE("ext_exists_storage_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_get_allocated_storage_Test) {
  WasmPointer key_ptr = 123;
  WasmSize key_size = 1233;
  WasmPointer len_ptr = 42;

  WasmPointer res_ptr = 1;

  EXPECT_CALL(*extension_,
              ext_get_allocated_storage(key_ptr, key_size, len_ptr))
      .WillOnce(Return(res_ptr));

  auto execute_code = (boost::format("    (call $assert_eq_i32\n"
                                     "      (call $ext_get_allocated_storage\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "      )\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % key_ptr % key_size % len_ptr % res_ptr)
                          .str();
  SCOPED_TRACE("ext_get_allocated_storage_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_get_storage_into_Test) {
  WasmPointer key_ptr = 123;
  WasmSize key_size = 1233;
  WasmPointer value_ptr = 42;
  WasmSize value_length = 321;
  WasmSize value_offset = 453;

  WasmSize res = 1;

  EXPECT_CALL(*extension_,
              ext_get_storage_into(
                  key_ptr, key_size, value_ptr, value_length, value_offset))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_get_storage_into\n"
                     "        (i32.const %d)\n"
                     "        (i32.const %d)\n"
                     "        (i32.const %d)\n"
                     "        (i32.const %d)\n"
                     "        (i32.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % key_ptr % key_size % value_ptr % value_length % value_offset % res)
          .str();
  SCOPED_TRACE("ext_get_allocated_storage_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_set_storage_Test) {
  WasmPointer key_ptr = 123;
  WasmSize key_size = 1233;

  WasmPointer value_ptr = 42;
  WasmSize value_size = 12;

  EXPECT_CALL(*extension_,
              ext_set_storage(key_ptr, key_size, value_ptr, value_size))
      .Times(1);
  auto execute_code = (boost::format("    (call $ext_set_storage\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % key_ptr % key_size % value_ptr % value_size)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_blake2_256_enumerated_trie_root_Test) {
  WasmPointer values_data = 12;
  WasmPointer lens_data = 42;
  WasmSize lens_length = 123;
  WasmPointer result = 321;

  EXPECT_CALL(*extension_,
              ext_blake2_256_enumerated_trie_root(
                  values_data, lens_data, lens_length, result))
      .Times(1);
  auto execute_code =
      (boost::format("    (call $ext_blake2_256_enumerated_trie_root\n"
                     "      (i32.const %d)\n"
                     "      (i32.const %d)\n"
                     "      (i32.const %d)\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % values_data % lens_data % lens_length % result)
          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_changes_root_Test) {
  WasmPointer parent_hash_data = 123;
  WasmSize parent_hash_len = 42;
  WasmPointer result = 321;

  WasmSize res = 1;

  EXPECT_CALL(*extension_, ext_storage_changes_root(parent_hash_data, result))
      .WillOnce(Return(res));

  auto execute_code = (boost::format("    (call $assert_eq_i32\n"
                                     "      (call $ext_storage_changes_root\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "      )\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % parent_hash_data % parent_hash_len % result % res)
                          .str();
  SCOPED_TRACE("ext_storage_changes_root_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_root_Test) {
  WasmPointer storage_root = 12;

  EXPECT_CALL(*extension_, ext_storage_root(storage_root)).Times(1);
  auto execute_code = (boost::format("    (call $ext_storage_root\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % storage_root)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_print_hex_Test) {
  WasmPointer data_ptr = 12;
  WasmSize data_size = 12;

  EXPECT_CALL(*extension_, ext_print_hex(data_ptr, data_size)).Times(1);
  auto execute_code = (boost::format("    (call $ext_print_hex\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % data_ptr % data_size)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_logging_log_version_1_Test) {
  WasmResult position(12, 12);
  const auto pos_packed = position.combine();
  WasmEnum ll = static_cast<WasmEnum>(WasmLogLevel::WasmLL_Error);

  EXPECT_CALL(*extension_,
              ext_logging_log_version_1(ll, pos_packed, pos_packed))
      .Times(1);
  auto execute_code = (boost::format("    (call $ext_logging_log_version_1\n"
                                     "      (i32.const %d)\n"
                                     "      (i64.const %d)\n"
                                     "      (i64.const %d)\n"
                                     "    )\n")
                       % ll % position.combine() % position.combine())
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_print_num_Test) {
  uint64_t num = 12;

  EXPECT_CALL(*extension_, ext_print_num(num)).Times(1);
  auto execute_code = (boost::format("    (call $ext_print_num\n"
                                     "      (i64.const %d)\n"
                                     "    )\n")
                       % num)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_print_utf8_Test) {
  WasmPointer data_ptr = 12;
  WasmSize data_size = 12;

  EXPECT_CALL(*extension_, ext_print_utf8(data_ptr, data_size)).Times(1);
  auto execute_code = (boost::format("    (call $ext_print_utf8\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % data_ptr % data_size)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_blake2_128_Test) {
  WasmPointer data_ptr = 12;
  WasmSize data_size = 12;
  WasmPointer out_ptr = 43;

  EXPECT_CALL(*extension_, ext_blake2_128(data_ptr, data_size, out_ptr))
      .Times(1);
  auto execute_code = (boost::format("    (call $ext_blake2_128\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % data_ptr % data_size % out_ptr)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_blake_256_Test) {
  WasmPointer data_ptr = 12;
  WasmSize data_size = 12;
  WasmPointer out_ptr = 43;

  EXPECT_CALL(*extension_, ext_blake2_256(data_ptr, data_size, out_ptr))
      .Times(1);
  auto execute_code = (boost::format("    (call $ext_blake2_256\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % data_ptr % data_size % out_ptr)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_keccak_256_Test) {
  WasmPointer data_ptr = 12;
  WasmSize data_size = 12;
  WasmPointer out_ptr = 43;

  EXPECT_CALL(*extension_, ext_keccak_256(data_ptr, data_size, out_ptr))
      .Times(1);
  auto execute_code = (boost::format("    (call $ext_keccak_256\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % data_ptr % data_size % out_ptr)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_ed25519_verify_Test) {
  WasmPointer msg_data = 123;
  WasmSize msg_len = 1233;
  WasmPointer sig_data = 42;
  WasmPointer pubkey_data = 321;

  WasmSize res = 1;

  EXPECT_CALL(*extension_,
              ext_ed25519_verify(msg_data, msg_len, sig_data, pubkey_data))
      .WillOnce(Return(res));

  auto execute_code = (boost::format("    (call $assert_eq_i32\n"
                                     "      (call $ext_ed25519_verify\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "      )\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % msg_data % msg_len % sig_data % pubkey_data % res)
                          .str();
  SCOPED_TRACE("ext_ed25519_verify_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_sr25519_verify_Test) {
  WasmPointer msg_data = 123;
  WasmSize msg_len = 1233;
  WasmPointer sig_data = 42;
  WasmPointer pubkey_data = 321;

  WasmSize res = 0;

  EXPECT_CALL(*extension_,
              ext_sr25519_verify(msg_data, msg_len, sig_data, pubkey_data))
      .WillOnce(Return(res));

  auto execute_code = (boost::format("    (call $assert_eq_i32\n"
                                     "      (call $ext_sr25519_verify\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "        (i32.const %d)\n"
                                     "      )\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % msg_data % msg_len % sig_data % pubkey_data % res)
                          .str();
  SCOPED_TRACE("ext_sr25519_verify_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_ed25519_public_keys_v1_Test) {
  WasmSize key_type = kProd;
  WasmSpan res = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_ed25519_public_keys_v1(key_type))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i64\n"
                     "      (call $ext_crypto_ed25519_public_keys_version_1\n"
                     "        (i32.const %d)\n"
                     "      )\n"
                     "      (i64.const %d)\n"
                     "    )\n")
       % key_type % res)
          .str();
  SCOPED_TRACE("ext_ed25519_public_keys_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_ed25519_generate_v1_Test) {
  WasmSize key_type = kProd;
  WasmSpan seed = WasmResult(1, 2).combine();

  WasmPointer res = 4;

  EXPECT_CALL(*extension_, ext_ed25519_generate_v1(key_type, seed))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_crypto_ed25519_generate_version_1\n"
                     "        (i32.const %d)\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % key_type % seed % res)
          .str();
  SCOPED_TRACE("ext_ed25519_generate_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_ed25519_sign_v1_Test) {
  WasmSize key_type = kProd;
  WasmPointer key = 1;
  WasmSpan msg = WasmResult(33, 2).combine();
  WasmSpan res = WasmResult(35, 25).combine();

  EXPECT_CALL(*extension_, ext_ed25519_sign_v1(key_type, key, msg))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i64\n"
                     "      (call $ext_crypto_ed25519_sign_version_1\n"
                     "        (i32.const %d)\n"
                     "        (i32.const %d)\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i64.const %d)\n"
                     "    )\n")
       % key_type % key % msg % res)
          .str();
  SCOPED_TRACE("ext_ed25519_generate_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_ed25519_verify_v1_Test) {
  WasmPointer msg_data = 123;
  WasmSize msg_len = 1233;
  WasmSpan msg = WasmResult(msg_data, msg_len).combine();
  WasmPointer sig_data = 42;
  WasmPointer pubkey_data = 321;

  WasmSize res = 1;

  EXPECT_CALL(*extension_, ext_ed25519_verify_v1(sig_data, msg, pubkey_data))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_crypto_ed25519_verify_version_1\n"
                     "        (i32.const %d)\n"
                     "        (i64.const %d)\n"
                     "        (i32.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % sig_data % msg % pubkey_data % res)
          .str();
  SCOPED_TRACE("ext_ed25519_verify_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_sr25519_public_keys_v1_Test) {
  WasmSize key_type = kProd;

  WasmSpan res = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_sr25519_public_keys_v1(key_type))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i64\n"
                     "      (call $ext_crypto_sr25519_public_keys_version_1\n"
                     "        (i32.const %d)\n"
                     "      )\n"
                     "      (i64.const %d)\n"
                     "    )\n")
       % key_type % res)
          .str();
  SCOPED_TRACE("ext_sr25519_public_keys_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_sr25519_generate_v1_Test) {
  WasmSize key_type = kProd;
  WasmSpan seed = WasmResult(1, 2).combine();

  WasmPointer res = 4;

  EXPECT_CALL(*extension_, ext_sr25519_generate_v1(key_type, seed))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_crypto_sr25519_generate_version_1\n"
                     "        (i32.const %d)\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % key_type % seed % res)
          .str();
  SCOPED_TRACE("ext_sr25519_generate_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_sr25519_sign_v1_Test) {
  WasmSize key_type = kProd;
  WasmPointer key = 1;
  WasmSpan msg = WasmResult(33, 2).combine();
  WasmSpan res = WasmResult(35, 25).combine();

  EXPECT_CALL(*extension_, ext_sr25519_sign_v1(key_type, key, msg))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i64\n"
                     "      (call $ext_crypto_sr25519_sign_version_1\n"
                     "        (i32.const %d)\n"
                     "        (i32.const %d)\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i64.const %d)\n"
                     "    )\n")
       % key_type % key % msg % res)
          .str();
  SCOPED_TRACE("ext_sr25519_sign_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_sr25519_verify_v2_Test) {
  WasmPointer msg_data = 123;
  WasmSize msg_len = 1233;
  WasmSpan msg = WasmResult(msg_data, msg_len).combine();
  WasmPointer sig_data = 42;
  WasmPointer pubkey_data = 321;

  WasmSize res = 1;

  EXPECT_CALL(*extension_, ext_sr25519_verify_v1(sig_data, msg, pubkey_data))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_crypto_sr25519_verify_version_2\n"
                     "        (i32.const %d)\n"
                     "        (i64.const %d)\n"
                     "        (i32.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % sig_data % msg % pubkey_data % res)
          .str();
  SCOPED_TRACE("ext_sr25519_verify_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_twox_128_Test) {
  WasmPointer data_ptr = 12;
  WasmSize data_size = 12;
  WasmPointer out_ptr = 43;

  EXPECT_CALL(*extension_, ext_twox_128(data_ptr, data_size, out_ptr)).Times(1);
  auto execute_code = (boost::format("    (call $ext_twox_128\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % data_ptr % data_size % out_ptr)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_twox_256_Test) {
  WasmPointer data_ptr = 12;
  WasmSize data_size = 12;
  WasmPointer out_ptr = 43;

  EXPECT_CALL(*extension_, ext_twox_256(data_ptr, data_size, out_ptr)).Times(1);
  auto execute_code = (boost::format("    (call $ext_twox_256\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % data_ptr % data_size % out_ptr)
                          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_chain_id_Test) {
  uint64_t res = 123141;

  EXPECT_CALL(*extension_, ext_chain_id()).WillOnce(Return(res));

  auto execute_code = (boost::format("    (call $assert_eq_i64\n"
                                     "      (call $ext_chain_id)\n"
                                     "      (i64.const %d)\n"
                                     "    )\n")
                       % res)
                          .str();
  SCOPED_TRACE("ext_chain_id_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_crypto_secp256k1_ecdsa_recover_version_1_Test) {
  WasmPointer sig_ptr = 12;
  WasmPointer msg_ptr = 77;
  WasmSpan out_span = WasmResult{109, 41}.combine();

  EXPECT_CALL(*extension_,
              ext_crypto_secp256k1_ecdsa_recover_v1(sig_ptr, msg_ptr))
      .WillOnce(Return(out_span));
  auto execute_code =
      (boost::format("(call $assert_eq_i64\n"
                     "    (call $ext_crypto_secp256k1_ecdsa_recover_version_1\n"
                     "      (i32.const %d)\n"
                     "      (i32.const %d)\n"
                     "    )\n"
                     "    (i64.const %d)\n"
                     ")")
       % sig_ptr % msg_ptr % out_span)
          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_crypto_secp256k1_ecdsa_recover_compressed_version_1_Test) {
  WasmPointer sig_ptr = 12;
  WasmPointer msg_ptr = 77;
  WasmSpan out_span = WasmResult{109, 41}.combine();

  EXPECT_CALL(
      *extension_,
      ext_crypto_secp256k1_ecdsa_recover_compressed_v1(sig_ptr, msg_ptr))
      .WillOnce(Return(out_span));
  auto execute_code =
      (boost::format(
           "(call $assert_eq_i64\n"
           "    (call "
           "$ext_crypto_secp256k1_ecdsa_recover_compressed_version_1\n"
           "      (i32.const %d)\n"
           "      (i32.const %d)\n"
           "    )\n"
           "    (i64.const %d)\n"
           ")")
       % sig_ptr % msg_ptr % out_span)
          .str();
  executeWasm(execute_code);
}

TEST_F(REITest, ext_hashing_keccak_256_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_hashing_keccak_256_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_hashing_keccak_256_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_hashing_keccak_256_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_hashing_sha2_256_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_hashing_sha2_256_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_hashing_sha2_256_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_hashing_sha2_256_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_hashing_blake2_128_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_hashing_blake2_128_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_hashing_blake2_128_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_hashing_blake2_128_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_hashing_blake2_256_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_hashing_blake2_256_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_hashing_blake2_256_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_hashing_blake2_256_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_hashing_twox_256_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_hashing_twox_256_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_hashing_twox_256_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_hashing_twox_256_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_hashing_twox_128_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_hashing_twox_128_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_hashing_twox_128_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_hashing_twox_128_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_hashing_twox_64_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_hashing_twox_64_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_hashing_twox_64_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_hashing_twox_64_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_allocator_malloc_version_1_Test) {
  WasmSize size = 42;
  WasmPointer ptr = 123;
  EXPECT_CALL(*extension_, ext_allocator_malloc_version_1(size))
      .WillOnce(Return(ptr));
  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_allocator_malloc_version_1\n"
                     "        (i32.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % size % ptr)
          .str();
  SCOPED_TRACE("ext_allocator_malloc_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_allocator_free_version_1_Test) {
  WasmPointer ptr = 123;
  EXPECT_CALL(*extension_, ext_allocator_free_version_1(ptr)).Times(1);
  auto execute_code = (boost::format("    (call $ext_allocator_free_version_1\n"
                                     "      (i32.const %d)\n"
                                     "    )\n")
                       % ptr)
                          .str();
  SCOPED_TRACE("ext_allocator_free_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_set_version_1_Test) {
  WasmSpan param1 = WasmResult(1, 2).combine();
  WasmSpan param2 = WasmResult(3, 4).combine();
  EXPECT_CALL(*extension_, ext_storage_set_version_1(param1, param2))
      .WillOnce(Return());

  auto execute_code = (boost::format("      (call $ext_storage_set_version_1\n"
                                     "        (i64.const %d)\n"
                                     "        (i64.const %d)\n"
                                     "      )\n")
                       % param1 % param2)
                          .str();
  SCOPED_TRACE("ext_storage_set_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_get_version_1_Test) {
  WasmSize key_type = kProd;
  WasmSpan res = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_storage_get_version_1(key_type))
      .WillOnce(Return(res));

  auto execute_code = (boost::format("    (call $assert_eq_i64\n"
                                     "      (call $ext_storage_get_version_1\n"
                                     "        (i64.const %d)\n"
                                     "      )\n"
                                     "      (i64.const %d)\n"
                                     "    )\n")
                       % key_type % res)
                          .str();
  SCOPED_TRACE("ext_storage_get_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_clear_version_1_Test) {
  uint64_t num = 12;

  EXPECT_CALL(*extension_, ext_storage_clear_version_1(num)).Times(1);
  auto execute_code = (boost::format("    (call $ext_storage_clear_version_1\n"
                                     "      (i64.const %d)\n"
                                     "    )\n")
                       % num)
                          .str();
  SCOPED_TRACE("ext_storage_clear_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_exists_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_storage_exists_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_storage_exists_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_storage_exists_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_read_version_1_Test) {
  WasmResult key(123, 1233);
  WasmResult value(42, 12);
  WasmOffset offset(1);
  WasmSpan res = WasmResult(1, 2).combine();

  EXPECT_CALL(
      *extension_,
      ext_storage_read_version_1(key.combine(), value.combine(), offset))
      .WillOnce(Return(res));
  auto execute_code = (boost::format("    (call $assert_eq_i64\n"
                                     "    (call $ext_storage_read_version_1\n"
                                     "      (i64.const %d)\n"
                                     "      (i64.const %d)\n"
                                     "      (i32.const %d)\n"
                                     "    )\n"
                                     "      (i64.const %d)\n"
                                     "    )\n")
                       % key.combine() % value.combine() % offset % res)
                          .str();
  SCOPED_TRACE("ext_storage_read_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_clear_prefix_version_1_Test) {
  uint64_t num = 12;

  EXPECT_CALL(*extension_, ext_storage_clear_prefix_version_1(num)).Times(1);
  auto execute_code =
      (boost::format("    (call $ext_storage_clear_prefix_version_1\n"
                     "      (i64.const %d)\n"
                     "    )\n")
       % num)
          .str();
  SCOPED_TRACE("ext_storage_clear_prefix_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_changes_root_version_1_Test) {
  WasmSize key_type = kProd;
  WasmPointer res = 2;

  EXPECT_CALL(*extension_, ext_storage_changes_root_version_1(key_type))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_storage_changes_root_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % key_type % res)
          .str();
  SCOPED_TRACE("ext_storage_changes_root_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_root_version_1_Test) {
  uint32_t res = 123141;

  EXPECT_CALL(*extension_, ext_storage_root_version_1()).WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_storage_root_version_1)\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % res)
          .str();
  SCOPED_TRACE("ext_storage_root_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_storage_next_key_version_1_Test) {
  WasmSpan param = 5678;
  WasmSpan res = 123141;

  EXPECT_CALL(*extension_, ext_storage_next_key_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i64\n"
                     "      (call $ext_storage_next_key_version_1"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i64.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_storage_next_key_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_trie_blake2_256_root_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_trie_blake2_256_root_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_trie_blake2_256_root_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_trie_blake2_256_root_version_1_Test");
  executeWasm(execute_code);
}

TEST_F(REITest, ext_trie_blake2_256_ordered_root_version_1_Test) {
  WasmPointer res = 3;
  WasmSpan param = WasmResult(1, 2).combine();

  EXPECT_CALL(*extension_, ext_trie_blake2_256_ordered_root_version_1(param))
      .WillOnce(Return(res));

  auto execute_code =
      (boost::format("    (call $assert_eq_i32\n"
                     "      (call $ext_trie_blake2_256_ordered_root_version_1\n"
                     "        (i64.const %d)\n"
                     "      )\n"
                     "      (i32.const %d)\n"
                     "    )\n")
       % param % res)
          .str();
  SCOPED_TRACE("ext_trie_blake2_256_ordered_root_version_1_Test");
  executeWasm(execute_code);
}
