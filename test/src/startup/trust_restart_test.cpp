#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/BurnConfig.hpp"
#include "account/GeniusSigner.hpp"
#include "account/TrustStartupController.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "trustedpeer/TrustStateStore.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::account;
    using namespace sgns::trustedpeer;

    class TrustRestartTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            boost::filesystem::create_directories( path_ );
            signers_ = { GeniusSigner::Generate(), GeniusSigner::Generate(), GeniusSigner::Generate() };
            node_    = test::securecrdt::MakeSecureCrdtTestNode( "trust_restart" );
            ASSERT_NE( node_, nullptr );
            secure_              = std::make_shared<securecrdt::SecureCrdt>( node_->db, "trust-restart-topic" );
            store_               = TrustStateStore::Open( ( path_ / "trust" ).string(), 42 ).value();
            manifest_.network_id = 42;
            manifest_.bootstrapper_public_key = signers_[0].GetAddress();
            manifest_.peers = { signers_[0].GetAddress(), signers_[1].GetAddress(), signers_[2].GetAddress() };
            manifest_.membership_threshold = 2;
            manifest_.burn_threshold       = 2;
            auto initial = store_->CommitGenesis( manifest_, signers_[0].Sign( manifest_.CanonicalBytes().value() ) )
                               .value();
            const auto core  = BurnConfig::BurnCandidateCore( initial.burn ).value();
            const auto bytes = core.CanonicalBytes().value();
            expected_        = store_
                            ->CommitBurnSuccessor( initial.burn,
                                                   { { signers_[0].GetAddress(), signers_[0].Sign( bytes ) },
                                                     { signers_[1].GetAddress(), signers_[1].Sign( bytes ) } },
                                                   bytes )
                            .value();
        }

        void TearDown() override
        {
            secure_.reset();
            node_.reset();
            store_.reset();
            boost::filesystem::remove_all( path_ );
        }

        outcome::result<std::shared_ptr<TrustStartupController>> Start(
            std::optional<GenesisManifest>              diagnostic,
            std::vector<TrustStartupController::Event> &events )
        {
            return TrustStartupController::New(
                secure_,
                store_,
                std::move( diagnostic ),
                signers_[2].GetAddress(),
                [this]( const std::vector<uint8_t> &bytes ) { return signers_[2].Sign( bytes ); },
                [&]( const auto &event ) { events.push_back( event ); } );
        }

        boost::filesystem::path                               path_;
        std::vector<GeniusSigner>                             signers_;
        std::unique_ptr<test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<securecrdt::SecureCrdt>               secure_;
        std::shared_ptr<TrustStateStore>                      store_;
        GenesisManifest                                       manifest_;
        ConfirmedTrustSnapshot                                expected_;
    };
}

TEST_F( TrustRestartTest, OmittedTrustConfigRestoresIdenticalDurableAuthority )
{
    std::vector<TrustStartupController::Event> events;
    auto                                       started = Start( std::nullopt, events );
    ASSERT_TRUE( started.has_value() ) << started.error().message();
    EXPECT_EQ( started.value()->GetState(), TrustStartupController::State::ConfirmedReady );
    EXPECT_TRUE( started.value()->IsEconomicallyReady() );
    EXPECT_EQ( started.value()->GetCurrentPeers(), expected_.policy.peers );
    EXPECT_EQ( started.value()->registry()->GetConfirmedSnapshot().value(), expected_ );
    EXPECT_TRUE( events.empty() );
}

TEST_F( TrustRestartTest, EveryTrustFieldConflictEmitsOneStructuredCriticalAndKeepsHeads )
{
    struct Mutation
    {
        const char                              *field;
        std::function<void( GenesisManifest & )> apply;
    };

    const std::vector<Mutation> mutations{
        { "trusted_peers", [&]( auto &value ) { value.peers = { signers_[0].GetAddress() }; } },
        { "bootstrapper_node", [&]( auto &value ) { value.bootstrapper_public_key = signers_[2].GetAddress(); } },
        { "trusted_peer_quorum_threshold", []( auto &value ) { value.membership_threshold = 1; } },
        { "trusted_peer_quorum_threshold", []( auto &value ) { value.membership_threshold = 4; } },
        { "burn_config_quorum_threshold", []( auto &value ) { value.burn_threshold = 1; } },
        { "burn_config_quorum_threshold", []( auto &value ) { value.burn_threshold = 4; } },
    };
    for ( const auto &mutation : mutations )
    {
        SCOPED_TRACE( mutation.field );
        auto configured = manifest_;
        mutation.apply( configured );
        std::vector<TrustStartupController::Event> events;
        auto                                       started = Start( configured, events );
        ASSERT_TRUE( started.has_value() ) << started.error().message();
        ASSERT_EQ( events.size(), 1U );
        EXPECT_EQ( events.front().code, TrustStartupController::EventCode::TRUST_CONFIG_CONFLICT );
        EXPECT_EQ( events.front().fields, std::vector<std::string>{ mutation.field } );
        EXPECT_EQ( events.front().persisted_fingerprint, expected_.genesis_fingerprint );
        EXPECT_EQ( started.value()->registry()->GetConfirmedSnapshot().value(), expected_ );
        started.value().reset();
    }
}

TEST_F( TrustRestartTest, NetworkMismatchIsFatalAndDoesNotMutateDurableAuthority )
{
    auto wrong_network       = manifest_;
    wrong_network.network_id = 43;
    std::vector<TrustStartupController::Event> events;
    auto                                       started = Start( wrong_network, events );
    ASSERT_TRUE( started.has_error() );
    EXPECT_EQ( started.error(), TrustStateStore::Error::NETWORK_MISMATCH );
    ASSERT_EQ( events.size(), 1U );
    EXPECT_EQ( events.front().code, TrustStartupController::EventCode::TRUST_NETWORK_MISMATCH );
    EXPECT_EQ( store_->LoadAndVerify().value(), expected_ );
}
