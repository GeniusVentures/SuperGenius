#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <array>

#include "verification/finality/structs.hpp"
#include "verification/finality/vote_graph/vote_graph_impl.hpp"
#include "mock/src/verification/finality/chain_mock.hpp"
#include "verification/finality/vote_weight.hpp"


using namespace sgns;
using namespace verification;
using namespace finality;

using sgns::primitives::BlockInfo;

class VoteGraphTest : public testing::Test {
public:

protected:
  void SetUp() override {
    BlockNumber block_number = 1u;
    BlockHash block_hash{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};

    std::shared_ptr<ChainMock> chain = std::make_shared<ChainMock>();	  
    BlockInfo block_info{block_number, block_hash};

    vote_graph_ = std::make_shared<VoteGraphImpl>(block_info, chain);
  }
   
  void TearDown() override {
    // clean up
  }

  std::shared_ptr<VoteGraphImpl> vote_graph_;

private:

};

TEST_F(VoteGraphTest, AdjustBaseTest) {
  
  BlockHash block_hash{{0x12, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockHash bh = block_hash;
  BlockHash bh1 = block_hash;
  bh1[0] = 0x21;
  BlockHash bh2 = block_hash;
  bh2[0] = 0x32;
  std::vector<BlockHash> ancestry_proof = { bh2 };

  VoteWeight v(0x8);
  BlockInfo bi{1u, bh};
  BlockInfo bi1{2u, bh1};

  // test for empty ancestor proof
  vote_graph_->adjustBase({});
  
  // insert blocks 
  vote_graph_->insert(bi, v);
  vote_graph_->insert(bi1, v);
  
  // test for adjust base
  // vote_graph_->adjustBase(ancestry_proof);
}

TEST_F(VoteGraphTest, InsertTest) {
  BlockHash block_hash{{0x12, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockInfo block_info{1u, block_hash};
  VoteWeight weight(0x8);
  outcome::result<void> result = vote_graph_->insert(block_info, weight);

  EXPECT_TRUE(result);

  // check if data is inserted or not
}

TEST_F(VoteGraphTest, FindAncestorTest) {
  // insert in loop
  BlockHash base_hash{{0x12, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  for(int i=0; i<8; ++i) {
    BlockHash block_hash = base_hash;
    block_hash[0] = block_hash[0] + i;
    BlockNumber block_num = i;
    BlockInfo block_info{block_num, block_hash};
    VoteWeight weight(0x8);
    vote_graph_->insert(block_info, weight);
  }

  BlockHash block_hash = base_hash;
  BlockInfo block_info{1u, block_hash};

  VoteGraph::Condition condition = [](const VoteWeight& voteWeight) {
    return true;
  };

  boost::optional<BlockInfo> result = vote_graph_->findAncestor(block_info, condition, VoteWeight::prevoteComparator);

  EXPECT_TRUE(result);  
}

TEST_F(VoteGraphTest, FindGhostTest) {

  boost::optional<BlockInfo> current_best = { };
  BlockHash base_hash{{0x12, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  for(int i=0; i<8; ++i) {
    BlockHash block_hash = base_hash;
    block_hash[0] = block_hash[0] + i;
    BlockNumber block_num = i;
    BlockInfo block_info{block_num, block_hash};
    VoteWeight weight(0x8);
    vote_graph_->insert(block_info, weight);
  }
  VoteGraph::Condition condition = [](const VoteWeight& voteWeight) {
    return true;
  };

  boost::optional<BlockInfo> result = vote_graph_->findGhost(current_best, condition, VoteWeight::prevoteComparator);

  EXPECT_TRUE(result);  
}

TEST_F(VoteGraphTest, IntroduceBranchTest) {
  BlockHash base_hash{{0x12, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  for(int i=0; i<8; ++i) {
    BlockHash block_hash = base_hash;
    block_hash[0] = block_hash[0] + i;
    BlockNumber block_num = i;
    BlockInfo block_info{block_num, block_hash};
    VoteWeight weight(0x8);
    vote_graph_->insert(block_info, weight);
  }

  BlockNumber block_number = 2u;
  BlockHash block_hash = base_hash;
  BlockInfo ancestor{block_number, block_hash};
  block_hash[0] = 0x13;
  std::vector<primitives::BlockHash> descendents = { block_hash };

  vote_graph_->introduceBranch(descendents, ancestor);

}

TEST_F(VoteGraphTest, AppendTest) {
  BlockHash base_hash{{0x12, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  for(int i=0; i<8; ++i) {
    BlockHash block_hash = base_hash;
    block_hash[0] = block_hash[0] + i;
    BlockNumber block_num = i;
    BlockInfo block_info{block_num, block_hash};
    VoteWeight weight(0x8);
    vote_graph_->insert(block_info, weight);
  }

  BlockNumber block_number = 2u;
  BlockHash block_hash = base_hash;
  block_hash[0] = 0x42;
  BlockInfo block{block_number, block_hash};

  auto result = vote_graph_->append(block);

  ASSERT_TRUE(result); 

}


