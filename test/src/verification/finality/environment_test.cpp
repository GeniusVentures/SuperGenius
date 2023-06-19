#include <gtest/gtest.h>

#include "verification/finality/structs.hpp"
#include "verification/finality/impl/environment_impl.hpp"
#include "mock/src/blockchain/block_tree_mock.hpp"
#include "mock/src/blockchain/block_header_repository_mock.hpp"
#include "mock/src/verification/finality/gossiper_mock.hpp"
#include <boost/optional/optional_io.hpp>

using namespace sgns;
using namespace verification;
using namespace finality;

using sgns::blockchain::BlockTreeMock;
using sgns::blockchain::BlockHeaderRepositoryMock;
using sgns::base::byte_t;


class EnvironmentImplTest : public testing::Test {
public:

protected:    
  void SetUp() override {
    block_tree_ = std::make_shared<BlockTreeMock>();
    header_repository_ = std::make_shared<BlockHeaderRepositoryMock>();
    gossiper_ = std::make_shared<GossiperMock>();

    environment_ = std::make_unique<EnvironmentImpl>(
            block_tree_,
            header_repository_,
            gossiper_);
  }

  void TearDown() override {
    environment_.reset();
  }

  std::unique_ptr<EnvironmentImpl> environment_;
  std::shared_ptr<blockchain::BlockTreeMock> block_tree_;
  std::shared_ptr<blockchain::BlockHeaderRepositoryMock> header_repository_;
  std::shared_ptr<GossiperMock> gossiper_;

private:

};

TEST_F(EnvironmentImplTest, GetAncestry) {
  BlockHash base{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};  

  BlockHash block{{0x42, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};  

  outcome::result<std::vector<BlockHash>> result = environment_->getAncestry(base, block);
  ASSERT_TRUE(result);
  std::vector<BlockHash> ancestry = result.value();
  // test for the ancestory size
}

TEST_F(EnvironmentImplTest, BestChainContaining) {
  BlockHash base{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                  0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};  
  outcome::result<BlockInfo> result = environment_->bestChainContaining(base);
  ASSERT_TRUE(result);
  BlockInfo block_info = result.value();
}

TEST_F(EnvironmentImplTest, OnProposed) {
  RoundNumber round{1};
  MembershipCounter mem_counter{};
  
  BlockNumber block_number = 1u;
  BlockHash block_hash{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  PrimaryPropose vote(block_number, block_hash);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  outcome::result<void> result = environment_->onProposed(round, mem_counter, sm);
  ASSERT_TRUE(result);
}

TEST_F(EnvironmentImplTest, OnPrevoted) {
  RoundNumber round{1};
  MembershipCounter mem_counter{};
  
  BlockNumber block_number = 1u;
  BlockHash block_hash{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  Prevote vote(block_number, block_hash);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  outcome::result<void> result = environment_->onPrevoted(round, mem_counter, sm);
  ASSERT_TRUE(result);
}

TEST_F(EnvironmentImplTest, OnPrecommitted) {
  RoundNumber round{1};
  MembershipCounter mem_counter{};
  
  BlockNumber block_number = 1u;
  BlockHash block_hash{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  Precommit vote(block_number, block_hash);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  outcome::result<void> result = environment_->onPrecommitted(round, mem_counter, sm);
  ASSERT_TRUE(result);
}

TEST_F(EnvironmentImplTest, OnCommitted) {
  RoundNumber round{2};
  BlockNumber block_number = 1u;
  BlockHash block_hash{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockInfo block{block_number, block_hash};

  // precommit msg 1
  BlockNumber base_number = 2u;
  BlockHash base_hash{{0x33, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                       0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                       0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                       0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  Precommit vote(base_number, base_hash);
  Id id{};
  id[0] = static_cast<byte_t>(101);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count()-100;

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  // pre commit msg 2
  BlockNumber base_number1 = 4u;
  BlockHash base_hash1 = base_hash;
  base_hash1[0] = 0x44;
  Precommit vote1(base_number1, base_hash1);
  Id id1{};
  id1[0] = static_cast<byte_t>(102);

  SignedMessage sm1{};
  sm1.message = vote1;
  sm1.id = id1;
  sm1.ts = ts+1000;
  FinalityJustification fj{};
  fj.items = {sm, sm1};

  auto result = environment_->onCommitted(round, block, fj);
  ASSERT_TRUE(result);

}
