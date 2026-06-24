/**
 * @file       bridge_e2e_chainlist_test.cpp
 * @brief      E2E test: ChainRpcEndpointProvider wired with real chainlist URLs
 *             + mock RPC transport proves the full consensus pipeline works
 *             without live networks.  Exercises the Phase 05.1 observer-driven
 *             Initialize(path, validator) API.
 * @date       2026-06-17
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "account/ChainRpcEndpointProvider.hpp"
#include "account/BridgeEventTypes.hpp"
#include "account/MintTransaction.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "eth/abi_decoder.hpp"
#include "eth/chainlist_provider.hpp"
#include "base/parse_utility.hpp"
#include "src/mock/mock_rpc_transport.hpp"

namespace fs = std::filesystem;

using namespace sgns;

// ─── Test Data ──────────────────────────────────────────────────────────────

/// @brief Real Sepolia RPC URLs from chainlist.org (3 endpoints × 25% = 75% consensus threshold).
static constexpr const char *kSepoliaRpcUrls[] = {
    "https://ethereum-sepolia-rpc.publicnode.com",
    "https://rpc.sepolia.org",
    "https://sepolia.drpc.org",
};

/// @brief Sepolia GNUS bridge contract address (checksummed).
static constexpr const char *kSepoliaContract = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";

/// @brief Chainlist.org-format JSON (array) containing real Sepolia endpoints.
static const std::string kChainlistJson = R"([
    {
        "name": "Ethereum Sepolia",
        "chain": "ETH",
        "chainId": 11155111,
        "rpc": [
            "https://ethereum-sepolia-rpc.publicnode.com",
            "https://rpc.sepolia.org",
            "https://sepolia.drpc.org"
        ]
    }
])";

/// @brief Canned chainlist fetch (no network) — returns the Sepolia dataset.
static std::optional<std::string> CannedChainlistFetch()
{
    return kChainlistJson;
}

/// @brief Compute the BridgeSourceBurned event topic0 to match provider-generated topic0.
static std::string ComputeBridgeTopic0()
{
    auto hash = eth::abi::event_signature_hash( std::string( kBridgeSourceBurnedSig ) );
    return rlp::base::parse::hex_bytes( hash.data(), hash.size() );
}

/// @brief Write a temp bridge_chains_config.json (Phase 05.1 object-keyed format).
static fs::path WriteTempBridgeConfig()
{
    auto              path = fs::temp_directory_path() / "e2e_bridge_chains_config.json";
    std::ofstream     out( path, std::ios::binary | std::ios::trunc );
    const std::string json = R"({
        "ethereum-sepolia": {
            "chain_id": 11155111,
            "bridge_contract_address": ")" +
                             std::string( kSepoliaContract ) + R"("
        }
    })";
    out << json;
    out.close();
    return path;
}

/// @brief Build a valid eth_getTransactionReceipt JSON-RPC response with correct
///        bridge contract + topic0 so VerifyPublicChainSmartContract passes.
static std::string BuildValidReceiptJson( const std::string &tx_hash,
                                          const std::string &contract_addr,
                                          const std::string &topic0 )
{
    boost::json::object root;
    root["jsonrpc"] = "2.0";
    root["id"]      = 1;

    boost::json::object result;
    result["status"]          = "0x1";
    result["blockNumber"]     = "0x100000";
    result["blockHash"]       = "0x" + std::string( 64, '1' );
    result["transactionHash"] = tx_hash;

    boost::json::object log_entry;
    log_entry["address"]         = contract_addr;
    log_entry["topics"]          = boost::json::array{ topic0 };
    log_entry["data"]            = "0x";
    log_entry["blockNumber"]     = "0x100000";
    log_entry["blockHash"]       = "0x" + std::string( 64, '1' );
    log_entry["transactionHash"] = tx_hash;
    log_entry["logIndex"]        = "0x0";

    result["logs"] = boost::json::array{ std::move( log_entry ) };
    root["result"] = std::move( result );
    return boost::json::serialize( root );
}

// ─── Minimal Mock Transport ─────────────────────────────────────────────────

/// @brief Mock RPC transport that returns a fixed receipt for ANY request.
class FixedReceiptTransport final : public eth::rpc::JsonRpcTransport
{
public:
    explicit FixedReceiptTransport( std::string receipt_json ) : receipt_( std::move( receipt_json ) )
    {
    }

    std::optional<std::string> call( const boost::json::object & /*request*/ ) override
    {
        return receipt_;
    }

private:
    std::string receipt_;
};

// ─── Test Accessor ──────────────────────────────────────────────────────────

/// @brief Friend accessor for the private VerifyPublicChainSmartContract and
///        the wired rpc_endpoints_ (mirrors BridgeRelayerTestAccess).
class PublicChainInputValidatorTestAccess
{
public:
    static bool Verify( const PublicChainInputValidator          &validator,
                        const std::shared_ptr<GeniusTransaction> &tx,
                        const std::string                        &source_reference )
    {
        return validator.VerifyPublicChainSmartContract( tx, source_reference );
    }

    /// @brief Read the accepted topic0 hashes wired onto the first endpoint for
    ///        a chain id (the provider stamps every endpoint identically).
    static std::vector<std::string> AcceptedTopic0Hashes( const PublicChainInputValidator &validator,
                                                          const std::string               &chain_id )
    {
        auto it = validator.rpc_endpoints_.find( chain_id );
        if ( it == validator.rpc_endpoints_.end() || it->second.empty() )
        {
            return {};
        }
        return it->second.front().accepted_topic0_hashes;
    }
};

// ─── Tests ──────────────────────────────────────────────────────────────────

TEST( BridgeE2EChainlistTest, ChainlistEndpointsWiredWithMockTransport )
{
    // 1. Parse chainlist data to get real URLs
    auto parse_result = eth::rpc::load_chainlist_from_json_text( kChainlistJson );
    ASSERT_TRUE( parse_result.has_value() ) << "Chainlist JSON must parse";
    ASSERT_FALSE( parse_result.value().empty() ) << "Chainlist must have at least 1 endpoint";

    // 2. Write temp bridge_chains_config.json
    auto config_path = WriteTempBridgeConfig();
    ASSERT_TRUE( fs::exists( config_path ) );

    // 3. Create validator + provider
    PublicChainInputValidator validator;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistFetch );

    // 4. Inject mock transport factory — verify it was accepted
    validator.SetTransportFactory(
        [&]( const std::string & /*url*/,
             std::chrono::seconds /*timeout*/ ) -> std::unique_ptr<eth::rpc::JsonRpcTransport>
        {
            const std::string topic0  = ComputeBridgeTopic0();
            const std::string receipt = BuildValidReceiptJson( "0x" + std::string( 64, 'a' ),
                                                               kSepoliaContract,
                                                               topic0 );
            return std::make_unique<FixedReceiptTransport>( receipt );
        } );

    // 5. Initialize provider — wires endpoints with bridge metadata (Phase 05.1 D-01/D-02)
    bool result = provider.Initialize( config_path, validator );
    ASSERT_TRUE( result ) << "Initialize should succeed with valid config";

    // 6. Add URLs from chainlist data with consensus weights
    const std::string                topic0 = ComputeBridgeTopic0();
    std::vector<WeightedRpcEndpoint> endpoints;
    for ( const auto &url : kSepoliaRpcUrls )
    {
        WeightedRpcEndpoint ep;
        ep.url                     = url;
        ep.consensus_weight        = 25;
        ep.bridge_contract_address = kSepoliaContract;
        ep.accepted_topic0_hashes  = { topic0 };
        endpoints.push_back( ep );
    }
    validator.SetRpcEndpoints( "11155111", std::move( endpoints ) );

    // 7. Verify endpoints are available and contain real URLs
    auto first_url = validator.GetFirstRpcUrl( "11155111" );
    ASSERT_TRUE( first_url.has_value() ) << "Validator must have RPC URL after wiring";
    EXPECT_FALSE( first_url->empty() );
    EXPECT_EQ( *first_url, kSepoliaRpcUrls[0] ) << "First URL should match the chainlist.org URL for Sepolia";

    // 8. Prove mock transport returns valid receipt JSON that would satisfy
    //    VerifyPublicChainSmartContract (which requires status=0x1, matching
    //    bridge_contract_address + event_topic0 in the receipt log).
    const std::string     valid_json = BuildValidReceiptJson( "0x" + std::string( 64, 'a' ), kSepoliaContract, topic0 );
    FixedReceiptTransport direct_transport( valid_json );

    boost::json::object dummy_request;
    dummy_request["method"] = "eth_getTransactionReceipt";
    dummy_request["params"] = boost::json::array{ "0x" + std::string( 64, 'a' ) };
    dummy_request["id"]     = 1;

    auto response = direct_transport.call( dummy_request );
    ASSERT_TRUE( response.has_value() );
    EXPECT_TRUE( response->find( "\"0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70\"" ) != std::string::npos )
        << "Mock receipt must contain the real Sepolia bridge contract address";
    EXPECT_TRUE( response->find( "\"0xde0dff20aee114e5ac35a9f7a916ab799270e86ae622ec6de8ab330eaacafc81\"" ) !=
                 std::string::npos )
        << "Mock receipt must contain the computed BridgeSourceBurned topic0";
}

// Regression for the v2-bridge topic0 mismatch: the relayer and catch-up scan
// mint from BridgeOutInitiated (v2), but every endpoint was configured with
// only the BridgeSourceBurned (v1) topic0, so witness validation rejected any
// mint created from a v2 burn. After the fix, the provider wires BOTH topic0
// hashes and VerifyPublicChainSmartContract accepts a v2 receipt.
TEST( BridgeE2EChainlistTest, ProviderWiresBothTopic0AndValidatorAcceptsV2Receipt )
{
    auto config_path = WriteTempBridgeConfig();
    ASSERT_TRUE( fs::exists( config_path ) );

    PublicChainInputValidator validator;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistFetch );

    // 1. Provider must wire BOTH v1 and v2 topic0 onto each endpoint.
    ASSERT_TRUE( provider.Initialize( config_path, validator ) );
    const auto hashes = PublicChainInputValidatorTestAccess::AcceptedTopic0Hashes( validator, "11155111" );
    ASSERT_EQ( hashes.size(), 2u );

    const auto v1_hash = eth::abi::event_signature_hash( std::string( kBridgeSourceBurnedSig ) );
    const auto v2_hash = eth::abi::event_signature_hash( std::string( kBridgeOutInitiatedSig ) );
    const std::string v1_topic0 = rlp::base::parse::hex_bytes( v1_hash.data(), v1_hash.size() );
    const std::string v2_topic0 = rlp::base::parse::hex_bytes( v2_hash.data(), v2_hash.size() );
    EXPECT_NE( std::find( hashes.begin(), hashes.end(), v1_topic0 ), hashes.end() )
        << "Provider must wire the v1 BridgeSourceBurned topic0";
    EXPECT_NE( std::find( hashes.begin(), hashes.end(), v2_topic0 ), hashes.end() )
        << "Provider must wire the v2 BridgeOutInitiated topic0";

    // 2. Validator must accept a receipt whose log carries the v2 topic0.
    //    Reuse the provider-wired dual hashes across 3 endpoints (75% quorum).
    std::vector<WeightedRpcEndpoint> endpoints;
    for ( const auto &url : kSepoliaRpcUrls )
    {
        WeightedRpcEndpoint ep;
        ep.url                     = url;
        ep.consensus_weight        = 25;
        ep.bridge_contract_address = kSepoliaContract;
        ep.accepted_topic0_hashes  = hashes;
        endpoints.push_back( std::move( ep ) );
    }
    validator.SetRpcEndpoints( "11155111", std::move( endpoints ) );

    const std::string source_ref = "0x" + std::string( 64, 'b' );
    validator.SetTransportFactory(
        [&]( const std::string &, std::chrono::seconds ) {
            return std::make_unique<FixedReceiptTransport>(
                BuildValidReceiptJson( source_ref, kSepoliaContract, v2_topic0 ) );
        } );

    SGTransaction::DAGStruct dag;
    auto mint = std::make_shared<MintTransaction>(
        MintTransaction::New( 1, "11155111", TokenID::FromBytes( { 0x00 } ), dag ) );

    EXPECT_TRUE( PublicChainInputValidatorTestAccess::Verify( validator, mint, source_ref ) )
        << "A v2 (BridgeOutInitiated) burn receipt must pass witness validation";

    // 3. Negative control: a topic0 that is NOT in accepted_topic0_hashes is
    //    still rejected (the fix must not weaken receipt-log validation).
    const std::string bogus_topic0 = "0x" + std::string( 63, 'c' ) + "1";
    validator.SetTransportFactory(
        [&]( const std::string &, std::chrono::seconds ) {
            return std::make_unique<FixedReceiptTransport>(
                BuildValidReceiptJson( source_ref, kSepoliaContract, bogus_topic0 ) );
        } );
    EXPECT_FALSE( PublicChainInputValidatorTestAccess::Verify( validator, mint, source_ref ) )
        << "A receipt with an unknown topic0 must still be rejected";
}

// Option 3: the provider discovers RPC URLs via the runtime chainlist fetch
// (no config "rpc" array, no manual SetRpcEndpoints). The canned fetch returns
// 3 Sepolia URLs (3 x 25 = 75) so a verified receipt reaches quorum.
TEST( BridgeE2EChainlistTest, RuntimeFetchWiresEndpointsThatReachQuorum )
{
    auto config_path = WriteTempBridgeConfig();
    ASSERT_TRUE( fs::exists( config_path ) );

    PublicChainInputValidator validator;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistFetch );
    ASSERT_TRUE( provider.Initialize( config_path, validator ) );
    ASSERT_TRUE( validator.GetFirstRpcUrl( "11155111" ).has_value() )
        << "Runtime fetch must wire real endpoints";

    const auto        v2_hash   = eth::abi::event_signature_hash( std::string( kBridgeOutInitiatedSig ) );
    const std::string v2_topic0 = rlp::base::parse::hex_bytes( v2_hash.data(), v2_hash.size() );
    const std::string source_ref = "0x" + std::string( 64, 'f' );
    validator.SetTransportFactory(
        [&]( const std::string &, std::chrono::seconds ) {
            return std::make_unique<FixedReceiptTransport>(
                BuildValidReceiptJson( source_ref, kSepoliaContract, v2_topic0 ) );
        } );

    SGTransaction::DAGStruct dag;
    auto mint = std::make_shared<MintTransaction>(
        MintTransaction::New( 1, "11155111", TokenID::FromBytes( { 0x00 } ), dag ) );

    EXPECT_TRUE( PublicChainInputValidatorTestAccess::Verify( validator, mint, source_ref ) )
        << "Runtime-fetched endpoints must reach the 75-weight quorum with no manual override";
}

// A failed chainlist fetch leaves the chain with no endpoints (fail-closed),
// but Initialize still returns true (chain registered for relayer watch).
TEST( BridgeE2EChainlistTest, FailedFetchLeavesChainWithoutEndpoints )
{
    auto config_path = WriteTempBridgeConfig();
    ASSERT_TRUE( fs::exists( config_path ) );

    PublicChainInputValidator validator;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( []() -> std::optional<std::string> { return std::nullopt; } );

    EXPECT_TRUE( provider.Initialize( config_path, validator ) )
        << "A failed fetch must not prevent chain discovery (relayer watch still registers)";
    EXPECT_FALSE( validator.GetFirstRpcUrl( "11155111" ).has_value() )
        << "A failed fetch must leave the chain with no endpoints (fail-closed)";
}

TEST( BridgeE2EChainlistTest, ObserverReceivesConfiguredChain )
{
    auto config_path = WriteTempBridgeConfig();
    ASSERT_TRUE( fs::exists( config_path ) );

    // Recording observer from Phase 05.1 unit tests
    struct Recorder final : IBridgeInitObserver
    {
        std::vector<ChainContractPair> chains;
        bool                           called = false;

        void OnRpcEndpointsReady( std::vector<ChainContractPair> c ) override
        {
            chains = std::move( c );
            called = true;
        }
    };

    Recorder                  recorder;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistFetch );
    PublicChainInputValidator validator;

    provider.AddObserver( recorder );
    bool result = provider.Initialize( config_path, validator );

    ASSERT_TRUE( result );
    ASSERT_TRUE( recorder.called );
    ASSERT_EQ( recorder.chains.size(), 1u );
    EXPECT_EQ( recorder.chains[0].chain_id, 11155111u );
    EXPECT_EQ( recorder.chains[0].chain_name, "ethereum-sepolia" );
    EXPECT_EQ( recorder.chains[0].contract_address, kSepoliaContract );
}

TEST( BridgeE2EChainlistTest, ChainlistEndpointsLoadedAndFiltered )
{
    // Verify chainlist provider API works with real data
    auto parse_result = eth::rpc::load_chainlist_from_json_text( kChainlistJson );
    ASSERT_TRUE( parse_result.has_value() );

    // Filter to only Sepolia (chain_id = 11155111)
    std::vector<uint64_t> configured = { 11155111 };
    auto                  filtered   = eth::rpc::filter_to_configured_chains( parse_result.value(), configured );

    ASSERT_FALSE( filtered.empty() );
    EXPECT_EQ( filtered.size(), 3u ) << "Should have 3 Sepolia endpoints";

    for ( const auto &ep : filtered )
    {
        EXPECT_EQ( ep.chain_id, 11155111u );
        EXPECT_FALSE( ep.url_template.empty() );
    }
}

// ─── Cleanup ────────────────────────────────────────────────────────────────

TEST( BridgeE2EChainlistTest, CleanupTempFiles )
{
    auto config_path = WriteTempBridgeConfig();
    ASSERT_TRUE( fs::exists( config_path ) );
    std::error_code ec;
    fs::remove( config_path, ec );
    EXPECT_FALSE( fs::exists( config_path ) );
}
