#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <array>

#include "verification/finality/structs.hpp"
#include "verification/finality/impl/voting_round_impl.hpp"
#include "verification/finality/impl/vote_tracker_impl.hpp"
#include "verification/finality/vote_graph/vote_graph_impl.hpp"
#include "verification/finality/round_state.hpp"
#include "mock/src/verification/finality/vote_crypto_provider_mock.hpp"
#include "mock/src/verification/finality/environment_mock.hpp"
#include "mock/src/verification/finality/finality_mock.hpp"
#include "mock/src/verification/finality/chain_mock.hpp"
#include "mock/src/clock/clock_mock.hpp"
#include "clock/impl/clock_impl.hpp"

using namespace sgns;
using namespace verification;
using namespace finality;

using sgns::base::byte_t;
using sgns::primitives::BlockNumber;
using sgns::primitives::BlockHash;
using sgns::clock::ClockImpl;

// unit testing vote tracker
class VotingRoundTest : public testing::Test {
public:

protected:
  std::shared_ptr<VotingRoundImpl> voting_round_;

  void SetUp() override {
    auto round_number = 1;	  
    Id id{};
    id[0] = static_cast<byte_t>(128);
    std::shared_ptr<VoterSet> voter_set = std::make_shared<VoterSet>(128);
    for(int i=0; i<8; ++i) {
      Id voter_id{};
      voter_id[0] = static_cast<byte_t>(128+1);
      voter_set->insert(voter_id, i);
    }

    FinalityConfig config{
	    voter_set,
	    round_number,
	    std::chrono::milliseconds(333),
	    id};

    BlockHash block_hash{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
    BlockNumber block_number = 1u;

    BlockHash final_hash = block_hash;
    final_hash[0] = 0x10;
    BlockInfo block_info( block_number, final_hash);

    // initialize
    finality_ = std::make_shared<FinalityMock>();
    // todo: finality config
    environment_ = std::make_shared<EnvironmentMock>();  
    vote_crypto_provider_ = std::make_shared<VoteCryptoProviderMock>();
    // mock not required
    prevote_tracker_ = std::make_shared<VoteTrackerImpl>();
    precommit_tracker_ = std::make_shared<VoteTrackerImpl>();
    chain_ = std::make_shared<ChainMock>();
    vote_graph_ = std::make_shared<VoteGraphImpl>(BlockInfo(block_number, block_hash), chain_); 
    clock_ = std::make_shared<ClockImpl<std::chrono::steady_clock>>();
    io_context_ = std::make_shared<boost::asio::io_context>();
    round_state_ = std::make_shared<RoundState>();
    round_state_->best_final_candidate = block_info;

    voting_round_ = std::make_shared<VotingRoundImpl>(finality_,
	  	    config,
		    environment_,
		    vote_crypto_provider_,
		    prevote_tracker_,
		    precommit_tracker_,
		    vote_graph_,
		    clock_,
		    io_context_,
		    round_state_);
  }	    

  void TearDown() override {
    // clean up	    
  }

  std::shared_ptr<FinalityMock> finality_;
  std::shared_ptr<EnvironmentMock> environment_;
  std::shared_ptr<VoteCryptoProviderMock> vote_crypto_provider_;
  std::shared_ptr<VoteTrackerImpl> prevote_tracker_;
  std::shared_ptr<VoteTrackerImpl> precommit_tracker_;
  std::shared_ptr<ChainMock> chain_;
  std::shared_ptr<VoteGraphImpl> vote_graph_;
  std::shared_ptr<ClockImpl<std::chrono::steady_clock>> clock_;
  std::shared_ptr<boost::asio::io_context> io_context_;
  std::shared_ptr<RoundState> round_state_;

 private:


};

// voting round start test
TEST_F(VotingRoundTest, PlayTest) {
  // test for the initial state
  auto curr_stage = voting_round_->getStage();
  EXPECT_EQ(curr_stage, VotingRoundImpl::Stage::INIT); 
  // call the play function 
  voting_round_->play(); 
  // test 
  curr_stage = voting_round_->getStage();
  EXPECT_EQ(curr_stage, VotingRoundImpl::Stage::PREVOTE_RUNS);
}

// voting end
TEST_F(VotingRoundTest, EndTest) {
  voting_round_->end();
  auto curr_stage = voting_round_->getStage();
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

  voting_round_->onPrimaryPropose(sm);
}

TEST_F(VotingRoundTest, OnPrevote) {
  BlockHash block_hash{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber block_number = 1u;
  Prevote vote(block_number, block_hash);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  voting_round_->onPrevote(sm);	
  
}

TEST_F(VotingRoundTest, OnPrecommit) {
  BlockHash t_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber t_block_number_ = 1u;
  Precommit vote( t_block_number_, t_block_hash_);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  voting_round_->onPrecommit(sm); 	
  
}

TEST_F(VotingRoundTest, OnFinalize) {
  
}
