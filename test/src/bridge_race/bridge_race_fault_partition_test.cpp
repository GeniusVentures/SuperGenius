/**
 * @file       bridge_race_fault_partition_test.cpp
 * @brief      Phase 8 D-11: pubsub partition + heal fault test.
 * @date       2026-07-17
 * @author     Henrique A. Klein (hklein@gnus.ai)
 *
 * Splits the 11-node cluster into two pubsub/libp2p-layer groups by disconnecting all
 * cross-group peer links, configures RPC endpoints and seeds the burn WHILE partitioned
 * (both sub-groups can still independently reach Anvil RPC — the partition is at the
 * pubsub/CRDT layer only, never the network path to Anvil), allows time for each
 * sub-group to independently observe/attempt the mint, heals the partition via
 * AddPeers(), then asserts CRDT convergence reconciles the two sub-clusters into one
 * consistent exactly-once mint outcome across ALL 11 nodes.
 *
 * Open Question 2 (08-RESEARCH.md, carried into this plan): resolved by using
 * `libp2p::Host::getId()` directly on each REMOTE node's OWN Host instance — this test
 * runs all 11 nodes in-process, so obtaining a remote node's PeerId does not require
 * parsing GetLocalAddress()'s multiaddress string; s_nodes[b]->GetPubSub()->GetHost()->
 * getId() returns that node's own PeerId directly (confirmed via
 * 3rdparty/libp2p/include/libp2p/host/host.hpp:80's `virtual peer::PeerId getId() const`).
 * The multiaddress-parsing fallback described in 08-RESEARCH.md was not needed.
 */

#include "bridge_race_fixture.hpp"

#include <algorithm>
#include <array>
#include <thread>

TEST_F( BridgeRaceE2ETest, PartitionThenHealConvergesExactlyOnce )
{
    // Group A: Full node + 5 Light nodes. Group B: 5 Light nodes.
    constexpr std::array<unsigned int, 6> kGroupA{ 0u, 1u, 2u, 3u, 4u, 5u };
    constexpr std::array<unsigned int, 5> kGroupB{ 6u, 7u, 8u, 9u, 10u };
    constexpr unsigned int kDestinationIndex = 7u; // a Group B Light node.

    // CRDT-reconciliation timeout after heal — generous, to allow CRDT merge across the
    // two temporarily-diverged sub-clusters.
    static constexpr std::chrono::milliseconds kPartitionHealConvergenceTimeout{ 60000 };
    // Bounded interval each sub-group's watcher is allowed to poll independently while
    // still partitioned, before the heal is triggered.
    static constexpr std::chrono::milliseconds kPrePartitionHealWindow{ 12000 };

    const std::string dest_addr = DeriveLightDestination( kDestinationIndex );
    std::array<uint64_t, kNodeCount> initial_balances{};
    for ( unsigned int i = 0u; i < kNodeCount; ++i )
    {
        initial_balances[i] = s_nodes[i]->GetBalance( dest_addr );
    }

    spdlog::info( "bridge_race fault_partition: dest={} initial_balance={}",
                  dest_addr.substr( 0, 16 ),
                  initial_balances[0] );

    // Partition: disconnect every cross-group pair, from BOTH sides (Host::disconnect()
    // is local-only per-Host, so the link must be severed from each endpoint to fully
    // sever it).
    for ( unsigned int a : kGroupA )
    {
        for ( unsigned int b : kGroupB )
        {
            const auto peer_id_b = s_nodes[b]->GetPubSub()->GetHost()->getId();
            const auto peer_id_a = s_nodes[a]->GetPubSub()->GetHost()->getId();
            s_nodes[a]->GetPubSub()->GetHost()->disconnect( peer_id_b );
            s_nodes[b]->GetPubSub()->GetHost()->disconnect( peer_id_a );
        }
    }
    spdlog::info( "bridge_race fault_partition: partitioned group A ({} nodes) from group B ({} nodes)",
                  kGroupA.size(),
                  kGroupB.size() );

    // Build the RPC endpoint slots (real-Anvil 3-slot, as in the kill test — this test
    // is about pubsub/CRDT partitioning, not RPC disagreement).
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

    // Configure every watcher while still partitioned. Publishing first lets a watcher
    // consume the CRDT head with the fixture's initial 25/75 endpoint set, and that
    // rejected observation is intentionally not replayed after reconfiguration.
    for ( unsigned int i = 0u; i < BridgeRaceE2ETest::kNodeCount; ++i )
    {
        ASSERT_TRUE( s_nodes[i]->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId, anvil_eps ) )
            << "Node " << i << " rejected RPC endpoint configuration after READY";
    }
    spdlog::info( "bridge_race fault_partition: released ConfigureRpcEndpoint on all {} nodes while partitioned",
                  BridgeRaceE2ETest::kNodeCount );

    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";
    spdlog::info( "bridge_race fault_partition: seeded burn tx_hash={}", tx_hash );

    // Allow a bounded interval for each sub-group's watcher poll to fire independently
    // before healing the partition. This must actually elapse the window (not wait on an
    // already-true condition, which would return immediately and heal the partition
    // before either sub-group's watcher has a chance to poll) so the two sub-groups can
    // genuinely diverge before the heal + reconciliation is exercised.
    std::this_thread::sleep_for( kPrePartitionHealWindow );
    ASSERT_EQ( s_nodes[0]->GetState(), GeniusNode::NodeState::READY )
        << "Full node must remain READY during the pre-heal partition window";

    // Heal: reconnect every previously-disconnected cross-group pair via AddPeers().
    for ( unsigned int a : kGroupA )
    {
        for ( unsigned int b : kGroupB )
        {
            const std::string addr_b = s_nodes[b]->GetPubSub()->GetLocalAddress();
            const std::string addr_a = s_nodes[a]->GetPubSub()->GetLocalAddress();
            s_nodes[a]->GetPubSub()->AddPeers( { addr_b } );
            s_nodes[b]->GetPubSub()->AddPeers( { addr_a } );
        }
    }
    spdlog::info( "bridge_race fault_partition: healed partition between group A and group B" );

    // CRDT convergence: ALL 11 nodes must agree on exactly one mint for the destination
    // (no permanent fork, no double-mint post-heal).
    EXPECT_WAIT_FOR_CONDITION(
        [&]()
        {
            for ( unsigned int i = 0u; i < kNodeCount; ++i )
            {
                if ( s_nodes[i]->GetBalance( dest_addr ) < initial_balances[i] + kMintAmount )
                {
                    return false;
                }
            }
            return true;
        },
        kPartitionHealConvergenceTimeout,
        "All 11 nodes must converge to exactly-once mint after the partition heals",
        nullptr );

    // Exactly-once guard across all 11 nodes post-heal.
    for ( unsigned int i = 0u; i < BridgeRaceE2ETest::kNodeCount; ++i )
    {
        const uint64_t final_balance = s_nodes[i]->GetBalance( dest_addr );
        EXPECT_EQ( final_balance, initial_balances[i] + kMintAmount )
            << "Node " << i << " must mint the contested burn exactly once post-heal (no fork, no double-mint)";
    }

    spdlog::info( "bridge_race fault_partition: exactly-once mint verified across all {} nodes "
                  "after partition-then-heal",
                  BridgeRaceE2ETest::kNodeCount );
}
