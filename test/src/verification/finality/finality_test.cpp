#include <gtest/gtest.h>

#include "verification/finality/structs.hpp"
#include "verification/finality/impl/finality_impl.hpp"
#include "crypto/ed25519/ed25519_provider_impl.hpp"
#include "mock/src/application/app_state_manager_mock.hpp"
#include "mock/src/verification/finality/environment_mock.hpp"
#include "mock/src/verification/authority/authority_manager_mock.hpp"
#include "mock/src/verification/finality/voting_round_mock.hpp"
#include "mock/src/runtime/finality_api_mock.hpp"
#include <boost/optional/optional_io.hpp>


using namespace sgns;
using namespace verification;
using namespace finality;

using sgns::application::AppStateManagerMock;
using sgns::storage::BufferStorage;
using sgns::crypto::ED25519ProviderImpl;
using sgns::crypto::ED25519Keypair;
using sgns::authority::AuthorityManagerMock;
using sgns::runtime::FinalityApiMock;
using sgns::base::byte_t;


class FinalityImplTest : public testing::Test {
public:

protected:    
  void SetUp() override {
    ed_provider_ = std::make_shared<ED25519ProviderImpl>();
    auto keypair = ed_provider_->generateKeypair();
    environment_ = std::make_shared<EnvironmentMock>(); 
    app_state_manager_ = std::make_shared<AppStateManagerMock>();
    auth_manager_ = std::make_shared<AuthorityManagerMock>();
    io_context_ = std::make_shared<boost::asio::io_context>();
    finality_api_ = std::make_shared<FinalityApiMock>();
    //storage_ = std::make_shared<BufferStorage>();
    //clock_ = std::make_shared<Clock>();

    /*
    finality_ = std::make_shared<FinalityImpl>(app_state_manager_,
		    environment_,
		    storage_,
		    ed_provider_,
		    finality_api_,
		    keypair,
		    clock_,
		    io_context_,
		    auth_manager_);
	*/	    
  }
  
  std::shared_ptr<boost::asio::io_context> io_context_;
  std::shared_ptr<AuthorityManagerMock> auth_manager_;
  std::shared_ptr<runtime::FinalityApi> finality_api_;
  std::shared_ptr<ED25519ProviderImpl> ed_provider_;
  std::shared_ptr<BufferStorage> storage_;
  std::shared_ptr<EnvironmentMock> environment_;
  std::shared_ptr<AppStateManagerMock> app_state_manager_;
  std::shared_ptr<Clock> clock_;

  std::shared_ptr<FinalityImpl> finality_;
};

TEST_F (FinalityImplTest, PrepareTest) {
  bool result = finality_->prepare();
  ASSERT_TRUE(result);
}

TEST_F (FinalityImplTest, StartTest) {
  bool result = finality_->start();
  ASSERT_TRUE(result);
}

TEST_F (FinalityImplTest, StopTest) {
  finality_->stop();
}

TEST_F (FinalityImplTest, ExecuteNextRoundTest) {
  finality_->executeNextRound();
}

TEST_F (FinalityImplTest, OnVoteMessageTest) {
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

  RoundNumber rm{1};
  MembershipCounter mc{8};

  VoteMessage vm{};
  vm.round_number = rm;
  vm.vote = sm;
  vm.counter = mc;

  finality_->onVoteMessage(vm);
}

TEST_F (FinalityImplTest, OnFinailizeTest) {
  BlockNumber block_number = 1u;
  BlockHash block_hash{{0x41, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                           0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  Precommit vote(block_number, block_hash);
  Id id{};
  id[0] = static_cast<byte_t>(128);
  Id id1{};
  id1[0] = static_cast<byte_t>(132);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  SignedMessage sm1{};
  sm1.message = vote;
  sm1.id = id1;
  sm.ts = ts + 100;

  FinalityJustification fin_just{};
  fin_just.items = {sm, sm1};
  BlockInfo block_info{block_number, block_hash};
  RoundNumber round_num{2};
  Fin fin{};
  fin.round_number = round_num;
  fin.vote = block_info;
  fin.justification = fin_just;

  finality_->onFinalize(fin);
}
