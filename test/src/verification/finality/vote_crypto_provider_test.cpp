#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <array>
#include "scale/scale.hpp"
#include <gsl/gsl>

#include "verification/finality/structs.hpp"
#include "verification/finality/impl/vote_crypto_provider_impl.hpp"
#include "verification/finality/voter_set.hpp"
#include "mock/src/crypto/ed25519_provider_mock.hpp"
#include "crypto/ed25519/ed25519_provider_impl.hpp"


using namespace sgns;
using namespace verification;
using namespace finality;

using sgns::crypto::ED25519Keypair;
using sgns::crypto::ED25519PrivateKey;
using sgns::crypto::ED25519PublicKey;
using sgns::crypto::ED25519ProviderMock;
using sgns::crypto::ED25519Signature;
using sgns::crypto::ED25519ProviderImpl;
using sgns::base::byte_t;

using testing::_;
using testing::A;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

class VoteCryptoProviderTest : public testing::Test {
public:

protected:
  void SetUp() override {
    auto pub = ED25519PublicKey::fromHex("3086e3f8cdc1e69f855a1b1907331b7594500c0fc40e"
                                         "91e25e734513df85289f");
    auto priv = ED25519PrivateKey::fromHex("a4681403ba5b6a3f3bd0b0604ce439a78244c7d43b1"
                                         "27ec35cd8325602dd47fd");	    
    // ed_provider_ = std::make_shared<ED25519ProviderImpl>();
    ed_provider_ = std::make_shared<ED25519ProviderMock>();
    ED25519Keypair keypair{priv.value(), pub.value()};
    //auto keypair = ed_provider_->generateKeypair();

    voter_set_ = std::make_shared<VoterSet>();

    vote_crypto_provider_ = std::make_unique<VoteCryptoProviderImpl>(
                            keypair, 
			    ed_provider_, 
			    round_number_, 
			    voter_set_);

  }	    

  void TearDown() override {
    // clean up
  }

  std::shared_ptr<ED25519ProviderMock> ed_provider_;
  RoundNumber round_number_{0};
  std::shared_ptr<VoterSet> voter_set_;
  std::shared_ptr<VoteCryptoProviderImpl> vote_crypto_provider_;
private:	  
  
};	

TEST_F(VoteCryptoProviderTest, VerifyPrimaryPropose) {
  BlockHash block_hash{{0x42, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber block_number = 1u;
  PrimaryPropose vote(block_number, block_hash);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  auto payload = scale::encode(sm.message, round_number_, voter_set_->id()).value();
  gsl::span<uint8_t> data(reinterpret_cast<uint8_t*>(payload.data()), payload.size());
  EXPECT_CALL(*ed_provider_, verify(sm.signature, data, sm.id)).WillOnce(Return(true));
  bool result = vote_crypto_provider_->verifyPrimaryPropose(sm);

  EXPECT_TRUE(result);
}

TEST_F(VoteCryptoProviderTest, VerifyPrevote) {
  BlockHash block_hash{{0x42, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber block_number = 1u;
  Prevote vote( block_number, block_hash);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  auto payload = scale::encode(sm.message, round_number_, voter_set_->id()).value();
  gsl::span<uint8_t> data(reinterpret_cast<uint8_t*>(payload.data()), payload.size());
  EXPECT_CALL(*ed_provider_, verify(sm.signature, data, sm.id)).WillOnce(Return(true));
  bool result = vote_crypto_provider_->verifyPrevote(sm);

  EXPECT_TRUE(result);
}

TEST_F(VoteCryptoProviderTest, VerifyPrecommit) {
  BlockNumber block_number = 1u;
  BlockHash block_hash{{0x42, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  Precommit vote( block_number, block_hash);
  Id id{};
  id[0] = static_cast<byte_t>(128);

  auto timestamp = std::chrono::system_clock::now();
  Timestamp ts = timestamp.time_since_epoch().count();

  SignedMessage sm{};
  sm.message = vote;
  sm.id = id;
  sm.ts = ts;

  auto payload = scale::encode(sm.message, round_number_, voter_set_->id()).value();
  gsl::span<uint8_t> data(reinterpret_cast<uint8_t*>(payload.data()), payload.size());

  EXPECT_CALL(*ed_provider_, verify(sm.signature, data, sm.id)).WillOnce(Return(true));
  bool result = vote_crypto_provider_->verifyPrecommit(sm);

  EXPECT_TRUE(result);
}

TEST_F(VoteCryptoProviderTest, SignPrimaryPropose) {
  std::shared_ptr<VoteCryptoProviderImpl> vote_crypto_provider;
  std::shared_ptr<ED25519ProviderImpl> ed_provider = std::make_shared<ED25519ProviderImpl>();
  auto pub = ED25519PublicKey::fromHex("3086e3f8cdc1e69f855a1b1907331b7594500c0fc40e"
                                        "91e25e734513df85289f");
  auto priv = ED25519PrivateKey::fromHex("a4681403ba5b6a3f3bd0b0604ce439a78244c7d43b1"
                                         "27ec35cd8325602dd47fd");	    
  ED25519Keypair keypair{priv.value(), pub.value()};
  vote_crypto_provider = std::make_unique<VoteCryptoProviderImpl>(
                            keypair,
                            ed_provider,
                            round_number_,
                            voter_set_);

  BlockHash block_hash{{0x42, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber block_number = 1u;
  PrimaryPropose primary_propose(block_number, block_hash);
  SignedMessage signed_message = vote_crypto_provider->signPrimaryPropose(primary_propose);
  // validate the signed message
  auto ts = signed_message.ts;
  std::cout<<"Signed Message" << ts << std::endl;
  EXPECT_EQ(ts, 0);
}

TEST_F(VoteCryptoProviderTest, SignPrevote) {
  std::shared_ptr<VoteCryptoProviderImpl> vote_crypto_provider;
  std::shared_ptr<ED25519ProviderImpl> ed_provider = std::make_shared<ED25519ProviderImpl>();
  auto pub = ED25519PublicKey::fromHex("3086e3f8cdc1e69f855a1b1907331b7594500c0fc40e"
                                        "91e25e734513df85289f");
  auto priv = ED25519PrivateKey::fromHex("a4681403ba5b6a3f3bd0b0604ce439a78244c7d43b1"
                                         "27ec35cd8325602dd47fd");	    
  ED25519Keypair keypair{priv.value(), pub.value()};
  vote_crypto_provider = std::make_unique<VoteCryptoProviderImpl>(
                            keypair,
                            ed_provider,
                            round_number_,
                            voter_set_);

  BlockHash block_hash{{0x42, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber block_number = 1u;
  Prevote vote(block_number, block_hash);

  SignedMessage signed_message = vote_crypto_provider->signPrevote(vote);
  // validate the signed message

}

TEST_F(VoteCryptoProviderTest, SignPrecommit) {
  std::shared_ptr<VoteCryptoProviderImpl> vote_crypto_provider;
  std::shared_ptr<ED25519ProviderImpl> ed_provider = std::make_shared<ED25519ProviderImpl>();
  auto pub = ED25519PublicKey::fromHex("3086e3f8cdc1e69f855a1b1907331b7594500c0fc40e"
                                        "91e25e734513df85289f");
  auto priv = ED25519PrivateKey::fromHex("a4681403ba5b6a3f3bd0b0604ce439a78244c7d43b1"
                                         "27ec35cd8325602dd47fd");	    
  ED25519Keypair keypair{priv.value(), pub.value()};
  vote_crypto_provider = std::make_unique<VoteCryptoProviderImpl>(
                            keypair,
                            ed_provider,
                            round_number_,
                            voter_set_);

  BlockHash block_hash{{0x42, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x54,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
                        0x11, 0x22, 0x33, 0x44, 0x11, 0x24, 0x33, 0x44}};
  BlockNumber block_number = 1u;
  Precommit vote(block_number, block_hash);

  SignedMessage signed_message = vote_crypto_provider->signPrecommit(vote);
  // validate the signed message

}
