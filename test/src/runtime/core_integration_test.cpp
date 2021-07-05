
#include <gtest/gtest.h>

#include <boost/filesystem.hpp>
#include <fstream>

#include "src/runtime/runtime_test.hpp"
#include "mock/src/blockchain/block_header_repository_mock.hpp"
#include "mock/src/storage/trie/trie_storage_mock.hpp"
#include "runtime/binaryen/runtime_api/core_impl.hpp"

using sgns::blockchain::BlockHeaderRepositoryMock;
using sgns::base::Buffer;
using sgns::extensions::ExtensionFactoryImpl;
using sgns::primitives::Block;
using sgns::primitives::BlockHeader;
using sgns::primitives::BlockId;
using sgns::primitives::BlockNumber;
using sgns::primitives::Extrinsic;
using sgns::runtime::WasmMemory;
using sgns::runtime::binaryen::CoreImpl;
using sgns::runtime::binaryen::WasmMemoryImpl;

using ::testing::_;
using ::testing::Return;
  std::ostream &operator<<(std::ostream &s,
                           const sgns::storage::changes_trie::ChangesTrieConfig &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<sgns::primitives::BlockHeader> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<sgns::base::Blob<32>> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<sgns::base::Buffer> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<sgns::blockchain::BlockStatus> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<void> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<uint64_t> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const std::function<outcome::result<sgns::base::Buffer>(void)> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const boost::optional<std::shared_ptr<sgns::storage::trie::PersistentTrieBatch>> &data) {
    return s;
  }
namespace fs = boost::filesystem;

class CoreTest : public RuntimeTest {
 public:
  void SetUp() override {
    RuntimeTest::SetUp();

    core_ = std::make_shared<CoreImpl>(
        wasm_provider_,
        runtime_manager_,
        changes_tracker_,
        std::make_shared<BlockHeaderRepositoryMock>());
  }

 protected:
  std::shared_ptr<CoreImpl> core_;
};

/**
 * @given initialized core api
 * @when version is invoked
 * @then successful result is returned
 */
TEST_F(CoreTest, VersionTest) {
  ASSERT_TRUE(core_->version(boost::none));
}

/**
 * @given initialized core api
 * @when execute_block is invoked
 * @then successful result is returned
 */
TEST_F(CoreTest, DISABLED_ExecuteBlockTest) {
  auto block = createBlock();

  ASSERT_TRUE(core_->execute_block(block));
}

/**
 * @given initialised core api
 * @when initialise_block is invoked
 * @then successful result is returned
 */
TEST_F(CoreTest, DISABLED_InitializeBlockTest) {
  auto header = createBlockHeader();

  ASSERT_TRUE(core_->initialise_block(header));
}

/**
 * @given initialized core api
 * @when authorities is invoked
 * @then successful result is returned
 */
TEST_F(CoreTest, DISABLED_AuthoritiesTest) {
  BlockId block_id = 0;
  ASSERT_TRUE(core_->authorities(block_id));
}
