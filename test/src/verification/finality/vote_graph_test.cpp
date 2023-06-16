#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <array>

#include "verification/finality/structs.hpp"
#include "verification/finality/vote_graph/vote_graph_impl.hpp"


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
  vote_graphi_->adjustBase(ancestry_proof);
}

TEST_F(VoteGraphTest, InsertTest) {
  BlockHash block_hash{{0x12, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockInfo bi{1u, block_hash};
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
  for(init i=0; i<8; ++i) {
    BlockHash block_hash = base_hash;
    block_hash[0] = block_hash[0] + i;
    Prevote block_info{i, block_hash};
    VoteWeight weight(0x8);
    vote_graph_->insert(block_info, weight);
  }

  BlockHash block_hash = base_hash;
  Prevote block_info{1u, block_hash};

  VoteGraph::Condition condition = [](const VoteWeight& voteWeight) {
    return true;
  };

  boost::optional<BlockInfo> result = vote_graph_->findAncestor(block, condition, prevoteComparator);

  EXPECT_TRUE(result);  
}

TEST_F(VoteGraphTest, FindGhostTest) {

  boost::optional<BlockInfo> current_best = { /* Provide the current best block data */ };
  VoteGraph::Condition condition = [](const VoteWeight& voteWeight) {
  };

  boost::optional<BlockInfo> result = vote_graph_->findGhost(current_best, condition, comparator);

  EXPECT_TRUE(result);  
}

TEST_F(VoteGraphTest, IntroduceBranchTest) {
  std::vector<primitives::BlockHash> descendents = { /* Provide the descendents block hashes */ };
  BlockInfo ancestor = { /* Provide the ancestor block data */ };

  vote_graph_->introduceBranch(descendents, ancestor);

}

TEST_F(VoteGraphTest, AppendTest) {
  BlockInfo block = {  };

  auto result = voteGraph.append(block);

  ASSERT_TRUE(result); 

}

TEST_F(VoteGraphTest, GhostFindMergePointTest) {
  BlockHash activeNodeHash = "active_node_hash";  
  VoteGraph::Entry activeNode = {  };
  boost::optional<BlockInfo> forceConstrain = {  };
  VoteGraph::Condition condition = [](const VoteWeight& weight) { };
  Comparator comparator = [](const VoteWeight& weight1, const VoteWeight& weight2) {  };

  VoteGraph::Subchain subchain = voteGraph.ghostFindMergePoint(activeNodeHash, activeNode, forceConstrain, condition, comparator);

}

TEST_F(VoteGraphTest, FindContainingNodesTest) {
  BlockInfo block = { /* Provide the block information */ };

  // Call the findContainingNodes function
  boost::optional<std::vector<BlockHash>> containingNodes = vote_graph_->findContainingNodes(block);

}


