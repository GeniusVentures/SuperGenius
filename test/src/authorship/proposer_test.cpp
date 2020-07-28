
#include "authorship/impl/proposer_impl.hpp"

#include <gtest/gtest.h>
#include "mock/src/authorship/block_builder_factory_mock.hpp"
#include "mock/src/authorship/block_builder_mock.hpp"
#include "mock/src/runtime/block_builder_api_mock.hpp"
#include "mock/src/transaction_pool/transaction_pool_mock.hpp"
#include "testutil/literals.hpp"
#include "testutil/outcome.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Test;

using sgns::authorship::BlockBuilder;
using sgns::authorship::BlockBuilderFactoryMock;
using sgns::authorship::BlockBuilderMock;
using sgns::authorship::ProposerImpl;
using sgns::base::Buffer;
using sgns::primitives::Block;
using sgns::primitives::BlockId;
using sgns::primitives::BlockNumber;
using sgns::primitives::Digest;
using sgns::primitives::Extrinsic;
using sgns::primitives::InherentData;
using sgns::primitives::InherentIdentifier;
using sgns::primitives::PreRuntime;
using sgns::primitives::Transaction;
using sgns::runtime::BlockBuilderApiMock;
using sgns::transaction_pool::TransactionPoolMock;

// TODO (kamilsa): workaround unless we bump gtest version to 1.8.1+

std::ostream &operator<<(std::ostream &s,
                        const outcome::result<sgns::primitives::BlockHash> &test_struct) {
return s;
}


class ProposerTest : public ::testing::Test {
 public:
  /**
   * Sets default behavior for BlockBuilderFactory and BlockBuilderApi:
   * BlockBuilderFactory creates BlockBuilderMock
   * BlockBuilerApi returns predefined InherentExtrinsics
   */
  void SetUp() override {
    ASSERT_TRUE(inherent_data_.putData(InherentIdentifier{}, Buffer{1, 2, 3}));

    block_builder_ = new BlockBuilderMock();
    EXPECT_CALL(*block_builder_factory_,
                createProxy(expected_block_id_, inherent_digests_))
        .WillOnce(Return(block_builder_));

    EXPECT_CALL(*block_builder_api_mock_, inherent_extrinsics(inherent_data_))
        .WillOnce(Return(inherent_xts));
  }

 protected:
  std::shared_ptr<BlockBuilderFactoryMock> block_builder_factory_ =
      std::make_shared<BlockBuilderFactoryMock>();
  std::shared_ptr<TransactionPoolMock> transaction_pool_ =
      std::make_shared<TransactionPoolMock>();
  std::shared_ptr<BlockBuilderApiMock> block_builder_api_mock_ =
      std::make_shared<BlockBuilderApiMock>();

  BlockBuilderMock *block_builder_;

  ProposerImpl proposer_{
      block_builder_factory_, transaction_pool_, block_builder_api_mock_};

  BlockNumber expected_number_{42};
  BlockId expected_block_id_{expected_number_};

  Digest inherent_digests_{PreRuntime{}};

  InherentData inherent_data_;
  std::vector<Extrinsic> inherent_xts{Extrinsic{{3, 4, 5}}};
  Block expected_block{{}, {{{5, 4, 3}}}};
};

/**
 * @given BlockBuilderApi creating inherent extrinsics @and TransactionPool
 * returning extrinsics
 * @when Proposer created from these BlockBuilderApi and TransactionPool is
 * trying to create block @and deadline is bigger than current time
 * @then Block is created and it is equal to the block baked in BlockBuilder
 */
TEST_F(ProposerTest, CreateBlockSuccess) {
  // given
  // we push 2 xts: 1 from inherent_xts and one from transaction pool
  EXPECT_CALL(*block_builder_, pushExtrinsic(_))
      .WillOnce(Return(outcome::success()))
      .WillOnce(Return(outcome::success()));

  // getReadyTransaction will return vector with single transaction
  std::map<Transaction::Hash, std::shared_ptr<Transaction>> ready_transactions{
      std::make_pair("fakeHash"_hash256, std::make_shared<Transaction>())};

  EXPECT_CALL(*transaction_pool_, getReadyTransactions())
      .WillOnce(Return(ready_transactions));

  EXPECT_CALL(*transaction_pool_, removeOne("fakeHash"_hash256))
      .WillOnce(Return(outcome::success()));

  EXPECT_CALL(*block_builder_, bake()).WillOnce(Return(expected_block));

  // when
  auto block_res =
      proposer_.propose(expected_block_id_, inherent_data_, inherent_digests_);

  // then
  ASSERT_TRUE(block_res);
  ASSERT_EQ(expected_block, block_res.value());
}

/**
 * @given BlockBuilderApi creating inherent extrinsics @and TransactionPool
 * returning extrinsics @and BlockBuilder that cannot accept extrinsics
 * @when Proposer created from these BlockBuilderApi and TransactionPool is
 * trying to create block
 * @then Block is not created
 */
TEST_F(ProposerTest, CreateBlockFailsWhenXtNotPushed) {
  // given
  EXPECT_CALL(*block_builder_, pushExtrinsic(inherent_xts[0]))
      .WillOnce(Return(outcome::failure(boost::system::error_code{})));

  // when
  auto block_res =
      proposer_.propose(expected_block_id_, inherent_data_, inherent_digests_);

  // then
  ASSERT_FALSE(block_res);
}

/**
 * @given BlockBuilderApi creating inherent extrinsics @and TransactionPool
 * returning extrinsics
 * @when Proposer created from these BlockBuilderApi and TransactionPool is
 * trying to create block @but push extrinsics to block builder failed
 * @then Block is not created
 */
TEST_F(ProposerTest, PushFailed) {
  // given
  // we push 1 xt from inherent_xts and 1 Xt from transaction pool. Second push
  // fails
  EXPECT_CALL(*block_builder_, pushExtrinsic(_))
      .WillOnce(Return(outcome::success()))  // for inherent xt
      .WillOnce(Return(outcome::failure(
          boost::system::error_code{})));  // for xt from tx pool

  std::map<Transaction::Hash, std::shared_ptr<Transaction>> ready_transactions{
      std::make_pair("fakeHash"_hash256, std::make_shared<Transaction>())};

  EXPECT_CALL(*transaction_pool_, getReadyTransactions())
      .WillOnce(Return(ready_transactions));

  // when
  auto block_res =
      proposer_.propose(expected_block_id_, inherent_data_, inherent_digests_);

  // then
  ASSERT_FALSE(block_res);
}
