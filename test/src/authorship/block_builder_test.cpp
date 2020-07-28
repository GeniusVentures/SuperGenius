
#include "authorship/impl/block_builder_impl.hpp"

#include <gtest/gtest.h>
#include "mock/src/runtime/block_builder_api_mock.hpp"
#include "testutil/outcome.hpp"

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Return;

using sgns::authorship::BlockBuilderImpl;
using sgns::primitives::ApplyOutcome;
using sgns::primitives::Block;
using sgns::primitives::BlockHeader;
using sgns::primitives::BlockNumber;
using sgns::primitives::Extrinsic;
using sgns::primitives::InherentData;
using sgns::runtime::BlockBuilderApiMock;

namespace sgns::primitives {
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<void> &test_struct) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<CheckInherentsResult> &test_struct) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<std::vector<Extrinsic>> &test_struct) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<BlockHeader> &test_struct) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s, const outcome::result<boost::variant<ApplyOutcome, ApplyError>> &test_struct) {
    return s ;
  }
  std::ostream &operator<<(std::ostream &s, const outcome::result<sgns::base::Blob<32>> &test_struct) {
    return s ;
  }
  std::ostream &operator<<(std::ostream &s, const outcome::result<void> &test_struct) {
    return s ;
  }
  
}  // namespace sgns::primitives

class BlockBuilderTest : public ::testing::Test {
 public:
  void SetUp() override {
    // add some number to the header to make it possible to differentiate it
    expected_header_.number = number_;

    block_builder_ = std::make_shared<BlockBuilderImpl>(expected_header_,
                                                        block_builder_api_);
  }

 protected:
  std::shared_ptr<BlockBuilderApiMock> block_builder_api_ =
      std::make_shared<BlockBuilderApiMock>();

  BlockHeader expected_header_;
  BlockNumber number_ = 123;

  std::shared_ptr<BlockBuilderImpl> block_builder_;
};

/**
 * @given BlockBuilderApi that fails to apply extrinsic @and BlockBuilder that
 * uses that BlockBuilderApi
 * @when BlockBuilder tries to push extrinsic @and BlockBuilder bakes a block
 * @then push fails @and created block is empty
 */
TEST_F(BlockBuilderTest, PushWhenApplyFails) {
  // given
  Extrinsic xt{};
  EXPECT_CALL(*block_builder_api_, apply_extrinsic(xt))
      .WillOnce(Return(outcome::failure(boost::system::error_code{})));
  EXPECT_CALL(*block_builder_api_, finalise_block())
      .WillOnce(Return(expected_header_));

  // when
  auto res = block_builder_->pushExtrinsic(xt);
  EXPECT_OUTCOME_TRUE(block, block_builder_->bake());

  // then
  ASSERT_FALSE(res);
  ASSERT_THAT(block.body, IsEmpty());
}

/**
 * @given BlockBuilderApi that returns false to apply extrinsic @and
 * BlockBuilder that uses that BlockBuilderApi
 * @when BlockBuilder tries to push extrinsic @and BlockBuilder bakes a block
 * @then Extrinsic is not added to the baked block
 */
TEST_F(BlockBuilderTest, PushWhenApplySucceedsWithTrue) {
  // given
  Extrinsic xt{};
  EXPECT_CALL(*block_builder_api_, apply_extrinsic(xt))
      .WillOnce(Return(ApplyOutcome::SUCCESS));
  EXPECT_CALL(*block_builder_api_, finalise_block())
      .WillOnce(Return(expected_header_));

  // when
  auto res = block_builder_->pushExtrinsic(xt);
  ASSERT_TRUE(res);

  EXPECT_OUTCOME_TRUE(block, block_builder_->bake());

  // then
  ASSERT_EQ(block.header, expected_header_);
  ASSERT_THAT(block.body, ElementsAre(xt));
}

/**
 * @given BlockBuilderApi that returns true to apply extrinsic @and
 * BlockBuilder that uses that BlockBuilderApi
 * @when BlockBuilder tries to push extrinsic @and BlockBuilder bakes a block
 * @then Extrinsic is not added to the baked block
 */
TEST_F(BlockBuilderTest, PushWhenApplySucceedsWithFalse) {
  // given
  Extrinsic xt{};
  EXPECT_CALL(*block_builder_api_, apply_extrinsic(xt))
      .WillOnce(Return(ApplyOutcome::FAIL));
  EXPECT_CALL(*block_builder_api_, finalise_block())
      .WillOnce(Return(expected_header_));

  // when
  auto res = block_builder_->pushExtrinsic(xt);

  // then
  ASSERT_FALSE(res);
  EXPECT_OUTCOME_TRUE(block, block_builder_->bake());
  ASSERT_EQ(block.header, expected_header_);
  ASSERT_THAT(block.body, IsEmpty());
}
