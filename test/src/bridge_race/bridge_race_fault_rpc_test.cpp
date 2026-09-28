/**
 * @file       bridge_race_fault_rpc_test.cpp
 * @brief      Phase 8 D-08/D-09 rewritten for the cumulative slot-quorum model.
 * @date       2026-07-17
 * @author     Henrique A Klein (hklein@gnus.ai)
 *
 * Quorum semantics under test (ValidatorRegistry::EvaluateSlotQuorum, D-02/D-03/D-06):
 * a bridge mint's certificate only confirms when the cumulative slot sum STRICTLY
 * exceeds ceil(3/4 * total voting reputation), where
 *   - slot 0 (DIRECT) contributes at most 1/2 of a voter's weight,
 *   - each PUBLIC slot group (>= 2 voters reporting the same slot hash) 1/4,
 *   - and a vote's slot hashes are sha256(endpoint URL) for the endpoints whose
 *     receipt verified THIS claim (weight >= 50 classifies as DIRECT).
 * Cross-SLOT disagreement is therefore tolerated by design (each slot group is
 * independent), while a missing slot permanently caps the reachable sum: with
 * DIRECT absent the ceiling is 1/4+1/4 = 1/2 < 3/4, and evidence-level weight
 * (GatherVerificationEvidence's >= 75 of endpoint consensus_weight) can NEVER
 * substitute for the tally-level slot math.
 *
 * The pre-slot-model version of this suite asserted the opposite ("DIRECT
 * weight-100 shortcut" / "PUBLIC pair 40+40 >= 75 reaches quorum alone") — both
 * scenarios are structurally unreachable under the slot tally, which only
 * passed historically because votes carried no slot hashes and the tally fell
 * back to the single-pool model (before the #364 evidence binding).
 *
 * Two scenarios are exercised:
 *  1. POSITIVE: three DISTINCT slot identities (real-Anvil DIRECT + two mock
 *     PUBLIC URLs), every slot confirming — the slot groups disagree with each
 *     other by construction, yet 1/2+1/4+1/4 = 100% > 3/4 reaches quorum and
 *     the contested burn mints exactly once.
 *  2. NEGATIVE: the DIRECT slot times out while both PUBLIC slots confirm.
 *     Evidence stays valid (40+40=80 >= 75) and every node votes approve, but
 *     the reachable slot sum is capped at 1/2 — the mint MUST fail closed: no
 *     balance ever appears, and the node stays READY (liveness, not crash).
 */

#include "bridge_race_fixture.hpp"

#include <chrono>
#include <thread>

#include "src/mock/mock_rpc_config.hpp"
#include "src/mock/mock_rpc_transport.hpp"

namespace
{
    /// @brief Mock-default bridge contract address/topic0 (mirrors the private
    ///        kBridgeContractAddress/kBridgeEventTopic0 constants baked into
    ///        MockRpcTransport's default success/wrong-logs receipt builders in
    ///        mock_rpc_transport.cpp). No quorum slot's verification traffic reaches the
    ///        real Anvil chain, so the WeightedRpcEndpoint's expected contract/topic0 must
    ///        match what MockRpcTransport actually returns, not the real Sepolia bridge
    ///        contract address.
    constexpr const char *kMockBridgeContractAddress = "0x1234567890123456789012345678901234567890";
    constexpr const char *kMockBridgeEventTopic0 =
        "0x1234567890123456789012345678901234567890123456789012345678901234";

    /// @brief Per-PUBLIC-slot consensus weight.
    ///
    /// Must sit below PublicChainInputValidator's kDirectApiWeightThreshold (50) so both
    /// slots stay PUBLIC (populating slot hashes 1/2, never slot 0), yet sum to
    /// 40+40=80 >= kRequiredConsensusWeight (75) — that is the POINT of the
    /// negative case below: evidence gathering succeeds and every node votes
    /// approve, while the slot tally remains structurally short of quorum.
    constexpr uint8_t kPublicSlotWeight = 40;

    /// @brief How long the negative case observes a fail-closed mint.
    ///
    /// The watcher polls every 15s and consensus rounds cycle in ~1s, so 30s
    /// spans two full discovery/verification cycles. The quorum ceiling in that
    /// scenario is structural (1/2 < 3/4), so the window only guards against a
    /// mint arriving LATE through some path that bypasses the slot tally.
    constexpr auto kFailCloseObservationWindow = std::chrono::seconds( 30 );

    /// @brief Install the 3-slot divergent TransportFactory on one node's
    ///        PublicChainInputValidator and configure the matching WeightedRpcEndpoint
    ///        vector (URLs must match BuildDivergentSlotConfigs()'s literal URLs).
    ///
    /// The DIRECT slot keeps the real Anvil URL instead of "mock://direct" because
    /// ConfigureRpcEndpoint wholesale-replaces the chain's endpoint list, and the
    /// node-owned BridgeCatchupWatcher resolves its discovery URL from that same list
    /// (GeniusNode's rpc_resolver -> PublicChainInputValidator::GetFirstRpcUrl ->
    /// front().url) using a real RpcHttpTransport, not this validator-only factory. An
    /// all-"mock://" list left every poll dying at "failed to query block number", so
    /// the seeded burn was never discovered and no mint ever started. The slot behavior
    /// is unaffected: the factory still serves this URL from MockRpcTransport, so
    /// verification never touches the real chain, and slot classification is weight-based
    /// (consensus_weight >= 50 == DIRECT), not URL-based.
    void ConfigureDivergentQuorum( const std::shared_ptr<GeniusNode>   &node,
                                   const std::string                   &direct_url,
                                   sgns::test::MockBehavior            direct_behavior,
                                   sgns::test::MockBehavior            public1_behavior,
                                   sgns::test::MockBehavior            public2_behavior )
    {
        const auto configs = sgns::test::BuildDivergentSlotConfigs( direct_behavior,
                                                                    public1_behavior,
                                                                    public2_behavior,
                                                                    direct_url );

        auto tx_mgr_result = node->GetTransactionManager();
        ASSERT_TRUE( tx_mgr_result.has_value() ) << "node transaction manager not ready";
        auto &validator = tx_mgr_result.value()->GetPublicChainInputValidator();

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

        sgns::WeightedRpcEndpoint ep_direct;
        ep_direct.url                     = configs[0].url;
        ep_direct.consensus_weight        = 100;
        ep_direct.bridge_contract_address = kMockBridgeContractAddress;
        ep_direct.accepted_topic0_hashes  = { kMockBridgeEventTopic0 };

        // The PUBLIC URLs are spelled as literals rather than read from configs[i].url so
        // the 3-way divergence is visible at this call site (asserted against the builder
        // below). The DIRECT slot's URL is the caller's, for the reason documented above.

        sgns::WeightedRpcEndpoint ep_public1 = ep_direct;
        ep_public1.url              = "mock://public1";
        ep_public1.consensus_weight = kPublicSlotWeight;
        ASSERT_EQ( ep_public1.url, configs[1].url );

        sgns::WeightedRpcEndpoint ep_public2 = ep_direct;
        ep_public2.url              = "mock://public2";
        ep_public2.consensus_weight = kPublicSlotWeight;
        ASSERT_EQ( ep_public2.url, configs[2].url );

        ASSERT_TRUE( node->ConfigureRpcEndpoint( sgns::test::anvil::kSepoliaChainId,
                                                 { ep_direct, ep_public1, ep_public2 } ) )
            << "READY node rejected divergent RPC endpoint configuration";
    }
} // namespace

TEST_F( BridgeRaceE2ETest, AllSlotsDistinctStillReachesQuorum )
{
    const std::string dest_addr       = DeriveLightDestination( 2u );
    const uint64_t    initial_balance = s_nodes[0]->GetBalance( dest_addr );

    spdlog::info( "bridge_race fault_rpc (all-slots-distinct): dest={} initial_balance={}",
                  dest_addr.substr( 0, 16 ),
                  initial_balance );

    // Seed the burn against the REAL Anvil instance BEFORE configuring the mock quorum
    // (D-03 ordering — still needed for genuine on-chain log discovery by the watcher).
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";

    // All three slots confirm, each under a DISTINCT slot identity: slot 0 hashes the
    // real Anvil URL (mock-served), slots 1/2 the mock://publicN URLs. The slot groups
    // disagree with each other by construction — the cumulative model does not care,
    // because each group is tallied independently: 1/2 + 1/4 + 1/4 = 100% > 3/4.
    for ( const auto &node : s_nodes )
    {
        ConfigureDivergentQuorum( node,
                                  s_anvil.RpcUrl(),
                                  sgns::test::MockBehavior::kSuccess,
                                  sgns::test::MockBehavior::kSuccess,
                                  sgns::test::MockBehavior::kSuccess );
        ASSERT_FALSE( ::testing::Test::HasFatalFailure() );
    }

    EXPECT_WAIT_FOR_CONDITION(
        [&]() { return s_nodes[0]->GetBalance( dest_addr ) >= initial_balance + kMintAmount; },
        BridgeRaceE2ETest::kRaceNodeReadyTimeout,
        "node 0 must mint via 1/2+1/4+1/4 slot sum despite the three slot identities disagreeing",
        nullptr );

    EXPECT_EQ( s_nodes[0]->GetBalance( dest_addr ), initial_balance + kMintAmount )
        << "Mint must be exactly-once across distinct slot identities";
}

TEST_F( BridgeRaceE2ETest, DirectSlotFailureFailClosesMint )
{
    const std::string dest_addr       = DeriveLightDestination( 4u );
    const uint64_t    initial_balance = s_nodes[0]->GetBalance( dest_addr );

    spdlog::info( "bridge_race fault_rpc (DIRECT-slot-failure fail-close): dest={} initial_balance={}",
                  dest_addr.substr( 0, 16 ),
                  initial_balance );

    // Same real-burn seeding as the positive case: discovery runs on a REAL transport
    // against Anvil and is expected to succeed — the fault injected below must gate the
    // mint at consensus, not at discovery.
    const std::string tx_hash = sgns::test::anvil::SendBridgeOutBurn(
        s_anvil.RpcUrl(), static_cast<uint64_t>( kMintAmount ), dest_addr );
    ASSERT_FALSE( tx_hash.empty() ) << "Failed to seed contested burn";

    // The DIRECT slot times out; both PUBLIC slots confirm and agree (their receipts
    // are identical — distinct slot hashes, same verified claim). Evidence gathering
    // still succeeds (40+40=80 >= 75) so every node votes approve with slots 1/2
    // populated, but slot 0 is never filled: the reachable slot sum is capped at
    // 1/4+1/4 = 1/2 of voting reputation, strictly below the 3/4 quorum threshold.
    // No number of approving votes can close that gap — the mint must fail closed.
    for ( const auto &node : s_nodes )
    {
        ConfigureDivergentQuorum( node,
                                  s_anvil.RpcUrl(),
                                  sgns::test::MockBehavior::kTimeout,
                                  sgns::test::MockBehavior::kSuccess,
                                  sgns::test::MockBehavior::kSuccess );
        ASSERT_FALSE( ::testing::Test::HasFatalFailure() );
    }

    // Actually elapse the observation window (a wait-for-false would return instantly):
    // the balance must not move through two full watcher/consensus cycles.
    std::this_thread::sleep_for( kFailCloseObservationWindow );

    EXPECT_EQ( s_nodes[0]->GetBalance( dest_addr ), initial_balance )
        << "Mint without a DIRECT slot must fail closed (1/2 slot ceiling < 3/4 quorum)";
    EXPECT_EQ( s_nodes[0]->GetState(), GeniusNode::NodeState::READY )
        << "Fail-close is a consensus gate, not a node failure — node 0 must stay READY";
}
