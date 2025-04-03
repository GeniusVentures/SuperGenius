#include <gtest/gtest.h>
#include <random>
#include <array>

#include "verification/finality/structs.hpp"
#include "verification/finality/impl/vote_tracker_impl.hpp"

using namespace sgns;
using namespace crypto;
using namespace verification;
using namespace finality;

using sgns::primitives::BlockNumber;
using sgns::primitives::BlockHash;
using sgns::verification::finality::Prevote;
using Id = crypto::ED25519PublicKey;
using Id = sgns::verification::finality::Id;

// unit testing vote tracker 
class VoteTrackerTest : public testing::Test {
protected:
    VoteTrackerImpl vote_tracker_;
};

TEST_F(VoteTrackerTest, TestVotePush) {
    std::mt19937 rng(std::time(nullptr));
    std::uniform_int_distribution<int> dist(10000000, 99999999);

    BlockHash t_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};	
    BlockNumber t_block_number_ = 1u;

    // two differant blocks
    BlockHash t_block_hash1_ = t_block_hash_;
    BlockNumber t_block_number1_ = t_block_number_; 
    t_block_hash1_[0] = 0x42;
    t_block_number1_  += 1; 

    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist1(0, 255);

    //crypto::ED25519Signature signature;
    //for (auto& byte : signature.bytes) {
    //    byte = dist1(rng);
    //}

    // weight range 
    std::uniform_int_distribution<int> wt_dist(1, 6);

    uint32_t index = 1;
    // votes for first block
    for(index = 0; index < 10; ++index) {
      // random timestamp
      Timestamp timestamp = dist(rng);
      Prevote vote( t_block_number_, t_block_hash_);
      Id id{};
      id[0] = static_cast<uint8_t>( 123456 + index );

      // Voting messages
      VoteTrackerImpl::VotingMessage vm; 
      vm.message = vote;
      //vm.signature = signature;
      vm.id = id;
      vm.ts = timestamp;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);   
    }	    

    // vote for second block
    for(; index < 20; ++index) {
      // random timestamp 
      Timestamp timestamp = dist(rng);
      Prevote vote( t_block_number1_, t_block_hash1_);
      Id id{};
      id[0] = static_cast<uint8_t>( 123456 + index );

      // Voting messages
      VoteTrackerImpl::VotingMessage vm; 
      vm.message = vote;
      //vm.signature = signature;
      vm.id = id;
      vm.ts = timestamp;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);
    } 

    auto messages = vote_tracker_.getMessages();
    EXPECT_EQ(messages.size(), 20);
}

TEST_F(VoteTrackerTest, TestVoteEquivocatory) {
    std::mt19937 rng(std::time(nullptr));
    std::uniform_int_distribution<int> dist(10000000, 99999999);

    BlockHash t_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};	
    BlockNumber t_block_number_ = 1u;

    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist1(0, 255);

    // weight range 
    std::uniform_int_distribution<int> wt_dist(1, 6);

    uint32_t index = 1;
    // votes for first block
    for(index = 1; index < 10; ++index) {
      // random timestamp
      Timestamp timestamp = dist(rng);
      Prevote vote( t_block_number_, t_block_hash_);
      Id id{};
      id[0] = static_cast<uint8_t>( 123456 + index );

      // Voting messages
      VoteTrackerImpl::VotingMessage vm; 
      vm.message = vote;
      vm.id = id;
      vm.ts = timestamp;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);   
    }	    

    // test for equivocatory votes
    t_block_hash_[0] = 0x42;
    Timestamp timestamp = dist(rng); 
    Prevote vote( t_block_number_, t_block_hash_);
    Id id{};
    id[0] = static_cast<uint8_t>( 123456 + 2 );

    VoteTrackerImpl::VotingMessage vm; 
    vm.message = vote;
    // vote with same id for the same block
    vm.id = id;
    vm.ts = timestamp;

    uint32_t wt = wt_dist(rng);
    vote_tracker_.push(vm, wt);   

    auto messages = vote_tracker_.getMessages();

    size_t e_size = 0;
    for (const auto& message : messages) {
        if (boost::get<VoteTrackerImpl::EquivocatoryVotingMessage>(&message) != nullptr) {
            e_size++;
        }
    }

    EXPECT_EQ(e_size, 1);
}    

TEST_F(VoteTrackerTest, TestVoteOrdering) {
    std::mt19937 rng(std::time(nullptr));
    std::uniform_int_distribution<int> dist(10000000, 99999999);

    BlockHash t_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};	
    BlockNumber t_block_number_ = 1u;

    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist1(0, 255);

    // weight range 
    std::uniform_int_distribution<int> wt_dist(1, 6);

    uint32_t index = 1;
    // votes for first block
    for(index = 1; index < 10; ++index) {
      // random timestamp
      Timestamp timestamp = dist(rng);
      Prevote vote( t_block_number_, t_block_hash_);
      Id id{};
      id[0] = static_cast<uint8_t>( 123456 + index );

      // Voting messages
      VoteTrackerImpl::VotingMessage vm; 
      vm.message = vote;
      vm.id = id;
      vm.ts = timestamp;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);   
    }	    

    auto ordered_votes = vote_tracker_.getOrderedVotes();
    auto prev_ts = ordered_votes.begin()->first;
    for (const auto& [key, obj] : ordered_votes) {
        EXPECT_LE(prev_ts, obj.ts);
        prev_ts = obj.ts;
    }
}    

TEST_F(VoteTrackerTest, TestMedianMessage) {
    std::mt19937 rng(std::time(nullptr));
    std::uniform_int_distribution<int> dist(10000000, 99999999);

    BlockHash t_block_hash_{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                              0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};	
    BlockNumber t_block_number_ = 1u;

    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist1(0, 255);

    // weight range 
    std::uniform_int_distribution<int> wt_dist(1, 6);
    // random timestamp
    Timestamp timestamp = dist(rng);

    uint32_t index = 0;
    // votes for first block
    for(; index < 5; ++index) {
      Prevote vote( t_block_number_, t_block_hash_);
      Id id{};
      id[0] = static_cast<uint8_t>( 123456 + index );

      // Voting messages
      VoteTrackerImpl::VotingMessage vm; 
      vm.message = vote;
      vm.id = id; 
      vm.ts = timestamp + index;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);   
    }	    
    Prevote vote( t_block_number_, t_block_hash_);

    auto message = vote_tracker_.getMedianMessage(t_block_hash_);
    EXPECT_EQ(message.ts, timestamp + 2);
}    

