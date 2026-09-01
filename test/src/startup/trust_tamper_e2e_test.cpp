#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/BurnConfig.hpp"
#include "account/GeniusSigner.hpp"
#include "account/TrustStartupController.hpp"
#include "base/buffer.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "trustedpeer/TrustStateStore.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::account;
    using namespace sgns::trustedpeer;

    class TrustTamperE2ETest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            boost::filesystem::create_directories( path_ );
            signers_ = { GeniusSigner::Generate(), GeniusSigner::Generate(), GeniusSigner::Generate() };
            node_    = test::securecrdt::MakeSecureCrdtTestNode( "trust_tamper" );
            ASSERT_NE( node_, nullptr );
            secure_              = std::make_shared<securecrdt::SecureCrdt>( node_->db, "trust-tamper-topic" );
            store_               = TrustStateStore::Open( ( path_ / "trust" ).string(), 42 ).value();
            manifest_.network_id = 42;
            manifest_.bootstrapper_public_key = signers_[0].GetAddress();
            manifest_.peers = { signers_[0].GetAddress(), signers_[1].GetAddress(), signers_[2].GetAddress() };
            manifest_.membership_threshold = 2;
            manifest_.burn_threshold       = 2;
            older_                         = ConfirmBurnV1();
            durable_                       = AdvanceToV2( older_ );
        }

        ConfirmedTrustSnapshot ConfirmBurnV1()
        {
            auto initial = store_->CommitGenesis( manifest_, signers_[0].Sign( manifest_.CanonicalBytes().value() ) )
                               .value();
            const auto core  = BurnConfig::BurnCandidateCore( initial.burn ).value();
            const auto bytes = core.CanonicalBytes().value();
            return store_
                ->CommitBurnSuccessor( initial.burn,
                                       { { signers_[0].GetAddress(), signers_[0].Sign( bytes ) },
                                         { signers_[1].GetAddress(), signers_[1].Sign( bytes ) } },
                                       bytes )
                .value();
        }

        ConfirmedTrustSnapshot AdvanceToV2( const ConfirmedTrustSnapshot &current )
        {
            auto       policy      = current.policy;
            const auto policy_hash = policy.Hash().value();
            ++policy.version;
            policy.expected_previous_hash  = policy_hash;
            policy.authorizing_policy_hash = policy_hash;
            const auto policy_bytes        = policy.CanonicalBytes().value();
            auto       policy_result       = store_
                                     ->CommitPolicySuccessor(
                                         policy,
                                         { { signers_[0].GetAddress(), signers_[0].Sign( policy_bytes ) },
                                           { signers_[1].GetAddress(), signers_[1].Sign( policy_bytes ) } } )
                                     .value();
            auto       burn               = policy_result.burn;
            const auto previous_burn_hash = burn.Hash().value();
            ++burn.version;
            burn.expected_previous_hash  = previous_burn_hash;
            burn.authorizing_policy_hash = policy_result.policy.Hash().value();
            burn.basis_points            = 125;
            const auto burn_bytes        = burn.CanonicalBytes().value();
            return store_
                ->CommitBurnSuccessor( burn,
                                       { { signers_[0].GetAddress(), signers_[0].Sign( burn_bytes ) },
                                         { signers_[1].GetAddress(), signers_[1].Sign( burn_bytes ) } } )
                .value();
        }

        void TearDown() override
        {
            secure_.reset();
            node_.reset();
            store_.reset();
            boost::filesystem::remove_all( path_ );
        }

        boost::filesystem::path                               path_;
        std::vector<GeniusSigner>                             signers_;
        std::unique_ptr<test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<securecrdt::SecureCrdt>               secure_;
        std::shared_ptr<TrustStateStore>                      store_;
        GenesisManifest                                       manifest_;
        ConfirmedTrustSnapshot                                older_;
        ConfirmedTrustSnapshot                                durable_;
    };
}

TEST_F( TrustTamperE2ETest, MissingOlderAndForkReplicatedStateNeverReplaceLastKnownGood )
{
    std::vector<TrustStartupController::Event> events;
    auto                                       controller = TrustStartupController::New(
                          secure_,
                          store_,
                          std::nullopt,
                          signers_[2].GetAddress(),
                          [this]( const std::vector<uint8_t> &bytes ) { return signers_[2].Sign( bytes ); },
                          [&]( const auto &event ) { events.push_back( event ); } )
                          .value();
    ASSERT_TRUE( controller->ObserveReplicatedSnapshot( std::nullopt ).has_value() );
    ASSERT_TRUE( controller->ObserveReplicatedSnapshot( older_ ).has_value() );
    auto fork              = durable_;
    fork.burn.basis_points = 999;
    ASSERT_TRUE( controller->ObserveReplicatedSnapshot( fork ).has_value() );
    ASSERT_EQ( events.size(), 3U );
    EXPECT_EQ( events[0].code, TrustStartupController::EventCode::TRUST_CRDT_MISSING );
    EXPECT_EQ( events[1].code, TrustStartupController::EventCode::TRUST_CRDT_ROLLBACK );
    EXPECT_EQ( events[2].code, TrustStartupController::EventCode::TRUST_CRDT_FORK );
    EXPECT_EQ( store_->LoadAndVerify().value(), durable_ );
}

TEST_F( TrustTamperE2ETest, CorruptLocalManifestFailsClosedWithoutJsonFallback )
{
    secure_.reset();
    node_.reset();
    store_.reset();
    auto       raw = storage::rocksdb::create( ( path_ / "trust" ).string() ).value();
    const auto key = base::Buffer{}.put( "trust/version-1/network/42/genesis" );
    ASSERT_TRUE( raw->put( key, base::Buffer{}.put( "tampered-manifest" ) ).has_value() );
    raw.reset();

    node_ = test::securecrdt::MakeSecureCrdtTestNode( "trust_tamper_corrupt" );
    ASSERT_NE( node_, nullptr );
    secure_ = std::make_shared<securecrdt::SecureCrdt>( node_->db, "trust-tamper-corrupt-topic" );
    store_  = TrustStateStore::Open( ( path_ / "trust" ).string(), 42 ).value();
    std::vector<TrustStartupController::Event> events;
    auto                                       started = TrustStartupController::New(
        secure_,
        store_,
        manifest_,
        signers_[2].GetAddress(),
        [this]( const std::vector<uint8_t> &bytes ) { return signers_[2].Sign( bytes ); },
        [&]( const auto &event ) { events.push_back( event ); } );
    ASSERT_TRUE( started.has_error() );
    EXPECT_EQ( started.error(), TrustStateStore::Error::CORRUPT_GENESIS );
    ASSERT_EQ( events.size(), 1U );
    EXPECT_EQ( events.front().code, TrustStartupController::EventCode::TRUST_LOCAL_STATE_CORRUPT );
    EXPECT_TRUE( store_->LoadAndVerify().has_error() );
}
