#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"

namespace
{
    using sgns::ValidatorRegistry;
    using UpdateVerification = ValidatorRegistry::UpdateVerification;

    // Builds a one-validator registry snapshot with a distinct epoch so tests
    // can assert WHICH chain position the loader returned (the classifier keys
    // verdicts by epoch, which is unique per chain position in every fixture).
    sgns::validator::Registry MakeRegistry( uint64_t epoch, const std::string &validator_id )
    {
        sgns::validator::Registry registry;
        registry.set_epoch( epoch );
        auto *entry = registry.add_validators();
        entry->set_validator_id( validator_id );
        entry->set_role( sgns::validator::FULL );
        entry->set_status( sgns::validator::ACTIVE );
        entry->set_weight( 100 );
        return registry;
    }

    // Builds an update linking a registry snapshot to its prev-registry CID.
    sgns::validator::RegistryUpdate MakeUpdate( sgns::validator::Registry registry, const std::string &prev_cid )
    {
        sgns::validator::RegistryUpdate update;
        *update.mutable_registry() = std::move( registry );
        update.set_prev_registry_hash( prev_cid );
        return update;
    }

    // In-memory stand-in for the GlobalDB-backed raw fetch + the
    // VerifyUpdateClassified decision, so the chain walk is exercised without
    // any node wiring (EvaluateRegularPromotionStatic test pattern).
    //
    // The verdict map models the real classification contract:
    //  - an update with a NON-empty prev verifies iff its signatures/quorum
    //    check out against the base registry its prev link names (attacker-
    //    controlled bases can make this pass -- the attack TRUST-01 closes);
    //  - an update with an EMPTY prev (chain root) verifies iff it carries the
    //    genesis-authority signature. A self-certified root therefore maps to
    //    kInvalid and an honest genesis root to kValid;
    //  - kMissingDependency models a base whose dependencies have not synced.
    class FakeChain
    {
    public:
        std::map<uint64_t, UpdateVerification> verdict_by_epoch;
        std::set<std::string>                  unfetchable_cids;
        int                                    fetch_calls = 0;

        void Store( const std::string &cid, sgns::validator::RegistryUpdate update )
        {
            updates_by_cid_[cid] = std::move( update );
        }

        outcome::result<sgns::validator::RegistryUpdate> Fetch( const std::string &cid )
        {
            ++fetch_calls;
            if ( unfetchable_cids.count( cid ) != 0 )
            {
                return outcome::failure( std::errc::no_such_file_or_directory );
            }
            const auto it = updates_by_cid_.find( cid );
            if ( it == updates_by_cid_.end() )
            {
                return outcome::failure( std::errc::no_such_file_or_directory );
            }
            return it->second;
        }

        UpdateVerification Classify( const sgns::validator::RegistryUpdate &update ) const
        {
            const auto it = verdict_by_epoch.find( update.registry().epoch() );
            return it == verdict_by_epoch.end() ? UpdateVerification::kInvalid : it->second;
        }

        std::function<outcome::result<sgns::validator::RegistryUpdate>( const std::string & )> Fetcher()
        {
            return [this]( const std::string &cid ) { return Fetch( cid ); };
        }

        std::function<UpdateVerification( const sgns::validator::RegistryUpdate & )> Classifier()
        {
            return [this]( const sgns::validator::RegistryUpdate &update ) { return Classify( update ); };
        }

    private:
        std::map<std::string, sgns::validator::RegistryUpdate> updates_by_cid_;
    };

    // ADVERSARIAL (TRUST-01, finding #1): attacker authority is a peer
    // supplying a forged, self-certified registry CID. Every hop "verifies"
    // against an attacker-controlled base, but the chain root has an empty
    // prev link WITHOUT the genesis-authority signature, so the chain never
    // roots in the genesis authority -> permanently unauthorized.
    TEST( ValidatorRegistryVerifiedLoadTest, ForgedSelfCertifiedChainIsUnauthorized )
    {
        FakeChain chain;
        chain.Store( "head", MakeUpdate( MakeRegistry( 2, "attacker" ), "forged-root" ) );
        chain.Store( "forged-root", MakeUpdate( MakeRegistry( 1, "attacker" ), "" ) );
        chain.verdict_by_epoch = { { 2, UpdateVerification::kValid }, { 1, UpdateVerification::kInvalid } };

        ValidatorRegistry::VerifiedRegistryMemo memo;
        auto                                    result = ValidatorRegistry::VerifyRegistryChainClassified( "head",
                                                                        chain.Fetcher(),
                                                                        chain.Classifier(),
                                                                        memo );
        ASSERT_TRUE( result.has_error() );
        EXPECT_EQ( result.error(), std::errc::permission_denied );
        EXPECT_EQ( memo.Size(), 0U );
    }

    // ADVERSARIAL (TRUST-01): the forged chain points its root at ANOTHER
    // self-signed update (cycle-shaped loitering) instead of genesis. The walk
    // must detect the revisit instead of looping forever, and the verdict is
    // unauthorized (a cyclic chain cannot be genesis-rooted).
    TEST( ValidatorRegistryVerifiedLoadTest, CyclicForgedChainIsUnauthorized )
    {
        FakeChain chain;
        chain.Store( "head", MakeUpdate( MakeRegistry( 3, "attacker" ), "a" ) );
        chain.Store( "a", MakeUpdate( MakeRegistry( 2, "attacker" ), "b" ) );
        chain.Store( "b", MakeUpdate( MakeRegistry( 1, "attacker" ), "a" ) );
        chain.verdict_by_epoch = { { 3, UpdateVerification::kValid },
                                   { 2, UpdateVerification::kValid },
                                   { 1, UpdateVerification::kValid } };

        ValidatorRegistry::VerifiedRegistryMemo memo;
        auto                                    result = ValidatorRegistry::VerifyRegistryChainClassified( "head",
                                                                        chain.Fetcher(),
                                                                        chain.Classifier(),
                                                                        memo );
        ASSERT_TRUE( result.has_error() );
        EXPECT_EQ( result.error(), std::errc::permission_denied );
        EXPECT_EQ( memo.Size(), 0U );
    }

    // ADVERSARIAL (TRUST-01): a mid-chain update signed by a non-quorum set
    // fails classification -> the loader reports the chain as unauthorized
    // (permission_denied), never retryable.
    TEST( ValidatorRegistryVerifiedLoadTest, MidChainNonQuorumUpdateIsUnauthorized )
    {
        FakeChain chain;
        chain.Store( "head", MakeUpdate( MakeRegistry( 2, "honest" ), "weak-mid" ) );
        chain.Store( "weak-mid", MakeUpdate( MakeRegistry( 1, "honest" ), "genesis" ) );
        chain.Store( "genesis", MakeUpdate( MakeRegistry( 0, "genesis-authority" ), "" ) );
        chain.verdict_by_epoch = { { 2, UpdateVerification::kValid },
                                   { 1, UpdateVerification::kInvalid },
                                   { 0, UpdateVerification::kValid } };

        ValidatorRegistry::VerifiedRegistryMemo memo;
        auto                                    result = ValidatorRegistry::VerifyRegistryChainClassified( "head",
                                                                        chain.Fetcher(),
                                                                        chain.Classifier(),
                                                                        memo );
        ASSERT_TRUE( result.has_error() );
        EXPECT_EQ( result.error(), std::errc::permission_denied );
    }

    // COMPAT: a registry whose ancestor update has not synced locally yet must
    // classify as retryable (resource_unavailable_try_again), never as
    // unauthorized -- missing CRDT evidence is an ordering problem.
    TEST( ValidatorRegistryVerifiedLoadTest, UnfetchableAncestorIsRetryable )
    {
        FakeChain chain;
        chain.Store( "head", MakeUpdate( MakeRegistry( 2, "honest" ), "not-synced-yet" ) );
        chain.unfetchable_cids.insert( "not-synced-yet" );
        chain.verdict_by_epoch = { { 2, UpdateVerification::kValid } };

        ValidatorRegistry::VerifiedRegistryMemo memo;
        auto                                    result = ValidatorRegistry::VerifyRegistryChainClassified( "head",
                                                                        chain.Fetcher(),
                                                                        chain.Classifier(),
                                                                        memo );
        ASSERT_TRUE( result.has_error() );
        EXPECT_EQ( result.error(), std::errc::resource_unavailable_try_again );
        EXPECT_NE( result.error(), std::errc::permission_denied );
        EXPECT_EQ( memo.Size(), 0U );
    }

    // COMPAT: classification returning kMissingDependency (base registry not
    // synced yet) maps to the retryable error, never to unauthorized.
    TEST( ValidatorRegistryVerifiedLoadTest, MissingDependencyVerdictIsRetryable )
    {
        FakeChain chain;
        chain.Store( "head", MakeUpdate( MakeRegistry( 2, "honest" ), "ancestor" ) );
        chain.verdict_by_epoch = { { 2, UpdateVerification::kMissingDependency } };

        ValidatorRegistry::VerifiedRegistryMemo memo;
        auto                                    result = ValidatorRegistry::VerifyRegistryChainClassified( "head",
                                                                        chain.Fetcher(),
                                                                        chain.Classifier(),
                                                                        memo );
        ASSERT_TRUE( result.has_error() );
        EXPECT_EQ( result.error(), std::errc::resource_unavailable_try_again );
        EXPECT_NE( result.error(), std::errc::permission_denied );
        EXPECT_EQ( memo.Size(), 0U );
    }

    // COMPAT: an honest, genesis-terminated chain loads successfully and
    // returns the HEAD registry snapshot (not an ancestor's).
    TEST( ValidatorRegistryVerifiedLoadTest, GenesisTerminatedHonestChainLoads )
    {
        FakeChain chain;
        chain.Store( "head", MakeUpdate( MakeRegistry( 2, "honest-head" ), "mid" ) );
        chain.Store( "mid", MakeUpdate( MakeRegistry( 1, "honest-mid" ), "genesis" ) );
        chain.Store( "genesis", MakeUpdate( MakeRegistry( 0, "genesis-authority" ), "" ) );
        chain.verdict_by_epoch = { { 2, UpdateVerification::kValid },
                                   { 1, UpdateVerification::kValid },
                                   { 0, UpdateVerification::kValid } };

        ValidatorRegistry::VerifiedRegistryMemo memo;
        auto                                    result = ValidatorRegistry::VerifyRegistryChainClassified( "head",
                                                                        chain.Fetcher(),
                                                                        chain.Classifier(),
                                                                        memo );
        ASSERT_FALSE( result.has_error() );
        EXPECT_EQ( result.value().epoch(), 2U );
        ASSERT_EQ( result.value().validators().size(), 1 );
        EXPECT_EQ( result.value().validators( 0 ).validator_id(), "honest-head" );
        // Every walked CID is memoized.
        EXPECT_EQ( memo.Size(), 3U );
        EXPECT_TRUE( memo.Contains( "head" ) );
        EXPECT_TRUE( memo.Contains( "mid" ) );
        EXPECT_TRUE( memo.Contains( "genesis" ) );
    }

    // COMPAT: a CID present in the verified memo terminates the walk without
    // further fetches (fetch-call count asserted) and returns the memoized
    // snapshot.
    TEST( ValidatorRegistryVerifiedLoadTest, MemoizedCidTerminatesWalkWithoutFetches )
    {
        FakeChain chain;
        chain.Store( "head", MakeUpdate( MakeRegistry( 2, "honest-head" ), "genesis" ) );
        chain.Store( "genesis", MakeUpdate( MakeRegistry( 0, "genesis-authority" ), "" ) );
        chain.verdict_by_epoch = { { 2, UpdateVerification::kValid }, { 0, UpdateVerification::kValid } };

        ValidatorRegistry::VerifiedRegistryMemo memo;
        auto                                    first = ValidatorRegistry::VerifyRegistryChainClassified( "head",
                                                                       chain.Fetcher(),
                                                                       chain.Classifier(),
                                                                       memo );
        ASSERT_FALSE( first.has_error() );
        EXPECT_GT( chain.fetch_calls, 0 );

        const int fetches_after_first_walk = chain.fetch_calls;
        auto      second                   = ValidatorRegistry::VerifyRegistryChainClassified( "head",
                                                                        chain.Fetcher(),
                                                                        chain.Classifier(),
                                                                        memo );
        ASSERT_FALSE( second.has_error() );
        EXPECT_EQ( chain.fetch_calls, fetches_after_first_walk );
        EXPECT_EQ( second.value().epoch(), 2U );

        // A memoized ANCESTOR also terminates a deeper walk without fetching it.
        FakeChain fresh_chain;
        fresh_chain.Store( "new-head", MakeUpdate( MakeRegistry( 3, "honest-new" ), "head" ) );
        fresh_chain.verdict_by_epoch = { { 3, UpdateVerification::kValid } };
        auto third                   = ValidatorRegistry::VerifyRegistryChainClassified( "new-head",
                                                                       fresh_chain.Fetcher(),
                                                                       fresh_chain.Classifier(),
                                                                       memo );
        ASSERT_FALSE( third.has_error() );
        EXPECT_EQ( third.value().epoch(), 3U );
        EXPECT_EQ( fresh_chain.fetch_calls, 1 );
        EXPECT_EQ( memo.Size(), 3U );
    }

    // COMPAT: memo at cap evicts exactly the oldest entry per insertion and
    // never clears wholesale; the just-inserted CID survives.
    TEST( ValidatorRegistryVerifiedLoadTest, MemoCapEvictsOldestFirst )
    {
        // Chain of kMaxVerifiedRegistryMemoEntries + 1 positions: c0 is the
        // genesis root, c<cap> is the head.
        FakeChain  chain;
        const auto cap = ValidatorRegistry::kMaxVerifiedRegistryMemoEntries;
        for ( uint64_t index = 0; index <= cap; ++index )
        {
            const std::string prev = ( index == 0 ) ? std::string() : "c" + std::to_string( index - 1 );
            chain.Store( "c" + std::to_string( index ), MakeUpdate( MakeRegistry( index, "v" ), prev ) );
            chain.verdict_by_epoch[index] = UpdateVerification::kValid;
        }
        const std::string head_cid = "c" + std::to_string( cap );

        ValidatorRegistry::VerifiedRegistryMemo memo;
        auto                                    result = ValidatorRegistry::VerifyRegistryChainClassified( head_cid,
                                                                        chain.Fetcher(),
                                                                        chain.Classifier(),
                                                                        memo );
        ASSERT_FALSE( result.has_error() );
        // Exactly one entry (the oldest, c0) was evicted -- not a wholesale clear.
        EXPECT_EQ( memo.Size(), cap );
        EXPECT_FALSE( memo.Contains( "c0" ) );
        EXPECT_TRUE( memo.Contains( "c1" ) );
        EXPECT_TRUE( memo.Contains( head_cid ) );

        // Re-walking the evicted root re-inserts it and evicts exactly the next
        // oldest (c1); size stays at the cap.
        auto reloaded = ValidatorRegistry::VerifyRegistryChainClassified( "c0",
                                                                          chain.Fetcher(),
                                                                          chain.Classifier(),
                                                                          memo );
        ASSERT_FALSE( reloaded.has_error() );
        EXPECT_EQ( memo.Size(), cap );
        EXPECT_TRUE( memo.Contains( "c0" ) );
        EXPECT_FALSE( memo.Contains( "c1" ) );
        EXPECT_TRUE( memo.Contains( head_cid ) );
    }
} // namespace
