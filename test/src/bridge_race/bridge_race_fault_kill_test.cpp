/**
 * @file       bridge_race_fault_kill_test.cpp
 * @brief      Phase 8 D-10: node-kill (object-lifecycle destruction) mid-mint fault test.
 * @date       2026-07-17
 * @author     Henrique A. Klein (hklein@gnus.ai)
 *
 * Destroys one Light node's GeniusNode instance (s_nodes[kKilledNodeIndex].reset())
 * IMMEDIATELY after releasing all 11 nodes' RPC endpoints (D-03), before any
 * convergence wait, then asserts the REMAINING 10 nodes still converge to
 * exactly-once mint for a DIFFERENT Light node's destination. D-10 is scoped to
 * object-lifecycle destruction only (no SIGKILL/fork/subprocess-level crash — that is
 * explicitly deferred per 08-CONTEXT.md).
 */

#include "bridge_race_fixture.hpp"

#include <algorithm>

TEST_F( BridgeRaceE2ETest, NodeKillMidMintStillConverges )
{
    // kKilledNodeIndex is a Light node distinct from kDestinationIndex, so the killed
    // node's absence never removes the destination whose balance we assert on.
    constexpr unsigned int kKilledNodeIndex = 5u;
    constexpr unsigned int kDestinationIndex = 1u;

    const std::string dest_addr = DeriveLightDestination( kDestinationIndex );
    const uint64_t initial_balance = RequireActiveBalance( s_nodes[0], dest_addr );

    spdlog::info( "bridge_race fault_kill: dest={} initial_balance={} killed_index={}",
                  dest_addr.substr( 0, 16 ),
                  initial_balance,
                  kKilledNodeIndex );

    // Seed the single contested burn BEFORE any ConfigureRpcEndpoint call (D-03).
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";
    spdlog::info( "bridge_race fault_kill: seeded burn tx_hash={}", tx_hash );

    // Build the RPC endpoint slots (simple same-URL 3-slot pattern — this test is about
    // node lifecycle, not RPC disagreement, so all 3 slots point at the real Anvil URL).
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
    spdlog::info( "bridge_race fault_kill: released ConfigureRpcEndpoint on all {} nodes",
                  BridgeRaceE2ETest::kNodeCount );

    // D-10: destroy the killed node's object IMMEDIATELY, before any convergence wait,
    // to genuinely test "mid-mint" rather than post-convergence destruction. Object
    // destruction only — no SIGKILL/fork/subprocess spawning (explicitly deferred).
    s_nodes[kKilledNodeIndex].reset();
    spdlog::info( "bridge_race fault_kill: destroyed node index {} mid-mint", kKilledNodeIndex );

    // Assert the REMAINING 10 nodes still converge to exactly-once mint for the
    // destination (a DIFFERENT Light node than the killed one).
    EXPECT_WAIT_FOR_CONDITION(
        [&]()
        {
            return std::all_of(
                s_nodes.begin(),
                s_nodes.end(),
                [&]( const std::shared_ptr<GeniusNode> &node )
                {
                    // The killed node's slot is now nullptr; only surviving nodes are checked.
                    if ( !node )
                    {
                        return true;
                    }
                    return RequireActiveBalance( node, dest_addr ) >= initial_balance + kMintAmount;
                } );
        },
        BridgeRaceE2ETest::kRaceNodeReadyTimeout,
        "All surviving nodes must converge to exactly-once mint despite one node's mid-mint destruction",
        nullptr );

    // Exactly-once guard on the surviving subset (do not query s_nodes[kKilledNodeIndex]
    // after .reset() — it no longer exists).
    for ( unsigned int i = 0u; i < BridgeRaceE2ETest::kNodeCount; ++i )
    {
        if ( i == kKilledNodeIndex )
        {
            continue;
        }
        const uint64_t final_balance = RequireActiveBalance( s_nodes[i], dest_addr );
        EXPECT_EQ( final_balance, initial_balance + kMintAmount )
            << "Surviving node " << i << " must mint the contested burn exactly once (no double-mint)";
    }

    spdlog::info( "bridge_race fault_kill: exactly-once mint verified across surviving {} nodes "
                  "after killing node {} mid-mint",
                  BridgeRaceE2ETest::kNodeCount - 1u,
                  kKilledNodeIndex );
}
