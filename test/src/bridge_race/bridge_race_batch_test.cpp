/**
 * @file       bridge_race_batch_test.cpp
 * @brief      Phase 8 D-04: 3-5 concurrently seeded burns each mint exactly once with no
 *             cross-burn interference.
 * @date       2026-07-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 *
 * Seeds kBatchBurnCount (4, within D-04's 3-5 range) distinct burns to 4 distinct
 * Light-node destinations BEFORE any node's RPC endpoint is configured, then releases
 * all 11 nodes' ConfigureRpcEndpoint calls back-to-back with no waits in between (D-03).
 * Asserts each of the 4 burned destinations independently reaches exactly kMintAmount on
 * every node, and that unburned Light destinations show no balance increase (cross-burn
 * interference check) — zero manual MintTokens()/MintFunds() calls anywhere in this file.
 */

#include "bridge_race_fixture.hpp"

#include <algorithm>
#include <thread>

namespace
{
    /** @brief Number of concurrently seeded burns in this batch (within D-04's 3-5 range). */
    constexpr unsigned int kBatchBurnCount = 4u;
} // namespace

TEST_F( BridgeRaceE2ETest, BatchBurnsNoInterference )
{
    std::vector<std::string> dest_addrs;
    std::vector<uint64_t>    initial_balances;
    dest_addrs.reserve( kBatchBurnCount );
    initial_balances.reserve( kBatchBurnCount );

    // Light indices 1..kBatchBurnCount get burns; indices (kBatchBurnCount+1)..10 stay
    // unburned and serve as the cross-burn-interference control group.
    for ( unsigned int i = 1u; i <= kBatchBurnCount; ++i )
    {
        const std::string dest = DeriveLightDestination( i );
        dest_addrs.push_back( dest );
        initial_balances.push_back( s_nodes[0]->GetBalance( dest ) );
    }

    std::vector<std::string> unburned_dest_addrs;
    std::vector<uint64_t>    unburned_initial_balances;
    for ( unsigned int i = kBatchBurnCount + 1u; i < BridgeRaceE2ETest::kNodeCount; ++i )
    {
        const std::string dest = DeriveLightDestination( i );
        unburned_dest_addrs.push_back( dest );
        unburned_initial_balances.push_back( s_nodes[0]->GetBalance( dest ) );
    }

    // Seed all kBatchBurnCount burns BEFORE any ConfigureRpcEndpoint call (D-03).
    for ( unsigned int i = 0u; i < kBatchBurnCount; ++i )
    {
        const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
            s_anvil.RpcUrl(), static_cast<uint64_t>( BridgeRaceE2ETest::kMintAmount ), dest_addrs[i] );
        ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed batch burn #" << i;
        spdlog::info( "bridge_race batch: seeded burn #{} dest={} tx_hash={}",
                      i,
                      dest_addrs[i].substr( 0, 16 ),
                      tx_hash );
    }

    sgns::WeightedRpcEndpoint ep_direct;
    ep_direct.url                     = s_anvil.RpcUrl();
    ep_direct.consensus_weight        = 100;
    ep_direct.bridge_contract_address = sgns::test::anvil::kSepoliaBridgeContractLower;
    ep_direct.accepted_topic0_hashes  = { sgns::test::anvil::BridgeEventTopic0() };

    sgns::WeightedRpcEndpoint ep_public1 = ep_direct;
    ep_public1.consensus_weight = 0;

    sgns::WeightedRpcEndpoint ep_public2 = ep_direct;
    ep_public2.consensus_weight = 0;

    std::vector<sgns::WeightedRpcEndpoint> anvil_eps{ ep_direct, ep_public1, ep_public2 };

    // Release all 11 nodes' RPC endpoints together, back-to-back, with NO waits inside
    // the loop (D-03 — the race-window trigger).
    for ( unsigned int i = 0u; i < BridgeRaceE2ETest::kNodeCount; ++i )
    {
        ASSERT_TRUE( s_nodes[i]->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, anvil_eps ) )
            << "Node " << i << " rejected RPC endpoint configuration after READY";
    }
    spdlog::info( "bridge_race batch: released ConfigureRpcEndpoint on all {} nodes",
                  BridgeRaceE2ETest::kNodeCount );

    // Every burned destination must independently reach exactly kMintAmount on every node.
    EXPECT_WAIT_FOR_CONDITION(
        [&]()
        {
            for ( unsigned int b = 0u; b < kBatchBurnCount; ++b )
            {
                const bool all_nodes_minted = std::all_of(
                    s_nodes.begin(),
                    s_nodes.end(),
                    [&]( const std::shared_ptr<GeniusNode> &node )
                    {
                        return node->GetBalance( dest_addrs[b] ) >=
                               initial_balances[b] + BridgeRaceE2ETest::kMintAmount;
                    } );
                if ( !all_nodes_minted )
                {
                    return false;
                }
            }
            return true;
        },
        BridgeRaceE2ETest::kRaceNodeReadyTimeout,
        "All 11 nodes must independently mint each of the batch burns exactly once",
        nullptr );

    // Stability/double-mint check: actually elapse one additional watcher poll window
    // (not a wait-for-already-true-condition, which would return immediately) so a
    // delayed double-mint on the next poll cycle has time to manifest before the final
    // exact-balance assertions below.
    std::this_thread::sleep_for( BridgeRaceE2ETest::kRaceStabilityWindow );
    ASSERT_EQ( s_nodes[0]->GetState(), GeniusNode::NodeState::READY )
        << "node 0 must remain READY across the stability window (liveness check)";

    // Each burned destination's final balance must equal EXACTLY initial + kMintAmount
    // on every node (no double-mint, no cross-burn interference inflating the amount).
    for ( unsigned int b = 0u; b < kBatchBurnCount; ++b )
    {
        for ( unsigned int i = 0u; i < BridgeRaceE2ETest::kNodeCount; ++i )
        {
            const uint64_t final_balance = s_nodes[i]->GetBalance( dest_addrs[b] );
            EXPECT_EQ( final_balance, initial_balances[b] + BridgeRaceE2ETest::kMintAmount )
                << "Node " << i << " destination #" << b << " must mint exactly once (no double-mint)";
        }
    }

    // Cross-burn interference check: unburned Light destinations must show NO balance
    // increase on any node.
    for ( unsigned int u = 0u; u < unburned_dest_addrs.size(); ++u )
    {
        for ( unsigned int i = 0u; i < BridgeRaceE2ETest::kNodeCount; ++i )
        {
            const uint64_t final_balance = s_nodes[i]->GetBalance( unburned_dest_addrs[u] );
            EXPECT_EQ( final_balance, unburned_initial_balances[u] )
                << "Node " << i << " unburned destination #" << u
                << " must show no balance increase (cross-burn interference check)";
        }
    }

    spdlog::info( "bridge_race batch: exactly-once mint verified across all {} nodes for {} "
                  "concurrent burns, no cross-burn interference",
                  BridgeRaceE2ETest::kNodeCount,
                  kBatchBurnCount );
}
