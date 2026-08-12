#include <gtest/gtest.h>

#include <algorithm>

#include <boost/filesystem/operations.hpp>

#include "account/BurnConfig.hpp"
#include "account/GeniusSigner.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/rocksdb/rocksdb_batch.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::account;
    using namespace sgns::trustedpeer;

    class BurnConfigPolicyE2ETest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            boost::filesystem::create_directories( path_ );
            for ( size_t i = 0; i < 4; ++i ) signers_.push_back( GeniusSigner::Generate() );
            node_ = test::securecrdt::MakeSecureCrdtTestNode( "burnconfig_policy" );
            ASSERT_NE( node_, nullptr );
            secure_crdt_ = std::make_shared<securecrdt::SecureCrdt>( node_->db, "burnconfig-policy-topic" );
            store_ = TrustStateStore::Open(
                         ( path_ / "trust" ).string(),
                         42,
                         [this]( storage::rocksdb &database, const std::vector<TrustStateStore::Write> &writes )
                             -> outcome::result<void>
                         {
                             if ( fail_commits_ ) return outcome::failure( std::errc::io_error );
                             auto batch = database.batch();
                             for ( const auto &[key, value] : writes )
                             {
                                 auto put = batch->put( key, value );
                                 if ( put.has_error() ) return put.error();
                             }
                             return batch->commit();
                         } )
                         .value();
            const auto manifest = Manifest();
            tpr_ = TrustedPeerRegistry::NewProduction(
                       secure_crdt_, store_, manifest, signers_[0].Sign( manifest.CanonicalBytes().value() ),
                       signers_[0].GetAddress(),
                       [this]( const std::vector<uint8_t> &bytes ) { return signers_[0].Sign( bytes ); } )
                       .value();
            burn_ = BurnConfig::NewProduction(
                        secure_crdt_, tpr_, store_, signers_[0].GetAddress(),
                        [this]( const std::vector<uint8_t> &bytes ) { return signers_[0].Sign( bytes ); } )
                        .value();
            ASSERT_TRUE( secure_crdt_->RegisterFilters() );
        }

        void TearDown() override
        {
            burn_.reset();
            tpr_.reset();
            secure_crdt_.reset();
            node_.reset();
            store_.reset();
            boost::filesystem::remove_all( path_ );
        }

        GenesisManifest Manifest() const
        {
            GenesisManifest manifest;
            manifest.network_id = 42;
            manifest.bootstrapper_public_key = signers_[0].GetAddress();
            manifest.peers = { signers_[2].GetAddress(), signers_[0].GetAddress(), signers_[1].GetAddress() };
            manifest.membership_threshold = 2;
            manifest.burn_threshold = 2;
            return manifest;
        }

        securecrdt::CandidateApprovalRecord Approval( const securecrdt::CandidateCore &core, size_t signer ) const
        {
            return { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                     core,
                     signers_[signer].GetAddress(),
                     signers_[signer].Sign( core.CanonicalBytes().value() ) };
        }

        void ConfirmGenesisAndBurn()
        {
            ASSERT_TRUE( tpr_->SubmitReviewedGenesisApproval().has_value() );
            auto genesis = burn_->OnTrustedPeerGenesisConfirmed();
            ASSERT_TRUE( genesis.has_value() ) << genesis.error().message();
            auto approvals = secure_crdt_->ReadCandidateApprovals( genesis.value() ).value();
            ASSERT_EQ( approvals.size(), 1U );
            ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( approvals.front().core, 1 ) ).has_value() );
            ASSERT_TRUE( burn_->TryActivateBurnCandidate( genesis.value() ).has_value() );
            ASSERT_TRUE( burn_->IsEconomicallyReady() );
        }

        boost::filesystem::path path_;
        std::vector<GeniusSigner> signers_;
        std::unique_ptr<test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<securecrdt::SecureCrdt> secure_crdt_;
        std::shared_ptr<TrustStateStore> store_;
        std::shared_ptr<TrustedPeerRegistry> tpr_;
        std::shared_ptr<BurnConfig> burn_;
        bool fail_commits_ = false;
    };
}

TEST_F( BurnConfigPolicyE2ETest, GenesisWaitsForTrustedPeerConfirmationAndExactBurnQuorum )
{
    EXPECT_FALSE( burn_->IsEconomicallyReady() );
    EXPECT_TRUE( burn_->ListPendingBurnCandidates().has_error() );
    EXPECT_TRUE( burn_->OnTrustedPeerGenesisConfirmed().has_error() );
    ASSERT_TRUE( tpr_->SubmitReviewedGenesisApproval().has_value() );
    auto genesis = burn_->OnTrustedPeerGenesisConfirmed();
    ASSERT_TRUE( genesis.has_value() );
    EXPECT_FALSE( burn_->IsEconomicallyReady() );
    auto approvals = secure_crdt_->ReadCandidateApprovals( genesis.value() ).value();
    ASSERT_EQ( approvals.size(), 1U );
    const auto first_hash = approvals.front().core.Hash();
    auto reordered_manifest = Manifest();
    std::reverse( reordered_manifest.peers.begin(), reordered_manifest.peers.end() );
    EXPECT_EQ( reordered_manifest.Fingerprint(), Manifest().Fingerprint() );
    auto same_burn = store_->LoadAndVerify().value().burn;
    EXPECT_EQ( BurnConfig::BurnCandidateCore( same_burn )->Hash(), first_hash );
    auto repeated = burn_->OnTrustedPeerGenesisConfirmed();
    ASSERT_TRUE( repeated.has_value() );
    EXPECT_EQ( repeated.value(), genesis.value() );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( genesis.value() ).value().size(), 1U );
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( approvals.front().core, 1 ) ).has_value() );
    ASSERT_TRUE( burn_->TryActivateBurnCandidate( genesis.value() ).has_value() );
    EXPECT_TRUE( burn_->IsEconomicallyReady() );
    EXPECT_EQ( burn_->GetCachedBasisPoints(), 100U );
    EXPECT_EQ( first_hash, approvals.front().core.Hash() );
}

TEST_F( BurnConfigPolicyE2ETest, PolicyBindingStalesOldCandidateAndLaterVersionsNeedExplicitApproval )
{
    ConfirmGenesisAndBurn();
    auto v2 = burn_->ProposeBurnCandidate( 250 );
    ASSERT_TRUE( v2.has_value() );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( v2.value() ).value().size(), 1U );
    EXPECT_TRUE( burn_->ApproveBurnCandidate( v2.value() ).has_value() );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( v2.value() ).value().size(), 1U );

    auto next_policy = tpr_->GetConfirmedSnapshot().value().policy;
    const auto old_policy_hash = next_policy.Hash().value();
    next_policy.version += 1;
    next_policy.expected_previous_hash = old_policy_hash;
    next_policy.authorizing_policy_hash = old_policy_hash;
    next_policy.peers = { signers_[0].GetAddress(), signers_[2].GetAddress(), signers_[3].GetAddress() };
    auto policy_id = tpr_->ProposePolicyCandidate( next_policy ).value();
    auto policy_core = TrustedPeerRegistry::PolicyCandidateCore( next_policy ).value();
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( policy_core, 1 ) ).has_value() );
    ASSERT_TRUE( tpr_->TryActivatePolicyCandidate( policy_id ).has_value() );

    EXPECT_TRUE( burn_->TryActivateBurnCandidate( v2.value() ).has_error() );
    EXPECT_TRUE( secure_crdt_->SubmitCandidateApproval(
        Approval( secure_crdt_->ReadCandidateApprovals( v2.value() ).value().front().core, 2 ) ).has_error() );
    auto fresh = burn_->ProposeBurnCandidate( 250 );
    ASSERT_TRUE( fresh.has_value() );
    EXPECT_NE( fresh.value().content_hash, v2.value().content_hash );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( fresh.value() ).value().size(), 1U );
}

TEST_F( BurnConfigPolicyE2ETest, PersistBeforeCacheLeavesConfirmedValueAndCallbacksUnchangedOnFailure )
{
    ConfirmGenesisAndBurn();
    uint32_t callback_count = 0;
    burn_->RegisterRefreshCallback( [&]( uint64_t ) { ++callback_count; } );
    const auto previous = burn_->GetCachedBasisPoints();
    auto candidate = burn_->ProposeBurnCandidate( 333 ).value();
    auto core = secure_crdt_->ReadCandidateApprovals( candidate ).value().front().core;
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval( Approval( core, 1 ) ).has_value() );
    fail_commits_ = true;
    EXPECT_TRUE( burn_->TryActivateBurnCandidate( candidate ).has_error() );
    EXPECT_EQ( burn_->GetCachedBasisPoints(), previous );
    EXPECT_TRUE( burn_->IsEconomicallyReady() );
    EXPECT_EQ( callback_count, 0U );
}
