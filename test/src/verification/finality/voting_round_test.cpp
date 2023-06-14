#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <array>

#include "verification/finality/structs.hpp"
#include "verification/finality/impl/voting_round_impl.hpp"
#include "verification/finality/impl/vote_tracker_impl.hpp"
#include "mock/src/verification/finality/vote_crypto_provider_mock.hpp"
#include "mock/src/verification/finality/environment_mock.hpp"
#include "mock/src/verification/finality/finality_mock.hpp"
#include "mock/src/verification/finality/vote_graph_mock.hpp"

using namespace sgns;
using namespace verification;
using namespace finality;

using sgns::base::byte_t;
using sgns::primitives::BlockNumber;
using sgns::primitives::BlockHash;

// unit testing vote tracker
class VotingRoundTest : public testing::Test {
public:
  std::shared_ptr<VotingRoundImpl> voting_round_;

protected:
  void SetUp() override {
    auto round_number = 1;	  
    Id id{};
    id[0] = static_cast<byte_t>(128);
    FinalityConfig config{
	    std::make_shared<VoterSet>(128),
	    round_number,
	    std::chrono::milliseconds(333),
	    id};

    // initialize
    std::shared_ptr<FinalityMock> finality = std::make_shared<FinalityMock>();
    // todo: finality config
    std::shared_ptr<EnvironmentMock> environment = std::make_shared<EnvironmentMock>();  
    std::shared_ptr<VoteCryptoProviderMock> vote_crypto_provider = std::make_shared<VoteCryptoProviderMock>();
    // mock not required
    std::shared_ptr<VoteTrackerImpl> prevote_tracker = std::make_shared<VoteTrackerImpl>();
    std::shared_ptr<VoteTrackerImpl> precommit_tracker = std::make_shared<VoteTrackerImpl>();
    std::shared_ptr<VoteGraphMock> vote_graph = std::make_shared<VoteGraphMock>(); 
    std::shared_ptr<Clock> clock = std::make_shared<Clock>();
    std::shared_ptr<boost::asio::io_context> io_context = std::make_shared<boost::asio::io_context>();

    voting_round_ = std::make_shared<VotingRoundImpl>(finality,
	  	    config,
		    environment,
		    vote_crypto_provider,
		    prevote_tracker,
		    precommit_tracker,
		    vote_graph,
		    clock,
		    io_context);

  }	    

  void TearDown() override {
    // clean up	    
  }

 private:


};

// voting round start test
TEST_F(VotingRoundTest, PlayTest) {
  // test for the initial state
  auto curr_stage = voting_round_.getStage();
  EXPECT_EQ(curr_stage, VotingRoundImpl::Stage::INIT); 
  // call the play function 
  voting_round_->play(); 
  // test 
  curr_stage = voting_round_.getStage();
  EXPECT_EQ(curr_stage, VotingRoundImpl::Stage::PREVOTE_RUNS);
}

// voting end
TEST_F(VotingRoundTest, EndTest) {
  voting_round_.end();
  auto curr_stage = voting_round_.getStage();
  EXPECT_EQ(curr_stage, VotingRoundImpl::Stage::COMPLETED); 
}


TEST_F(VotingRoundTest, DoProposalTest) {
  // call proposal
  auto success = voting_round_->doProposal(); 
  EXPECT_EQ(success, true);
}

// do prevote 
TEST_F(VotingRoundTest, DoPrevoteTest) {
  auto success = voting_round_->doPrevote();  
  EXPECT_EQ(success, true);
}

// do precommit
TEST_F(VotingRoundTest, DoPrecommitTest) {
  auto success = voting_round_->doPrecommit(); 
  EXPECT_EQ(success, true);
}

TEST_F(VotingRoundTest, OnPrimaryPropose) {

  BlockHash t_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber t_block_number_ = 1u;
  PrimaryPropose vote( t_block_number_, t_block_hash_);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  voting_round_.onPrimaryPropose(sm);
}

TEST_F(VotingRoundTest, OnPrevote) {
  BlockHash t_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber t_block_number_ = 1u;
  PrimaryPropose vote( t_block_number_, t_block_hash_);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  voting_round_.onPrevote(sm);	
  
}

TEST_F(VotingRoundTest, OnPrecommit) {
  BlockHash t_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber t_block_number_ = 1u;
  PrimaryPropose vote( t_block_number_, t_block_hash_);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  voting_round_.onPrecommit(sm); 	
  
}

TEST_F(VotingRoundTest, OnFinalize) {
  
}
