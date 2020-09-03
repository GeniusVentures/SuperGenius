

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <memory>

#include "clock/impl/clock_impl.hpp"
#include "verification/production/production_error.hpp"
#include "mock/src/application/app_state_manager_mock.hpp"
#include "verification/production/impl/production_impl.hpp"
#include "mock/src/authorship/proposer_mock.hpp"
#include "mock/src/blockchain/block_tree_mock.hpp"
#include "mock/src/clock/clock_mock.hpp"
#include "mock/src/clock/timer_mock.hpp"
#include "mock/src/verification/authority/authority_update_observer_mock.hpp"
#include "mock/src/verification/production/production_gossiper_mock.hpp"
#include "mock/src/verification/production/production_synchronizer_mock.hpp"
#include "mock/src/verification/production/epoch_storage_mock.hpp"
#include "mock/src/verification/production_lottery_mock.hpp"
#include "mock/src/verification/finality/finality_mock.hpp"
#include "mock/src/verification/validation/block_validator_mock.hpp"
#include "mock/src/crypto/hasher_mock.hpp"
#include "mock/src/runtime/production_api_mock.hpp"
#include "mock/src/runtime/core_mock.hpp"
#include "mock/src/storage/trie/trie_storage_mock.hpp"
#include "mock/src/transaction_pool/transaction_pool_mock.hpp"
#include "primitives/block.hpp"
#include "testutil/literals.hpp"
#include "testutil/sr25519_utils.hpp"

using namespace sgns;
using namespace authority;
using namespace verification;
using namespace application;
using namespace blockchain;
using namespace authorship;
using namespace crypto;
using namespace primitives;
using namespace clock;
using namespace base;
using namespace network;

using testing::_;
using testing::A;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using std::chrono_literals::operator""ms;

Hash256 createHash(uint8_t byte) {
  Hash256 h;
  h.fill(byte);
  return h;
}

// TODO (kamilsa): workaround unless we bump gtest version to 1.8.1+
namespace sgns::primitives {
  std::ostream &operator<<(std::ostream &s,
                           const detail::DigestItemCommon &dic) {
    return s;
  }
  //Added to fix link error in test mode
  std::ostream &operator<<(std::ostream &out, const outcome::result<BlockBody> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const std::chrono::time_point<struct std::chrono::system_clock,class std::chrono::duration<__int64,struct std::ratio<1,10000000> > > &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<Block> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const detail::BlockInfoT<BlockInfoTag> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<std::vector<Hash256>> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<std::unique_ptr<sgns::storage::trie::EphemeralTrieBatch>>  &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<std::vector<Transaction>> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<std::vector<Version>> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<Version> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<std::unique_ptr<sgns::storage::trie::PersistentTrieBatch>> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<detail::BlockInfoT<BlockInfoTag>> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<std::vector<AuthorityId>> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const std::vector<boost::optional<sgns::crypto::VRFOutput>> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const boost::optional<sgns::crypto::VRFOutput> &test_struct)
  {
    return out ;
  }

  std::ostream &operator<<(std::ostream &out, const outcome::result<void> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<Justification> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<BlockHeader> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const outcome::result<sgns::verification::NextEpochDescriptor> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const std::function<void(std::vector<Block> const &)> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const std::function<void(std::error_code const &)> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const  boost::optional<Hash256> &test_struct)
  {
    return out ;
  }
  std::ostream &operator<<(std::ostream &out, const  boost::optional<uint64_t> &test_struct)
  {
    return out ;
  }
}  // namespace sgns::primitives

class ProductionTest : public testing::Test {
 public:
  void SetUp() override {
    app_state_manager_ = std::make_shared<AppStateManagerMock>();
    lottery_ = std::make_shared<ProductionLotteryMock>();
    production_synchronizer_ = std::make_shared<ProductionSynchronizerMock>();
    trie_db_ = std::make_shared<storage::trie::TrieStorageMock>();
    production_block_validator_ = std::make_shared<BlockValidatorMock>();
    epoch_storage_ = std::make_shared<EpochStorageMock>();
    tx_pool_ = std::make_shared<transaction_pool::TransactionPoolMock>();
    core_ = std::make_shared<runtime::CoreMock>();
    proposer_ = std::make_shared<ProposerMock>();
    block_tree_ = std::make_shared<BlockTreeMock>();
    gossiper_ = std::make_shared<ProductionGossiperMock>();
    clock_ = std::make_shared<SystemClockMock>();
    hasher_ = std::make_shared<HasherMock>();
    timer_mock_ = std::make_unique<testutil::TimerMock>();
    timer_ = timer_mock_.get();
    finality_authority_update_observer_ =
        std::make_shared<AuthorityUpdateObserverMock>();

    // add initialization logic
    auto expected_config = std::make_shared<primitives::ProductionConfiguration>();
    expected_config->slot_duration = slot_duration_;
    expected_config->randomness.fill(0);
    expected_config->genesis_authorities = {
        primitives::Authority{{keypair_.public_key}, 1}};
    expected_config->leadership_rate = {1, 4};
    expected_config->epoch_length = epoch_length_;

    verification::NextEpochDescriptor expected_epoch_digest{
        /*.authorities =*/ expected_config->genesis_authorities,
        /*.randomness =*/ expected_config->randomness};
    EXPECT_CALL(*epoch_storage_, addEpochDescriptor(0, expected_epoch_digest))
        .WillOnce(Return(outcome::success()));
    EXPECT_CALL(*epoch_storage_, addEpochDescriptor(1, expected_epoch_digest))
        .WillOnce(Return(outcome::success()));
    EXPECT_CALL(*epoch_storage_, getEpochDescriptor(1))
        .WillOnce(Return(expected_epoch_digest));

    auto block_executor = std::make_shared<BlockExecutor>(block_tree_,
                                                          core_,
                                                          expected_config,
                                                          production_synchronizer_,
                                                          production_block_validator_,
                                                          epoch_storage_,
                                                          tx_pool_,
                                        hasher_,
                                        finality_authority_update_observer_);

    production_ = std::make_shared<ProductionImpl>(app_state_manager_,
                                       lottery_,
                                       block_executor,
                                       trie_db_,
                                       epoch_storage_,
                                       expected_config,
                                       proposer_,
                                       block_tree_,
                                       gossiper_,
                                       keypair_,
                                       clock_,
                                       hasher_,
                                       std::move(timer_mock_),
                                       finality_authority_update_observer_);

    epoch_.randomness = expected_config->randomness;
    epoch_.epoch_duration = expected_config->epoch_length;
    epoch_.authorities = expected_config->genesis_authorities;
    epoch_.start_slot = 0;
    epoch_.epoch_index = 0;
  }
  std::shared_ptr<AppStateManagerMock> app_state_manager_;
  std::shared_ptr<ProductionLotteryMock> lottery_;
  std::shared_ptr<ProductionSynchronizer> production_synchronizer_;
  std::shared_ptr<storage::trie::TrieStorageMock> trie_db_;
  std::shared_ptr<BlockValidator> production_block_validator_;
  std::shared_ptr<EpochStorageMock> epoch_storage_;
  std::shared_ptr<runtime::CoreMock> core_;
  std::shared_ptr<ProposerMock> proposer_;
  std::shared_ptr<BlockTreeMock> block_tree_;
  std::shared_ptr<transaction_pool::TransactionPoolMock> tx_pool_;
  std::shared_ptr<ProductionGossiperMock> gossiper_;
  SR25519Keypair keypair_{generateSR25519Keypair()};
  std::shared_ptr<SystemClockMock> clock_;
  std::shared_ptr<HasherMock> hasher_;
  std::unique_ptr<testutil::TimerMock> timer_mock_;
  testutil::TimerMock *timer_;
  std::shared_ptr<AuthorityUpdateObserverMock>
      finality_authority_update_observer_;

  std::shared_ptr<ProductionImpl> production_;

  Epoch epoch_;
  ProductionDuration slot_duration_{60ms};
  EpochLength epoch_length_{2};

  VRFOutput leader_vrf_output_{
      uint256_t_to_bytes(50),
      {0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33,
       0x44, 0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44, 0x11, 0x22,
       0x33, 0x44, 0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44}};
  ProductionLottery::SlotsLeadership leadership_{boost::none, leader_vrf_output_};

  BlockHash best_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber best_block_number_ = 1u;

  primitives::BlockInfo best_leaf{best_block_number_, best_block_hash_};

  BlockHeader block_header_{
      createHash(0), 2, createHash(1), createHash(2), {PreRuntime{}}};
  Extrinsic extrinsic_{{1, 2, 3}};
  Block created_block_{block_header_, {extrinsic_}};

  Hash256 created_block_hash_{createHash(3)};

  SystemClockImpl real_clock_{};
};

ACTION_P(CheckBlockHeader, expected_block_header) {
  auto header_to_check = arg0.header;
  ASSERT_EQ(header_to_check.digest.size(), 2);
  header_to_check.digest.pop_back();
  ASSERT_EQ(header_to_check, expected_block_header);
}


/**
 * @given PRODUCTION production
 * @when running it in epoch with two slots @and out node is a leader in one of
 * them
 * @then block is emitted in the leader slot @and after two slots PRODUCTION moves to
 * the next epoch
 */
TEST_F(ProductionTest, Success) {
  auto test_begin = real_clock_.now();

  // runEpoch
  epoch_.randomness.fill(0);
  EXPECT_CALL(*lottery_, slotsLeadership(epoch_, _, keypair_))
      .WillOnce(Return(leadership_));
  Epoch next_epoch = epoch_;
  next_epoch.epoch_index++;
  next_epoch.start_slot += epoch_length_;

  EXPECT_CALL(*lottery_, slotsLeadership(next_epoch, _, keypair_))
      .WillOnce(Return(leadership_));

  EXPECT_CALL(*trie_db_, getRootHash())
      .WillRepeatedly(Return(base::Buffer{}));

  // runSlot (3 times)
  EXPECT_CALL(*clock_, now())
      .WillOnce(Return(test_begin))
      .WillOnce(Return(test_begin + slot_duration_))
      .WillOnce(Return(test_begin + slot_duration_))
      .WillOnce(Return(test_begin + slot_duration_ * 2))
      .WillOnce(Return(test_begin + slot_duration_ * 2));

  EXPECT_CALL(*timer_, expiresAt(test_begin + slot_duration_));
  EXPECT_CALL(*timer_, expiresAt(test_begin + slot_duration_ * 2));
  EXPECT_CALL(*timer_, expiresAt(test_begin + slot_duration_ * 3));

  EXPECT_CALL(*timer_, asyncWait(_))
      .WillOnce(testing::InvokeArgument<0>(boost::system::error_code{}))
      .WillOnce(testing::InvokeArgument<0>(boost::system::error_code{}))
      .WillOnce({})
      .RetiresOnSaturation();

  // processSlotLeadership
  // we are not leader of the first slot, but leader of the second
  EXPECT_CALL(*block_tree_, deepestLeaf()).WillOnce(Return(best_leaf));
  EXPECT_CALL(*proposer_, propose(BlockId{best_block_hash_}, _, _))
      .WillOnce(Return(created_block_));
  EXPECT_CALL(*hasher_, blake2b_256(_)).WillOnce(Return(created_block_hash_));
  EXPECT_CALL(*block_tree_, addBlock(_)).WillOnce(Return(outcome::success()));

  EXPECT_CALL(*gossiper_, blockAnnounce(_))
      .WillOnce(CheckBlockHeader(created_block_.header));

  production_->runEpoch(epoch_, test_begin + slot_duration_);
}
