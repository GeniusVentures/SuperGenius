#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "account/GeniusSigner.hpp"
#include "securecrdt/SecureCrdtCandidate.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/rocksdb/rocksdb_batch.hpp"
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

        ConfirmedTrustSnapshot ConfirmInitialBurn( const std::shared_ptr<TrustStateStore> &store,
                                                   const ConfirmedTrustSnapshot            &snapshot ) const
        {
            const auto burn_bytes = snapshot.burn.CanonicalBytes().value();
            const sgns::securecrdt::CandidateCore core{
                sgns::securecrdt::CandidateCore::ENCODING_VERSION,
                "burn-config",
                snapshot.burn.network_id,
                sgns::securecrdt::CandidateKind::BurnConfig,
                snapshot.burn.version,
                snapshot.burn.expected_previous_hash,
                snapshot.burn.authorizing_policy_hash,
                burn_bytes,
            };
            const auto authorization = core.CanonicalBytes().value();
            auto result = store->CommitBurnSuccessor( snapshot.burn, Sign( authorization ), authorization );
            EXPECT_TRUE( result.has_value() );
            return result.value();
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
    EXPECT_EQ( snapshot.burn_authorization, BurnAuthorizationKind::BootstrapOnly );
    snapshot = ConfirmInitialBurn( opened.value(), snapshot );
    EXPECT_EQ( snapshot.burn_authorization, BurnAuthorizationKind::PeerQuorum );

    auto next_policy = snapshot.policy;
    next_policy.version++;
    next_policy.expected_previous_hash = *snapshot.policy.Hash();
    next_policy.authorizing_policy_hash = *snapshot.policy.Hash();
    auto policy_bytes = next_policy.CanonicalBytes();
    ASSERT_TRUE( policy_bytes.has_value() );
    auto policy_result = opened.value()->CommitPolicySuccessor( next_policy, Sign( *policy_bytes ) );
    ASSERT_TRUE( policy_result.has_value() );

    auto next_burn = policy_result.value().burn;
    const auto previous_burn_hash = next_burn.Hash().value();
    next_burn.version++;
    next_burn.expected_previous_hash = previous_burn_hash;
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
    const auto expected = ConfirmInitialBurn( store, CommitGenesis( store ) );
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
    auto initial = ConfirmInitialBurn( store, CommitGenesis( store ) );
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

TEST_F( TrustStateStoreTest, CorruptAndPartialRecordsReturnTypedFailuresWithoutRepairingDisk )
{
    auto store = TrustStateStore::Open( path_.string(), 42 ).value();
    const auto snapshot = CommitGenesis( store );
    store.reset();

    auto raw = sgns::storage::rocksdb::create( path_.string() ).value();
    auto key = []( const std::string &text ) { return sgns::base::Buffer{}.put( text ); };
    const std::string prefix = "trust/version-1/network/42/";
    const auto genesis_key = key( prefix + "genesis" );
    const auto original_genesis = raw->get( genesis_key ).value();
    ASSERT_TRUE( raw->put( genesis_key, key( "corrupt" ) ).has_value() );
    raw.reset();
    auto corrupt = TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify();
    ASSERT_TRUE( corrupt.has_error() );
    EXPECT_EQ( corrupt.error(), TrustStateStore::Error::CORRUPT_GENESIS );
    raw = sgns::storage::rocksdb::create( path_.string() ).value();
    ASSERT_TRUE( raw->put( genesis_key, original_genesis ).has_value() );

    const auto policy_hash = snapshot.policy.Hash().value();
    const auto policy_key = key( prefix + "policy/version-1/" + policy_hash );
    const auto original_policy = raw->get( policy_key ).value();
    ASSERT_TRUE( raw->remove( policy_key ).has_value() );
    raw.reset();
    auto missing_policy = TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify();
    ASSERT_TRUE( missing_policy.has_error() );
    EXPECT_EQ( missing_policy.error(), TrustStateStore::Error::MISSING_POLICY_RECORD );
    raw = sgns::storage::rocksdb::create( path_.string() ).value();
    ASSERT_TRUE( raw->put( policy_key, original_policy ).has_value() );

    const auto burn_hash = snapshot.burn.Hash().value();
    const auto burn_key = key( prefix + "burn/version-1/" + burn_hash );
    const auto original_burn = raw->get( burn_key ).value();
    ASSERT_TRUE( raw->put( burn_key, key( "corrupt" ) ).has_value() );
    raw.reset();
    auto corrupt_burn = TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify();
    ASSERT_TRUE( corrupt_burn.has_error() );
    EXPECT_EQ( corrupt_burn.error(), TrustStateStore::Error::CORRUPT_BURN_RECORD );
    raw = sgns::storage::rocksdb::create( path_.string() ).value();
    ASSERT_TRUE( raw->put( burn_key, original_burn ).has_value() );
    raw.reset();

    EXPECT_EQ( TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify().value(), snapshot );
}

TEST_F( TrustStateStoreTest, CommitFailureBeforeBatchLeavesDurableStateAndPublicationUnchanged )
{
    std::atomic_uint32_t publication_count{ 0 };
    auto fail_before_commit = []( sgns::storage::rocksdb &, const std::vector<TrustStateStore::Write> & )
        -> outcome::result<void> { return outcome::failure( std::errc::io_error ); };
    auto store = TrustStateStore::Open( path_.string(), 42, fail_before_commit ).value();
    auto manifest = Manifest();
    auto result = store->CommitGenesis( manifest, signers_[0].Sign( manifest.CanonicalBytes().value() ) );
    if ( result.has_value() ) ++publication_count;
    ASSERT_TRUE( result.has_error() );
    EXPECT_EQ( result.error(), TrustStateStore::Error::COMMIT_FAILED );
    EXPECT_EQ( publication_count.load(), 0U );
    store.reset();
    auto reopened = TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify();
    ASSERT_TRUE( reopened.has_error() );
    EXPECT_EQ( reopened.error(), TrustStateStore::Error::NOT_FOUND );
}

TEST_F( TrustStateStoreTest, CommitFailureAfterDurableBatchRecoversNewHeadWithoutPublication )
{
    std::atomic_uint32_t publication_count{ 0 };
    auto fail_after_commit = []( sgns::storage::rocksdb &database,
                                 const std::vector<TrustStateStore::Write> &writes ) -> outcome::result<void> {
        auto batch = database.batch();
        for ( const auto &[key, value] : writes )
        {
            auto put = batch->put( key, value );
            if ( put.has_error() ) return put.error();
        }
        auto commit = batch->commit();
        if ( commit.has_error() ) return commit.error();
        return outcome::failure( std::errc::io_error );
    };
    auto store = TrustStateStore::Open( path_.string(), 42, fail_after_commit ).value();
    auto manifest = Manifest();
    auto result = store->CommitGenesis( manifest, signers_[0].Sign( manifest.CanonicalBytes().value() ) );
    if ( result.has_value() ) ++publication_count;
    ASSERT_TRUE( result.has_error() );
    EXPECT_EQ( publication_count.load(), 0U );
    store.reset();
    auto recovered = TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify();
    ASSERT_TRUE( recovered.has_value() );
    EXPECT_EQ( recovered.value().genesis_fingerprint, manifest.Fingerprint().value() );
}

TEST_F( TrustStateStoreTest, ConcurrentSuccessorsHaveOneWinnerAndOneStableStaleResult )
{
    auto store = TrustStateStore::Open( path_.string(), 42 ).value();
    const auto initial = ConfirmInitialBurn( store, CommitGenesis( store ) );
    auto first = initial.policy;
    first.version++;
    first.expected_previous_hash = initial.policy.Hash().value();
    first.authorizing_policy_hash = initial.policy.Hash().value();
    auto second = first;
    second.membership_threshold = 3;
    const auto first_proof = Sign( first.CanonicalBytes().value() );
    const auto second_proof = Sign( second.CanonicalBytes().value() );

    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    size_t ready = 0;
    bool go = false;
    std::array<std::optional<std::error_code>, 2> errors;
    auto run = [&]( size_t index, const QuorumPolicyState &candidate,
                    const sgns::multisig::CollectedSignatures &proof ) {
        {
            std::unique_lock<std::mutex> lock( gate_mutex );
            ++ready;
            gate_cv.notify_all();
            gate_cv.wait( lock, [&] { return go; } );
        }
        auto result = store->CommitPolicySuccessor( candidate, proof );
        if ( result.has_error() ) errors[index] = result.error();
    };
    std::thread a( run, 0, std::cref( first ), std::cref( first_proof ) );
    std::thread b( run, 1, std::cref( second ), std::cref( second_proof ) );
    {
        std::unique_lock<std::mutex> lock( gate_mutex );
        gate_cv.wait( lock, [&] { return ready == 2; } );
        go = true;
    }
    gate_cv.notify_all();
    a.join();
    b.join();

    EXPECT_EQ( static_cast<unsigned>( errors[0].has_value() ) + static_cast<unsigned>( errors[1].has_value() ), 1U );
    const auto loser = errors[0].has_value() ? *errors[0] : *errors[1];
    EXPECT_EQ( loser, TrustStateStore::Error::STALE_HEAD );
    const auto winner = store->LoadAndVerify().value();
    store.reset();
    EXPECT_EQ( TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify().value(), winner );
}

TEST_F( TrustStateStoreTest, PolicySuccessorRejectedUntilInitialBurnPeerConfirmed )
{
    auto store   = TrustStateStore::Open( path_.string(), 42 ).value();
    auto initial = CommitGenesis( store );
    EXPECT_EQ( initial.burn_authorization, BurnAuthorizationKind::BootstrapOnly );

    auto policy_v2 = initial.policy;
    policy_v2.version++;
    policy_v2.expected_previous_hash  = initial.policy.Hash().value();
    policy_v2.authorizing_policy_hash = initial.policy.Hash().value();
    const auto policy_bytes = policy_v2.CanonicalBytes().value();
    const sgns::securecrdt::CandidateCore policy_core{
        sgns::securecrdt::CandidateCore::ENCODING_VERSION,
        "trusted-peer-policy",
        policy_v2.network_id,
        sgns::securecrdt::CandidateKind::TrustPolicy,
        policy_v2.version,
        policy_v2.expected_previous_hash,
        policy_v2.authorizing_policy_hash,
        policy_bytes,
    };
    const auto policy_authorization = policy_core.CanonicalBytes().value();

    auto rejected = store->CommitPolicySuccessor( policy_v2, Sign( policy_authorization ), policy_authorization );
    ASSERT_TRUE( rejected.has_error() );
    EXPECT_EQ( rejected.error(), TrustStateStore::Error::INITIAL_BURN_NOT_CONFIRMED );
    EXPECT_EQ( store->LoadAndVerify().value(), initial );

    store.reset();
    store = TrustStateStore::Open( path_.string(), 42 ).value();
    EXPECT_EQ( store->LoadAndVerify().value(), initial );

    const auto burn_bytes = initial.burn.CanonicalBytes().value();
    const sgns::securecrdt::CandidateCore burn_core{
        sgns::securecrdt::CandidateCore::ENCODING_VERSION,
        "burn-config",
        initial.burn.network_id,
        sgns::securecrdt::CandidateKind::BurnConfig,
        initial.burn.version,
        initial.burn.expected_previous_hash,
        initial.burn.authorizing_policy_hash,
        burn_bytes,
    };
    const auto burn_authorization = burn_core.CanonicalBytes().value();
    auto confirmed = store->CommitBurnSuccessor( initial.burn, Sign( burn_authorization ), burn_authorization );
    ASSERT_TRUE( confirmed.has_value() );
    EXPECT_EQ( confirmed.value().burn_authorization, BurnAuthorizationKind::PeerQuorum );

    auto committed = store->CommitPolicySuccessor( policy_v2, Sign( policy_authorization ), policy_authorization );
    ASSERT_TRUE( committed.has_value() );
    store.reset();
    auto reopened = TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify();
    ASSERT_TRUE( reopened.has_value() );
    EXPECT_EQ( reopened.value().burn_authorization, BurnAuthorizationKind::PeerQuorum );
    EXPECT_EQ( reopened.value(), committed.value() );
}

TEST_F( TrustStateStoreTest, BurnV2RejectedUntilInitialBurnPeerConfirmed )
{
    auto store   = TrustStateStore::Open( path_.string(), 42 ).value();
    auto initial = CommitGenesis( store );
    EXPECT_EQ( initial.burn_authorization, BurnAuthorizationKind::BootstrapOnly );

    auto burn_v2 = initial.burn;
    burn_v2.version++;
    burn_v2.expected_previous_hash  = initial.burn.Hash().value();
    burn_v2.authorizing_policy_hash = initial.policy.Hash().value();
    burn_v2.basis_points            = 250;
    const auto burn_v2_bytes = burn_v2.CanonicalBytes().value();
    const sgns::securecrdt::CandidateCore burn_v2_core{
        sgns::securecrdt::CandidateCore::ENCODING_VERSION,
        "burn-config",
        burn_v2.network_id,
        sgns::securecrdt::CandidateKind::BurnConfig,
        burn_v2.version,
        burn_v2.expected_previous_hash,
        burn_v2.authorizing_policy_hash,
        burn_v2_bytes,
    };
    const auto burn_v2_authorization = burn_v2_core.CanonicalBytes().value();

    auto rejected = store->CommitBurnSuccessor( burn_v2, Sign( burn_v2_authorization ), burn_v2_authorization );
    ASSERT_TRUE( rejected.has_error() );
    EXPECT_EQ( rejected.error(), TrustStateStore::Error::INITIAL_BURN_NOT_CONFIRMED );
    EXPECT_EQ( store->LoadAndVerify().value(), initial );

    store.reset();
    store = TrustStateStore::Open( path_.string(), 42 ).value();
    EXPECT_EQ( store->LoadAndVerify().value(), initial );

    const auto burn_v1_bytes = initial.burn.CanonicalBytes().value();
    const sgns::securecrdt::CandidateCore burn_v1_core{
        sgns::securecrdt::CandidateCore::ENCODING_VERSION,
        "burn-config",
        initial.burn.network_id,
        sgns::securecrdt::CandidateKind::BurnConfig,
        initial.burn.version,
        initial.burn.expected_previous_hash,
        initial.burn.authorizing_policy_hash,
        burn_v1_bytes,
    };
    const auto burn_v1_authorization = burn_v1_core.CanonicalBytes().value();
    auto confirmed = store->CommitBurnSuccessor( initial.burn, Sign( burn_v1_authorization ), burn_v1_authorization );
    ASSERT_TRUE( confirmed.has_value() );
    EXPECT_EQ( confirmed.value().burn_authorization, BurnAuthorizationKind::PeerQuorum );

    auto committed = store->CommitBurnSuccessor( burn_v2, Sign( burn_v2_authorization ), burn_v2_authorization );
    ASSERT_TRUE( committed.has_value() );
    store.reset();
    auto reopened = TrustStateStore::Open( path_.string(), 42 ).value()->LoadAndVerify();
    ASSERT_TRUE( reopened.has_value() );
    EXPECT_EQ( reopened.value().burn_authorization, BurnAuthorizationKind::PeerQuorum );
    EXPECT_EQ( reopened.value(), committed.value() );
}

TEST_F( TrustStateStoreTest, RollbackBoundaryIsDocumentedByThePublicContract )
{
    SUCCEED() << "Whole-disk rollback requires a TPM, OS-keystore monotonic counter, or off-host checkpoint.";
}
