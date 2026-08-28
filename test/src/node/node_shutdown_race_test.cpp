/**
 * @file       node_shutdown_race_test.cpp
 * @brief      Regression test for the GeniusNode shutdown race: the node
 *             destructor tears down the pubsub while a blockchain-retry timer
 *             is pending and the account-messenger worker may still hold
 *             queued blockchain tasks that call GossipPubSub::getPeerCount.
 * @date       2026-08-27
 *
 * Exposure (in-process boot + teardown, offline): Blockchain::Start() fails
 * with BLOCKCHAIN_NOT_INITIALIZED because the validator registry never
 * initializes without peers, the node schedules a 5s retry it must cancel or
 * gate at shutdown, and the messenger worker must be drained before the
 * pubsub gossip object is released.
 */
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

#include <chrono>
#include <string>
#include "testutil/genius_node_test_access.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include <boost/dll/runtime_symbol_info.hpp>
#include <gtest/gtest.h>

using namespace sgns;

namespace sgns
{
    /// Number of boot/teardown cycles; the crash is interleaving-dependent,
    /// so the cycle repeats to cover the race window repeatedly. Two cycles
    /// keep CI cost near two teardowns (~20s worst case) while still hitting
    /// the window twice unfixed (verified: faults without the fix).
    constexpr unsigned int kShutdownRaceIterations = 2;

    /// Upper bound for the offline blockchain-start failure to schedule its retry.
    constexpr std::chrono::milliseconds kRetryScheduledTimeout{ 30000 };

    /**
     * @class NodeShutdownRaceTest
     * @brief Fixture for node shutdown race regression tests.
     */
    class NodeShutdownRaceTest : public ::testing::Test
    {
    protected:
        /**
         * @brief Point the account at an in-memory secure storage so the test
         *        never touches the developer's real key store.
         */
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
        }
    };

    /**
     * @brief       Boot one offline node, wait until its blockchain retry is
     *              pending, then destroy the node inside the race window.
     * @param[in]   iteration Cycle index, used to isolate on-disk state.
     */
    static void RunBootTeardownCycle( unsigned int iteration )
    {
        boost::filesystem::path path = boost::dll::program_location().parent_path() /
                                       ( "node_shutdown_race_" + std::to_string( iteration ) );

        try
        {
            sgns::test::removeAllWithRetry( path.string() );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }

        const auto base_write_path = path.generic_string() + '/';
        sgns::GeniusNode::WriteNetworkConfig( base_write_path, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( base_write_path,
                                           /*node_type=*/"Full",
                                           /*is_processor=*/false,
                                           /*rpc_catchup=*/false );

        auto node = sgns::GeniusNode::New(
            { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), base_write_path },
            sgns::FromPrivateKey{ "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" } );
        ASSERT_NE( node, nullptr );

        // Offline (auto_dht off, no bootstrap) the validator registry never
        // initializes, so Blockchain::Start() fails and the node schedules a
        // 5s retry. Retry count > 0 marks that the pending-retry window is open.
        std::chrono::milliseconds retry_wait_duration{ 0 };
        ASSERT_WAIT_FOR_CONDITION( [&]() { return GeniusNodeTestAccess::BlockchainRetryCount( node ) > 0; },
                                   kRetryScheduledTimeout,
                                   "blockchain retry scheduled after offline start failure",
                                   &retry_wait_duration );

        // Tear the node down while the retry timer is pending. The destructor
        // must gate the retry and drain the messenger worker before the pubsub
        // is destroyed; a use-after-free here faults the process, so finishing
        // every cycle cleanly is the assertion.
        node.reset();
        ASSERT_EQ( node, nullptr );

        try
        {
            sgns::test::removeAllWithRetry( path.string() );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }
    }

    /**
     * @brief Repeated boot + teardown during the pending-blockchain-retry
     *        window must complete without faulting.
     */
    TEST_F( NodeShutdownRaceTest, ShutdownDuringPendingBlockchainRetryCompletesCleanly )
    {
        for ( unsigned int iteration = 0; iteration < kShutdownRaceIterations; ++iteration )
        {
            RunBootTeardownCycle( iteration );
        }
    }
} // namespace sgns
