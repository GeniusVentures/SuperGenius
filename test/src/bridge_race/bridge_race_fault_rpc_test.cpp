/**
 * @file       bridge_race_fault_rpc_test.cpp
 * @brief      Phase 8 D-08/D-09: RPC-endpoint disagreement still reaches correct quorum.
 * @date       2026-07-17
 * @author     Henrique A. Klein (hklein@gnus.ai)
 *
 * Extends the Phase 5 Mock RPC Transport (via BuildDivergentSlotConfigs(), additive-only)
 * to prove the existing >75% weighted RPC quorum still reaches the correct mint decision
 * when the 3 configured quorum slots (1 DIRECT + 2 PUBLIC) genuinely disagree — one
 * returns wrong logs, one times out, one succeeds — rather than all 3 silently resolving
 * to the same real Anvil URL as every other test in this suite does.
 *
 * Two disagreement configurations are exercised:
 *  1. DIRECT succeeds alone (weight=100) while both PUBLIC slots disagree with each other
 *     and with DIRECT — quorum met via the DIRECT weight-100 shortcut.
 *  2. DIRECT disagrees (kWrongLogs) while both PUBLIC slots succeed and contribute
 *     weights 40 + 35 — quorum met by their combined 75 weight, not the DIRECT shortcut.
 */

#include "bridge_race_fixture.hpp"

#include "src/mock/mock_rpc_config.hpp"
#include "src/mock/mock_rpc_transport.hpp"

#include <eth/rpc_http_transport.hpp>

namespace
{
    /// @brief Install the 3-slot divergent TransportFactory on one node's
    ///        PublicChainInputValidator and configure the matching WeightedRpcEndpoint
    ///        vector. The DIRECT endpoint keeps the real Anvil URL so the separate
    ///        catch-up watcher can discover burns; only receipt validation is faulted.
    void ConfigureDivergentQuorum( const std::shared_ptr<GeniusNode>   &node,
                                   sgns::test::MockBehavior            direct_behavior,
                                   sgns::test::MockBehavior            public1_behavior,
                                   sgns::test::MockBehavior            public2_behavior,
                                   const std::string                  &success_rpc_url )
    {
        const auto configs =
            sgns::test::BuildDivergentSlotConfigs( direct_behavior, public1_behavior, public2_behavior );

        auto tx_mgr_result = node->GetTransactionManager();
        ASSERT_TRUE( tx_mgr_result.has_value() ) << "node transaction manager not ready";
        auto &validator = tx_mgr_result.value()->GetPublicChainInputValidator();

        // Successful slots proxy the real Anvil receipt so strengthened semantic
        // validation sees the actual event data and receipt-local ordinal. Faulted
        // slots retain the deterministic mock behaviors.
        validator.SetTransportFactory(
            [configs, success_rpc_url]( const std::string &url,
                                        std::chrono::seconds timeout ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
            {
                const auto make_transport = [&]( const sgns::test::MockEndpointConfig &config )
                    -> std::unique_ptr<eth::rpc::JsonRpcTransport>
                {
                    if ( config.behavior == sgns::test::MockBehavior::kSuccess )
                    {
                        eth::rpc::RpcHttpTransportOptions options;
                        options.timeout = timeout;
                        return std::make_unique<eth::rpc::RpcHttpTransport>( success_rpc_url, options );
                    }
                    return std::make_unique<sgns::test::MockRpcTransport>( config );
                };

                // The DIRECT slot intentionally uses the real URL in the endpoint
                // configuration so BridgeCatchupWatcher (which owns a separate
                // transport) can query block numbers and logs. Its validator behavior
                // still comes from configs[0].
                if ( url == success_rpc_url )
                {
                    return make_transport( configs[0] );
                }
                for ( std::size_t i = 1; i < configs.size(); ++i )
                {
                    if ( configs[i].url == url ) return make_transport( configs[i] );
                }
                return std::make_unique<sgns::test::MockRpcTransport>( configs.back() );
            } );

        // PUBLIC URLs match the two mock slots. DIRECT retains the real Anvil URL
        // for the independent catch-up watcher and is mapped to configs[0] above.
        sgns::WeightedRpcEndpoint ep_direct;
        ep_direct.url                     = success_rpc_url;
        ep_direct.consensus_weight        = 100;
        ep_direct.bridge_contract_address = sgns::test::anvil::kSepoliaBridgeContractLower;
        ep_direct.accepted_topic0_hashes  = { sgns::test::anvil::BridgeEventTopic0() };
        ASSERT_EQ( configs[0].url, "mock://direct" );

        sgns::WeightedRpcEndpoint ep_public1 = ep_direct;
        ep_public1.url              = "mock://public1";
        ep_public1.consensus_weight = 40;
        ASSERT_EQ( ep_public1.url, configs[1].url );

        sgns::WeightedRpcEndpoint ep_public2 = ep_direct;
        ep_public2.url              = "mock://public2";
        ep_public2.consensus_weight = 35;
        ASSERT_EQ( ep_public2.url, configs[2].url );

        ASSERT_TRUE( node->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId,
                                                 { ep_direct, ep_public1, ep_public2 } ) )
            << "READY node rejected divergent RPC endpoint configuration";
    }
} // namespace

TEST_F( BridgeRaceE2ETest, RpcDisagreementStillReachesCorrectQuorum )
{
    const std::string dest_addr     = DeriveLightDestination( 2u );
    const uint64_t    initial_balance = s_nodes[0]->GetBalance( dest_addr );

    spdlog::info( "bridge_race fault_rpc (DIRECT-succeeds-alone): dest={} initial_balance={}",
                  dest_addr.substr( 0, 16 ),
                  initial_balance );

    // Case 1: DIRECT succeeds, one PUBLIC returns wrong logs, one PUBLIC times out.
    // DIRECT alone (weight=100) is sufficient for quorum under this disagreement.
    for ( const auto &node : s_nodes )
    {
        ConfigureDivergentQuorum( node,
                                  sgns::test::MockBehavior::kSuccess,
                                  sgns::test::MockBehavior::kWrongLogs,
                                  sgns::test::MockBehavior::kTimeout,
                                  s_anvil.RpcUrl() );
        ASSERT_FALSE( ::testing::Test::HasFatalFailure() );
    }

    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";

    EXPECT_WAIT_FOR_CONDITION(
        [&]() { return s_nodes[0]->GetBalance( dest_addr ) >= initial_balance + kMintAmount; },
        BridgeRaceE2ETest::kRaceNodeReadyTimeout,
        "node 0 must mint via DIRECT-succeeds-alone quorum despite PUBLIC-slot disagreement",
        nullptr );

    EXPECT_EQ( s_nodes[0]->GetBalance( dest_addr ), initial_balance + kMintAmount )
        << "Mint must be exactly-once even under RPC-slot disagreement";
}

TEST_F( BridgeRaceE2ETest, RpcDisagreementPublicPairQuorumStillCorrect )
{
    const std::string dest_addr     = DeriveLightDestination( 3u );
    const uint64_t    initial_balance = s_nodes[0]->GetBalance( dest_addr );

    spdlog::info( "bridge_race fault_rpc (PUBLIC-pair-agrees-alone): dest={} initial_balance={}",
                  dest_addr.substr( 0, 16 ),
                  initial_balance );

    // Case 2: DIRECT disagrees (kWrongLogs), both PUBLIC slots succeed and contribute
    // weights 40 + 35. Their combined 75 weight reaches the receipt-verification
    // threshold without the DIRECT weight-100 shortcut.
    for ( const auto &node : s_nodes )
    {
        ConfigureDivergentQuorum( node,
                                  sgns::test::MockBehavior::kWrongLogs,
                                  sgns::test::MockBehavior::kSuccess,
                                  sgns::test::MockBehavior::kSuccess,
                                  s_anvil.RpcUrl() );
        ASSERT_FALSE( ::testing::Test::HasFatalFailure() );
    }

    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";

    EXPECT_WAIT_FOR_CONDITION(
        [&]() { return s_nodes[0]->GetBalance( dest_addr ) >= initial_balance + kMintAmount; },
        BridgeRaceE2ETest::kRaceNodeReadyTimeout,
        "node 0 must mint via PUBLIC-pair-agrees-alone quorum despite DIRECT-slot disagreement",
        nullptr );

    EXPECT_EQ( s_nodes[0]->GetBalance( dest_addr ), initial_balance + kMintAmount )
        << "Mint must be exactly-once even under RPC-slot disagreement";
}
