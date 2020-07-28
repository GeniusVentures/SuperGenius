
#include <gtest/gtest.h>

#include "authorship/impl/block_builder_factory_impl.hpp"
#include "mock/src/blockchain/block_header_repository_mock.hpp"
#include "mock/src/runtime/block_builder_api_mock.hpp"
#include "mock/src/runtime/core_mock.hpp"
#include "testutil/outcome.hpp"

using ::testing::Return;

using sgns::authorship::BlockBuilderFactoryImpl;
using sgns::blockchain::BlockHeaderRepositoryMock;
using sgns::primitives::BlockHeader;
using sgns::primitives::BlockId;
using sgns::primitives::BlockNumber;
using sgns::primitives::Digest;
using sgns::primitives::PreRuntime;
using sgns::runtime::BlockBuilderApiMock;
using sgns::runtime::CoreMock;
namespace sgns::primitives {
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<void> &test_struct) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<CheckInherentsResult> &test_struct) {
    return s;
  }
}  // namespace sgns::primitives

class BlockBuilderFactoryTest : public ::testing::Test {
 public:
  void SetUp() override {
    parent_hash_.fill(0);
    parent_id_ = parent_hash_;

    expected_header_.parent_hash = parent_hash_;
    expected_header_.number = expected_number_;
    expected_header_.digest = inherent_digests_;

    EXPECT_CALL(*header_backend_, getNumberByHash(parent_hash_))
        .WillOnce(Return(parent_number_));
  }

  std::shared_ptr<CoreMock> core_ = std::make_shared<CoreMock>();
  std::shared_ptr<BlockBuilderApiMock> block_builder_api_ =
      std::make_shared<BlockBuilderApiMock>();
  std::shared_ptr<BlockHeaderRepositoryMock> header_backend_ =
      std::make_shared<BlockHeaderRepositoryMock>();

  BlockNumber parent_number_{41};
  BlockNumber expected_number_{parent_number_ + 1};
  sgns::base::Hash256 parent_hash_;
  BlockId parent_id_;
  Digest inherent_digests_{{PreRuntime{}}};
  BlockHeader expected_header_;
};

/**
 * @given HeaderBackend providing expected_hash and expected_number of the
 * block, which become part of expected_block_header
 * @when core runtime successfully initialises expected_block_header
 * @then BlockBuilderFactory that uses this core runtime and HeaderBackend
 * successfully creates BlockBuilder
 */
TEST_F(BlockBuilderFactoryTest, CreateSuccessful) {
  // given
  EXPECT_CALL(*core_, initialise_block(expected_header_))
      .WillOnce(Return(outcome::success()));
  BlockBuilderFactoryImpl factory(core_, block_builder_api_, header_backend_);

  // when
  auto block_builder_res = factory.create(parent_id_, inherent_digests_);

  // then
  ASSERT_TRUE(block_builder_res);
}

/**
 * @given HeaderBackend providing expected_hash and expected_number of the
 * block, which become part of expected_block_header
 * @when core runtime does not initialise expected_block_header
 * @then BlockBuilderFactory that uses this core runtime and HeaderBackend
 * does not create BlockBuilder
 */
TEST_F(BlockBuilderFactoryTest, CreateFailed) {
  // given
  EXPECT_CALL(*core_, initialise_block(expected_header_))
      .WillOnce(Return(outcome::failure(boost::system::error_code{})));
  BlockBuilderFactoryImpl factory(core_, block_builder_api_, header_backend_);

  // when
  auto block_builder_res = factory.create(parent_id_, inherent_digests_);

  // then
  ASSERT_FALSE(block_builder_res);
}
