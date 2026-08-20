/**
 * @file       bridge_race_single_burn_test.cpp
 * @brief      Phase 8 D-01/D-02: single contested burn, exactly-once mint across 11 nodes.
 * @date       2026-07-16
 * @author     Henrique A Klein (hklein@gnus.ai)
 *
 * Seeds ONE burn to a Light-node destination BEFORE any node's RPC endpoint is
 * configured, then releases all 11 nodes' ConfigureRpcEndpoint calls back-to-back with
 * no waits in between (D-03), proving every node's watcher independently discovers the
 * same burn and mints it exactly once (D-01/D-02) — zero manual MintTokens()/MintFunds()
 * calls anywhere in this file.
 */

#include "bridge_race_fixture.hpp"

#include <algorithm>
#include <thread>

TEST_F( BridgeRaceE2ETest, SingleContestedBurnExactlyOnce )
{
    const std::string dest_addr = DeriveLightDestination( 1u );
    const uint64_t initial_balance = RequireActiveBalance( s_nodes[0], dest_addr );

    spdlog::info( "bridge_race single_burn: dest={} initial_balance={}",
                  dest_addr.substr( 0, 8 ),
                  initial_balance );

    // Seed the single contested burn BEFORE any ConfigureRpcEndpoint call (D-03).
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";
    spdlog::info( "bridge_race single_burn: seeded burn tx_hash={}", tx_hash );

    // Build the RPC endpoint slots once (mirrors 08-PATTERNS.md's ep_direct/ep_public1/
    // ep_public2 construction).
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
        ASSERT_FALSE( RequireActiveAddress( s_nodes[i] ).empty() )
            << "Node " << i << " is not active-ready before RPC endpoint configuration";
        ASSERT_TRUE( s_nodes[i]->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, anvil_eps ) )
            << "Node " << i << " rejected RPC endpoint configuration after READY";
    }
    spdlog::info( "bridge_race single_burn: released ConfigureRpcEndpoint on all {} nodes",
                  BridgeRaceE2ETest::kNodeCount );

    // D-01/D-02: every one of the 11 nodes' watchers must independently discover and
    // mint the same burn.
    EXPECT_WAIT_FOR_CONDITION(
        [&]()
        {
            return std::all_of( s_nodes.begin(),
                                 s_nodes.end(),
                                 [&]( const std::shared_ptr<GeniusNode> &node )
                                 { return RequireActiveBalance( node, dest_addr ) >= initial_balance + kMintAmount; } );
        },
        BridgeRaceE2ETest::kRaceNodeReadyTimeout,
        "All 11 nodes must independently mint the contested burn exactly once",
        nullptr );

    const uint64_t balance_before_stability_window = RequireActiveBalance( s_nodes[0], dest_addr );

    // Stability/double-mint check: actually elapse one additional watcher poll window
    // (not a wait-for-already-true-condition, which would return immediately) so a
    // delayed double-mint on the next poll cycle has time to manifest before the final
    // exact-balance assertions below.
    std::this_thread::sleep_for( BridgeRaceE2ETest::kRaceStabilityWindow );
    ASSERT_EQ( s_nodes[0]->GetState(), GeniusNode::NodeState::READY )
        << "node 0 must remain READY across the stability window (liveness check)";

    // Every node's final balance must equal EXACTLY initial + kMintAmount (not >=) —
    // this is the double-mint guard (D-02).
    for ( unsigned int i = 0u; i < BridgeRaceE2ETest::kNodeCount; ++i )
    {
        const uint64_t final_balance = RequireActiveBalance( s_nodes[i], dest_addr );
        EXPECT_EQ( final_balance, initial_balance + kMintAmount )
            << "Node " << i << " must mint the contested burn exactly once (no double-mint)";
    }

    spdlog::info( "bridge_race single_burn: exactly-once mint verified across all {} nodes "
                  "(balance stable at {} through stability window)",
                  BridgeRaceE2ETest::kNodeCount,
                  balance_before_stability_window );
}
