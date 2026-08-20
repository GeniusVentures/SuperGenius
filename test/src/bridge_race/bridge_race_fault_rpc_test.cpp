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
 *  2. DIRECT disagrees (kWrongLogs) while both PUBLIC slots succeed and agree with each
 *     other — quorum met via PUBLIC-pair agreement (REQ-SLOT-03 dedup path), not the
 *     DIRECT shortcut.
 */

#include "bridge_race_fixture.hpp"

#include "src/mock/mock_rpc_config.hpp"
#include "src/mock/mock_rpc_transport.hpp"

namespace sgns
{
    class BridgeRaceFaultRpcTestAccess
    {
    public:
        static PublicChainInputValidator &Validator( TransactionManager &manager )
        {
            return manager.GetPublicChainInputValidator();
        }
    };
} // namespace sgns

namespace
{
    /// @brief Mock-default bridge contract address/topic0 (mirrors the private
    ///        kBridgeContractAddress/kBridgeEventTopic0 constants baked into
    ///        MockRpcTransport's default success/wrong-logs receipt builders in
    ///        mock_rpc_transport.cpp). The mock:// slots never touch the real Anvil
    ///        chain, so the WeightedRpcEndpoint's expected contract/topic0 must match
    ///        what MockRpcTransport actually returns, not the real Sepolia bridge
    ///        contract address.
    constexpr const char *kMockBridgeContractAddress = "0x1234567890123456789012345678901234567890";
    constexpr const char *kMockBridgeEventTopic0 =
        "0x1234567890123456789012345678901234567890123456789012345678901234";

    /// @brief Install the 3-slot divergent TransportFactory on one node's
    ///        PublicChainInputValidator and configure the matching WeightedRpcEndpoint
    ///        vector (URLs must match BuildDivergentSlotConfigs()'s literal URLs).
    void ConfigureDivergentQuorum( const std::shared_ptr<GeniusNode>   &node,
                                   sgns::test::MockBehavior            direct_behavior,
                                   sgns::test::MockBehavior            public1_behavior,
                                   sgns::test::MockBehavior            public2_behavior )
    {
        const auto configs =
            sgns::test::BuildDivergentSlotConfigs( direct_behavior, public1_behavior, public2_behavior );

        ASSERT_FALSE( RequireActiveAddress( node ).empty() )
            << "node is not active-ready before divergent RPC endpoint configuration";
        auto tx_mgr_result = node->GetTransactionManager();
        ASSERT_TRUE( tx_mgr_result.has_value() ) << "node transaction manager not ready";
        auto manager = tx_mgr_result.value();
        ASSERT_EQ( manager->GetLifecycle(), sgns::TransactionManager::ManagerLifecycle::ACTIVE )
            << "node transaction manager retired before fault injection";
        auto &validator = sgns::BridgeRaceFaultRpcTestAccess::Validator( *manager );

        // Factory-dispatch lambda keyed on exact URL match (08-PATTERNS.md Pattern 3).
        validator.SetTransportFactory(
            [configs]( const std::string &url,
                       std::chrono::seconds /*timeout*/ ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
            {
                for ( const auto &config : configs )
                {
                    if ( config.url == url )
                    {
                        return std::make_unique<sgns::test::MockRpcTransport>( config );
                    }
                }
                return std::make_unique<sgns::test::MockRpcTransport>( configs.back() );
            } );

        // Literal URLs match BuildDivergentSlotConfigs()'s 3 distinct "mock://" configs
        // (asserted below) — used directly rather than via configs[i].url so the
        // 3-way divergence is visible as literal strings at this call site.
        sgns::WeightedRpcEndpoint ep_direct;
        ep_direct.url                     = "mock://direct";
        ep_direct.consensus_weight        = 100;
        ep_direct.bridge_contract_address = kMockBridgeContractAddress;
        ep_direct.accepted_topic0_hashes  = { kMockBridgeEventTopic0 };
        ASSERT_EQ( ep_direct.url, configs[0].url );

        sgns::WeightedRpcEndpoint ep_public1 = ep_direct;
        ep_public1.url              = "mock://public1";
        ep_public1.consensus_weight = 0;
        ASSERT_EQ( ep_public1.url, configs[1].url );

        sgns::WeightedRpcEndpoint ep_public2 = ep_direct;
        ep_public2.url              = "mock://public2";
        ep_public2.consensus_weight = 0;
        ASSERT_EQ( ep_public2.url, configs[2].url );

        ASSERT_TRUE( node->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId,
                                                 { ep_direct, ep_public1, ep_public2 } ) )
            << "READY node rejected divergent RPC endpoint configuration";
    }
} // namespace

TEST_F( BridgeRaceE2ETest, RpcDisagreementStillReachesCorrectQuorum )
{
    const std::string dest_addr     = DeriveLightDestination( 2u );
    const uint64_t    initial_balance = RequireActiveBalance( s_nodes[0], dest_addr );

    spdlog::info( "bridge_race fault_rpc (DIRECT-succeeds-alone): dest={} initial_balance={}",
                  dest_addr.substr( 0, 16 ),
                  initial_balance );

    // Seed the burn against the REAL Anvil instance BEFORE configuring the mock quorum
    // (D-03 ordering — still needed for genuine on-chain log discovery by the watcher).
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";

    // Case 1: DIRECT succeeds, one PUBLIC returns wrong logs, one PUBLIC times out.
    // DIRECT alone (weight=100) is sufficient for quorum under this disagreement.
    for ( const auto &node : s_nodes )
    {
        ConfigureDivergentQuorum( node,
                                  sgns::test::MockBehavior::kSuccess,
                                  sgns::test::MockBehavior::kWrongLogs,
                                  sgns::test::MockBehavior::kTimeout );
        ASSERT_FALSE( ::testing::Test::HasFatalFailure() );
    }

    EXPECT_WAIT_FOR_CONDITION(
        [&]() { return RequireActiveBalance( s_nodes[0], dest_addr ) >= initial_balance + kMintAmount; },
        BridgeRaceE2ETest::kRaceNodeReadyTimeout,
        "node 0 must mint via DIRECT-succeeds-alone quorum despite PUBLIC-slot disagreement",
        nullptr );

    EXPECT_EQ( RequireActiveBalance( s_nodes[0], dest_addr ), initial_balance + kMintAmount )
        << "Mint must be exactly-once even under RPC-slot disagreement";
}

TEST_F( BridgeRaceE2ETest, RpcDisagreementPublicPairQuorumStillCorrect )
{
    const std::string dest_addr     = DeriveLightDestination( 3u );
    const uint64_t    initial_balance = RequireActiveBalance( s_nodes[0], dest_addr );

    spdlog::info( "bridge_race fault_rpc (PUBLIC-pair-agrees-alone): dest={} initial_balance={}",
                  dest_addr.substr( 0, 16 ),
                  initial_balance );

    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";

    // Case 2: DIRECT disagrees (kWrongLogs), both PUBLIC slots succeed and agree with
    // each other — quorum must be reached via PUBLIC-pair agreement, exercising the
    // WeightedRpcEndpoint dedup-based quorum path rather than the DIRECT weight-100
    // shortcut.
    for ( const auto &node : s_nodes )
    {
        ConfigureDivergentQuorum( node,
                                  sgns::test::MockBehavior::kWrongLogs,
                                  sgns::test::MockBehavior::kSuccess,
                                  sgns::test::MockBehavior::kSuccess );
        ASSERT_FALSE( ::testing::Test::HasFatalFailure() );
    }

    EXPECT_WAIT_FOR_CONDITION(
        [&]() { return RequireActiveBalance( s_nodes[0], dest_addr ) >= initial_balance + kMintAmount; },
        BridgeRaceE2ETest::kRaceNodeReadyTimeout,
        "node 0 must mint via PUBLIC-pair-agrees-alone quorum despite DIRECT-slot disagreement",
        nullptr );

    EXPECT_EQ( RequireActiveBalance( s_nodes[0], dest_addr ), initial_balance + kMintAmount )
        << "Mint must be exactly-once even under RPC-slot disagreement";
}
