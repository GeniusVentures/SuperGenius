/**
 * @file       consensus_pending_lifecycle_test.cpp
 * @brief      Focused harness for consensus pending proposal lifecycle tests.
 * @details    Provides the Wave 0 target for PEND-01 through PEND-07 so
 *             later implementation plans can add D-01 through D-12 and D-16
 *             behavior cases without changing CTest wiring.
 * @date       2026-06-16
 */

#include <gtest/gtest.h>

#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"

#include <array>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgns
{
    /**
     * @brief Test-only helper surface for future private pending lifecycle access.
     * @details Later implementation tasks can extend this accessor when
     *          ConsensusManager exposes local pending metadata, typed dependency
     *          indexes, retry scheduling, and expiry cleanup.
     */
    class ConsensusPendingLifecycleTestAccess
    {
    public:
        static constexpr const char *Scope()
        {
            return "consensus pending lifecycle";
        }
    };
} // namespace sgns

namespace
{
    constexpr std::array<const char *, 12> kConsensusPendingBehaviors = {
        "D-01 structured deferred validation result",
        "D-02 local-only pending dependencies",
        "D-03 typed Certificate dependency key",
        "D-04 retry on any dependency arrival",
        "D-05 scheduled transient retry backoff",
        "D-06 dependency-triggered retry throttle",
        "D-07 idempotent retry approval",
        "D-08 global pending count limit",
        "D-09 per-proposer pending count limit",
        "D-10 retained-byte admission limit",
        "D-11 local capacity refusal without reject vote",
        "D-12 pending TTL expiry"
    };

    class ConsensusPendingLifecycleTest : public ::testing::Test
    {
    protected:
        static std::vector<std::string> PendingBehaviorNames()
        {
            return std::vector<std::string>(
                kConsensusPendingBehaviors.begin(),
                kConsensusPendingBehaviors.end() );
        }
    };
} // namespace

TEST_F( ConsensusPendingLifecycleTest, HarnessIsDiscoverable )
{
    /**
     * Given a dedicated consensus pending lifecycle CTest target,
     * When the current Wave 0 harness is discovered,
     * Then it exposes the future behavior slots for PEND-01 through PEND-07
     * and avoids bridge or EVM RPC concerns.
     */
    const auto behaviors = PendingBehaviorNames();

    ASSERT_EQ( behaviors.size(), kConsensusPendingBehaviors.size() );
    EXPECT_STREQ( sgns::ConsensusPendingLifecycleTestAccess::Scope(),
                  "consensus pending lifecycle" );
    EXPECT_FALSE( sgns::NONCE_SUBJECT_TYPE.empty() );
}

TEST_F( ConsensusPendingLifecycleTest, ValidationResultPreservesTerminalChecks )
{
    /**
     * Given the local structured validation result contract,
     * When terminal outcomes are constructed,
     * Then each result preserves the legacy consensus check value without
     * adding local pending dependency metadata.
     */
    const auto approve = sgns::ConsensusManager::ValidationResult::Approve();
    const auto reject  = sgns::ConsensusManager::ValidationResult::Reject();
    const auto stalled = sgns::ConsensusManager::ValidationResult::Stalled();

    EXPECT_EQ( approve.check, sgns::ConsensusManager::Check::Approve );
    EXPECT_TRUE( approve.dependencies.empty() );
    EXPECT_FALSE( approve.retry_after.has_value() );

    EXPECT_EQ( reject.check, sgns::ConsensusManager::Check::Reject );
    EXPECT_TRUE( reject.dependencies.empty() );

    EXPECT_EQ( stalled.check, sgns::ConsensusManager::Check::Stalled );
    EXPECT_TRUE( stalled.dependencies.empty() );
}

TEST_F( ConsensusPendingLifecycleTest, PendingResultCarriesTypedCertificateDependencyAndRetryMetadata )
{
    /**
     * Given a proposal waiting for a predecessor certificate,
     * When Pending is constructed with a Certificate dependency key,
     * Then the typed dependency and optional retry metadata stay local to the
     * structured result.
     */
    using PendingDependencyKey = sgns::ConsensusManager::PendingDependencyKey;

    const auto dependency = PendingDependencyKey::Certificate( "tx-previous-cert" );
    const auto pending    = sgns::ConsensusManager::ValidationResult::Pending(
        { dependency },
        std::chrono::seconds( 2 ) );

    ASSERT_EQ( pending.check, sgns::ConsensusManager::Check::Pending );
    ASSERT_EQ( pending.dependencies.size(), 1U );
    EXPECT_EQ( pending.dependencies.front().type, PendingDependencyKey::Type::Certificate );
    EXPECT_EQ( pending.dependencies.front().value, "tx-previous-cert" );
    ASSERT_TRUE( pending.retry_after.has_value() );
    EXPECT_EQ( pending.retry_after.value(), std::chrono::seconds( 2 ) );
}

TEST_F( ConsensusPendingLifecycleTest, PendingDependencyKeySupportsHashIdentity )
{
    /**
     * Given typed dependency keys are used as local pending indexes,
     * When identical and different Certificate keys are inserted into an
     * unordered map,
     * Then identical keys address the same entry and different values stay
     * isolated.
     */
    using PendingDependencyKey     = sgns::ConsensusManager::PendingDependencyKey;
    using PendingDependencyKeyHash = sgns::ConsensusManager::PendingDependencyKeyHash;

    std::unordered_map<PendingDependencyKey, int, PendingDependencyKeyHash> index;
    index.emplace( PendingDependencyKey::Certificate( "tx-a" ), 1 );
    index[PendingDependencyKey::Certificate( "tx-a" )] += 1;
    index.emplace( PendingDependencyKey::Certificate( "tx-b" ), 7 );

    EXPECT_EQ( index.size(), 2U );
    EXPECT_EQ( index.at( PendingDependencyKey::Certificate( "tx-a" ) ), 2 );
    EXPECT_EQ( index.at( PendingDependencyKey::Certificate( "tx-b" ) ), 7 );
}
