#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

#include "account/GeniusSigner.hpp"
#include "trustedpeer/TrustStateStore.hpp"

namespace
{
    using namespace sgns::trustedpeer;

    class TrustStateStoreTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            signers_.push_back( sgns::GeniusSigner::Generate() );
            signers_.push_back( sgns::GeniusSigner::Generate() );
            signers_.push_back( sgns::GeniusSigner::Generate() );
        }
        void TearDown() override { boost::filesystem::remove_all( path_ ); }

        GenesisManifest Manifest() const
        {
            GenesisManifest manifest;
            manifest.network_id                  = 42;
            manifest.bootstrapper_public_key     = signers_[0].GetAddress();
            manifest.peers                       = { signers_[0].GetAddress(), signers_[1].GetAddress(),
                                                     signers_[2].GetAddress() };
            manifest.membership_threshold        = 2;
            manifest.burn_threshold              = 2;
            return manifest;
        }

        ConfirmedTrustSnapshot CommitGenesis( const std::shared_ptr<TrustStateStore> &store ) const
        {
            auto manifest = Manifest();
            auto bytes = manifest.CanonicalBytes();
            EXPECT_TRUE( bytes.has_value() );
            auto result = store->CommitGenesis( manifest, signers_[0].Sign( *bytes ) );
            EXPECT_TRUE( result.has_value() );
            return result.value();
        }

        sgns::multisig::CollectedSignatures Sign( const std::vector<uint8_t> &bytes ) const
        {
            return { { signers_[0].GetAddress(), signers_[0].Sign( bytes ) },
                     { signers_[1].GetAddress(), signers_[1].Sign( bytes ) } };
        }

        boost::filesystem::path          path_;
        std::vector<sgns::GeniusSigner> signers_;
    };
}

TEST_F( TrustStateStoreTest, CommitAndReopenReproducesVerifiedHashesWithoutJson )
{
    auto opened = TrustStateStore::Open( path_.string(), 42 );
    ASSERT_TRUE( opened.has_value() );
    auto snapshot = CommitGenesis( opened.value() );

    auto next_policy = snapshot.policy;
    next_policy.version++;
    next_policy.expected_previous_hash = *snapshot.policy.Hash();
    next_policy.authorizing_policy_hash = *snapshot.policy.Hash();
    auto policy_bytes = next_policy.CanonicalBytes();
    ASSERT_TRUE( policy_bytes.has_value() );
    auto policy_result = opened.value()->CommitPolicySuccessor( next_policy, Sign( *policy_bytes ) );
    ASSERT_TRUE( policy_result.has_value() );

    auto next_burn = policy_result.value().burn;
    next_burn.version++;
    next_burn.expected_previous_hash = *next_burn.Hash();
    next_burn.authorizing_policy_hash = *policy_result.value().policy.Hash();
    next_burn.basis_points = 250;
    auto burn_bytes = next_burn.CanonicalBytes();
    ASSERT_TRUE( burn_bytes.has_value() );
    auto burn_result = opened.value()->CommitBurnSuccessor( next_burn, Sign( *burn_bytes ) );
    ASSERT_TRUE( burn_result.has_value() );

    opened.value().reset();
    auto reopened = TrustStateStore::Open( path_.string(), 42 );
    ASSERT_TRUE( reopened.has_value() );
    auto loaded = reopened.value()->LoadAndVerify();
    ASSERT_TRUE( loaded.has_value() );
    EXPECT_EQ( loaded.value(), burn_result.value() );
}

TEST_F( TrustStateStoreTest, WrongNetworkIsDistinctAndDoesNotMutateSnapshot )
{
    auto store = TrustStateStore::Open( path_.string(), 42 ).value();
    const auto expected = CommitGenesis( store );
    store.reset();
    auto wrong = TrustStateStore::Open( path_.string(), 43 ).value()->LoadAndVerify();
    ASSERT_TRUE( wrong.has_error() );
    EXPECT_EQ( wrong.error(), TrustStateStore::Error::NETWORK_MISMATCH );
    EXPECT_EQ( TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify().value(), expected );
}

TEST_F( TrustStateStoreTest, VersionAndLinkFailuresAreDistinctAndPreserveLastKnownGood )
{
    auto store = TrustStateStore::Open( path_.string(), 42 ).value();
    const auto expected = CommitGenesis( store );
    auto candidate = expected.policy;
    const auto current_hash = *expected.policy.Hash();

    candidate.version = expected.policy.version;
    EXPECT_EQ( store->CommitPolicySuccessor( candidate, {} ).error(), TrustStateStore::Error::VERSION_DECREASE );
    candidate.version = expected.policy.version + 2;
    EXPECT_EQ( store->CommitPolicySuccessor( candidate, {} ).error(), TrustStateStore::Error::VERSION_SKIP );
    candidate.version = expected.policy.version + 1;
    candidate.expected_previous_hash = std::string( 64, '1' );
    candidate.authorizing_policy_hash = current_hash;
    EXPECT_EQ( store->CommitPolicySuccessor( candidate, {} ).error(), TrustStateStore::Error::WRONG_PREDECESSOR );
    candidate.expected_previous_hash = current_hash;
    candidate.authorizing_policy_hash = std::string( 64, '2' );
    EXPECT_EQ( store->CommitPolicySuccessor( candidate, {} ).error(), TrustStateStore::Error::WRONG_AUTHORIZER );
    EXPECT_EQ( store->LoadAndVerify().value(), expected );
}

TEST_F( TrustStateStoreTest, ForkAttemptCannotReplaceDurableWinner )
{
    auto store = TrustStateStore::Open( path_.string(), 42 ).value();
    auto initial = CommitGenesis( store );
    auto winner = initial.policy;
    winner.version++;
    winner.expected_previous_hash = *initial.policy.Hash();
    winner.authorizing_policy_hash = *initial.policy.Hash();
    auto bytes = winner.CanonicalBytes().value();
    ASSERT_TRUE( store->CommitPolicySuccessor( winner, Sign( bytes ) ).has_value() );

    auto fork = winner;
    fork.membership_threshold = 3;
    auto fork_bytes = fork.CanonicalBytes().value();
    auto rejected = store->CommitPolicySuccessor( fork, Sign( fork_bytes ) );
    ASSERT_TRUE( rejected.has_error() );
    EXPECT_TRUE( rejected.error() == TrustStateStore::Error::VERSION_DECREASE ||
                 rejected.error() == TrustStateStore::Error::STALE_HEAD );
    EXPECT_EQ( store->LoadAndVerify().value().policy, winner.Canonicalized().value() );
}
