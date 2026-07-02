/**
 * @file       transaction_manager_pending_lifecycle_test.cpp
 * @brief      Focused harness for TransactionManager pending lifecycle tests.
 * @details    Provides the Wave 0 target for TXSTATE-01 plus PEND-03,
 *             PEND-06, and PEND-07 integration behavior so later tasks can
 *             add D-13 through D-16 cases without changing CTest wiring.
 * @date       2026-06-16
 */

#include <gtest/gtest.h>

#include "account/TransactionManager.hpp"
#include "blockchain/Consensus.hpp"
#include "testutil/storage/base_crdt_test.hpp"

#include <array>
#include <string>
#include <vector>

namespace sgns
{
    /**
     * @brief Test-only helper surface for future private TransactionManager access.
     * @details Later implementation tasks can extend this accessor when pending
     *          expiry needs to verify local outgoing UNCONFIRMED state, remote
     *          temporary record cleanup, and proven-invalid FAILED transitions.
     */
    class TransactionManagerPendingLifecycleTestAccess
    {
    public:
        static constexpr const char *Scope()
        {
            return "transaction manager pending lifecycle";
        }

        static bool IsUnconfirmedDistinctFromFailed()
        {
            return TransactionManager::TransactionStatus::UNCONFIRMED != TransactionManager::TransactionStatus::FAILED;
        }
    };
} // namespace sgns

namespace
{
    constexpr std::array<const char *, 4> kTransactionPendingBehaviors = {
        "D-13 local outgoing inconclusive expiry becomes UNCONFIRMED",
        "D-14 no automatic resubmission from UNCONFIRMED",
        "D-15 remote temporary VERIFYING entry is removed",
        "D-16 expiry removes transaction and consensus pending state"
    };

    /**
     * @brief CRDT-backed fixture shape for future TransactionManager lifecycle cases.
     * @details This deliberately avoids GeniusNode network startup. Later tests can
     *          construct GeniusAccount, Blockchain, and TransactionManager directly
     *          using the certificate fallback fixture pattern.
     */
    class TransactionManagerPendingLifecycleTest : public test::CRDTFixture
    {
    public:
        TransactionManagerPendingLifecycleTest()
            : CRDTFixture( "transaction_manager_pending_lifecycle_test" )
        {
        }

    protected:
        static std::vector<std::string> PendingBehaviorNames()
        {
            return std::vector<std::string>(
                kTransactionPendingBehaviors.begin(),
                kTransactionPendingBehaviors.end() );
        }
    };
} // namespace

TEST_F( TransactionManagerPendingLifecycleTest, HarnessIsDiscoverable )
{
    /**
     * Given a CRDT-backed TransactionManager pending lifecycle CTest target,
     * When the current Wave 0 harness is discovered,
     * Then it exposes the future behavior slots for TXSTATE-01 and D-13
     * through D-16 without GeniusNode network startup.
     */
    const auto behaviors = PendingBehaviorNames();

    ASSERT_EQ( behaviors.size(), kTransactionPendingBehaviors.size() );
    EXPECT_STREQ( sgns::TransactionManagerPendingLifecycleTestAccess::Scope(),
                  "transaction manager pending lifecycle" );
    EXPECT_FALSE( sgns::NONCE_SUBJECT_TYPE.empty() );
}

TEST( TransactionManagerPendingLifecycleContractTest, UnconfirmedAndCertificatePendingContractsExist )
{
    using PendingDependencyKey = sgns::ConsensusManager::PendingDependencyKey;

    EXPECT_TRUE( sgns::TransactionManagerPendingLifecycleTestAccess::IsUnconfirmedDistinctFromFailed() );

    const auto pending = sgns::ConsensusManager::ValidationResult::Pending(
        { PendingDependencyKey::Certificate( "previous-tx" ) } );

    ASSERT_EQ( pending.check, sgns::ConsensusManager::Check::Pending );
    ASSERT_EQ( pending.dependencies.size(), 1U );
    EXPECT_EQ( pending.dependencies.front().type, PendingDependencyKey::Type::Certificate );
    EXPECT_EQ( pending.dependencies.front().value, "previous-tx" );
}
