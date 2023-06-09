#include <gtest/gtest.h>
#include <random>
#include <array>

#include "verification/finality/structs.hpp"
#include "verification/finality/impl/vote_tracker_impl.hpp"

using namespace sgns;
using namespace crypto;
using namespace verification;
using namespace finality;

using testing::Return;

// unit testing vote tracker 
class VoteTrackerTest : public testing::Test {
protected:
    VoteTracker vote_tracker_;
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
    t_block_number1_  += 1 

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    crypto::ED25519Signature signature;
    for (auto& byte : signature.bytes) {
        byte = dist(rng);
    }

    // weight range 
    std::uniform_int_distribution<int> wt_dist(1, 6);

    uint32_t index_ = 1;
    // votes for first block
    for(index = 1; index < 10; ++index) {
      // random timestamp
      Timestamp timestamp = dist(rng);
      Vote vote{block_hash, block_number};

      // Voting messages
      VotingMessage vm; 
      vm.message = {t_block_hash_, t_block_number_};
      vm.signature = signature;
      vm.id = index;
      vm.ts = timestamp;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);   
    }	    

    // vote for second block
    for(index = 1; index < 10; ++index) {
      // random timestamp 
      Timestamp timestamp = dist(rng);
      Vote vote{block_hash, block_number};
      
      // Voting messages
      VotingMessage vm; 
      vm.message = {block_hash1_, block_number2_};
      vm.signature = signature;
      vm.id = index;
      vm.ts = timestamp;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);
    } 

    auto messges = vote_tracker_.getMessages();
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

    // two differant blocks
    BlockHash t_block_hash1_ = t_block_hash_;
    BlockNumber t_block_number1_ = t_block_number_; 
    t_block_hash1_[0] = 0x42;
    t_block_number1_  += 1 

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    crypto::ED25519Signature signature;
    for (auto& byte : signature.bytes) {
        byte = dist(rng);
    }

    // weight range 
    std::uniform_int_distribution<int> wt_dist(1, 6);

    uint32_t index_ = 1;
    // votes for first block
    for(index = 1; index < 10; ++index) {
      // random timestamp
      Timestamp timestamp = dist(rng);

      // Voting messages
      VotingMessage vm; 
      vm.message = {t_block_hash_, t_block_number_};
      vm.signature = signature;
      vm.id = index;
      vm.ts = timestamp;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);   
    }	    

    // test for equivocatory votes
    Timestamp timestamp = dist(rng); 
    VotingMessage vm; 
    vm.message = {t_block_hash_, t_block_number_};
    vm.signature = signature;
    // vote with same id for the same block
    vm.id = 2;
    vm.ts = timestamp;

    Vote vote{block_hash, block_number};
    uint32_t wt = wt_dist(rng);
    vote_tracker_.push(vm, wt);   

    auto messges = vote_tracker_.getMessages();

    size_t e_size = 0;
    for (const auto& message : messages) {
        if (std::get_if<EquivocatoryVotingMessage>(&message) != nullptr) {
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

    // two differant blocks
    BlockHash t_block_hash1_ = t_block_hash_;
    BlockNumber t_block_number1_ = t_block_number_; 
    t_block_hash1_[0] = 0x42;
    t_block_number1_  += 1 

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    crypto::ED25519Signature signature;
    for (auto& byte : signature.bytes) {
        byte = dist(rng);
    }

    // weight range 
    std::uniform_int_distribution<int> wt_dist(1, 6);

    uint32_t index_ = 1;
    // votes for first block
    for(index = 1; index < 10; ++index) {
      // random timestamp
      Timestamp timestamp = dist(rng);

      // Voting messages
      VotingMessage vm; 
      vm.message = {t_block_hash_, t_block_number_};
      vm.signature = signature;
      vm.id = index;
      vm.ts = timestamp;
      
      uint32_t wt = wt_dist(rng);
      vote_tracker_.push(vm, wt);   
    }	    

    auto ordered_votes = vote_tracker.get_ordered_votes();
    auto prev_ts = ordered_votes.begin()->second.ts;
    for (const auto& [key, obj] : ordered_votes) {
        EXPECT_LE(prev_ts, obj.ts);
        prev_ts = obj.ts;
    }
}    

