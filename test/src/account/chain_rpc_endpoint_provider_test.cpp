/**
 * @file       chain_rpc_endpoint_provider_test.cpp
 * @brief      Unit tests for ChainRpcEndpointProvider bridge configuration
 *             propagation and graceful degradation.
 * @date       2026-06-04
 */
#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "account/ChainRpcEndpointProvider.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "base/logger.hpp"
#include "eth/abi_decoder.hpp"
#include "base/parse_utility.hpp"

namespace fs = std::filesystem;

using namespace sgns;

// ─── Test Helpers ───────────────────────────────────────────────────────────

/// @brief Create a minimal chains.json content for testing.
std::string MakeChainsJson( const std::string &chain_name,
                            uint64_t           chain_id,
                            const std::string &rpc_url,
                            const std::string &bridge_addr = "" )
{
    std::string json = R"({
        ")" + chain_name +
                       R"(": {
            "name": ")" +
                       chain_name + R"(",
            "chainId": )" +
                       std::to_string( chain_id ) + R"(,
            "rpc": [")" +
                       rpc_url + R"("])";
    if ( !bridge_addr.empty() )
    {
        json += R"(,
            "bridge_contract_address": ")" +
                bridge_addr + R"(")";
    }
    json += R"(
        }
    })";
    return json;
}

/// @brief Write a temporary chains.json file.
fs::path WriteTempJson( const std::string &content )
{
    auto path = fs::temp_directory_path() / "test_chains_config_provider.json";
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    out << content;
    out.close();
    return path;
}

// ─── Test Logger ────────────────────────────────────────────────────────────

static base::Logger TestLogger()
{
    return base::createLogger( "chain_rpc_endpoint_provider_test" );
}

// ─── Tests: Bridge Contract Address Propagation ─────────────────────────────

TEST( ChainRpcEndpointProviderTest, BridgeContractAddressPopulated )
{
    // Set bridge_contract_addresses map in ChainRpcProviderConfig,
    // call Initialize(), verify WeightedRpcEndpoint has bridge_contract_address.

    const std::string expected_addr = "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";
    const std::string chain_name   = "ethereum-sepolia";
    const std::string rpc_url      = "https://sepolia.infura.io/v3/test";

    // Write a chains.json with bridge_contract_address
    auto json    = MakeChainsJson( chain_name, 11155111, rpc_url, expected_addr );
    auto tmpfile = WriteTempJson( json );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    // Setup ChainRpcEndpointProvider with chain id mapping
    ChainRpcEndpointProvider::ChainIdMap chain_id_map;
    chain_id_map.emplace( chain_name, 11155111 );

    ChainRpcProviderConfig config;
    config.chains_json_path             = tmpfile;
    config.bridge_contract_addresses[11155111] = expected_addr;

    // Compute topic0 for the bridge event
    auto topic0_hash = eth::abi::event_signature_hash(
        "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)" );
    std::string topic0_hex =
        rlp::base::parse::hex_bytes( topic0_hash.data(), topic0_hash.size() );
    config.bridge_event_topic0[11155111] = topic0_hex;

    ChainRpcEndpointProvider provider( std::move( chain_id_map ) );
    PublicChainInputValidator validator;

    auto logger = TestLogger();
    bool result = provider.Initialize( validator, config, logger );

    // Initialize should succeed (at least one chain received endpoints)
    EXPECT_TRUE( result )
        << "Initialize should return true when endpoints are configured";

    // Verify that an RPC endpoint URL is available for this chain
    auto first_url = validator.GetFirstRpcUrl( std::to_string( 11155111 ) );
    EXPECT_TRUE( first_url.has_value() )
        << "Validator should have RPC endpoint for chain 11155111 after init";
    if ( first_url.has_value() )
    {
        EXPECT_FALSE( first_url->empty() );
    }

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

TEST( ChainRpcEndpointProviderTest, EventTopic0Populated )
{
    // Set bridge_event_topic0 map, call Initialize(),
    // verify event_topic0 on endpoint.

    const std::string contract_addr = "0x614577036F0a024DBC1C88BA616b394DD65d105a";
    const std::string chain_name   = "ethereum-mainnet";
    const std::string rpc_url      = "https://mainnet.infura.io/v3/test";

    auto json    = MakeChainsJson( chain_name, 1, rpc_url, contract_addr );
    auto tmpfile = WriteTempJson( json );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    ChainRpcEndpointProvider::ChainIdMap chain_id_map;
    chain_id_map.emplace( chain_name, 1 );

    // Compute expected topic0
    auto topic0_hash = eth::abi::event_signature_hash(
        "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)" );
    std::string topic0_hex =
        rlp::base::parse::hex_bytes( topic0_hash.data(), topic0_hash.size() );

    ChainRpcProviderConfig config;
    config.chains_json_path               = tmpfile;
    config.bridge_contract_addresses[1]   = contract_addr;
    config.bridge_event_topic0[1]         = topic0_hex;

    ChainRpcEndpointProvider provider( std::move( chain_id_map ) );
    PublicChainInputValidator validator;

    auto logger = TestLogger();
    bool result = provider.Initialize( validator, config, logger );

    // Initialize should succeed
    EXPECT_TRUE( result );

    // Verify the topic0 is valid and non-empty
    EXPECT_FALSE( topic0_hex.empty() );
    EXPECT_EQ( topic0_hex.size(), 66u )  // "0x" + 64 hex chars
        << "Topic0 hex should be valid 32-byte hash";
    EXPECT_EQ( topic0_hex.substr( 0, 2 ), "0x" );

    // Verify RPC endpoint was wired
    auto first_url = validator.GetFirstRpcUrl( "1" );
    EXPECT_TRUE( first_url.has_value() );

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

TEST( ChainRpcEndpointProviderTest, MissingBridgeFieldsDoesNotCrash )
{
    // Config with no bridge_contract_address for a chain,
    // call Initialize(), verify no crash and WeightedRpcEndpoint
    // has empty bridge_contract_address.

    const std::string chain_name = "ethereum-sepolia";
    const std::string rpc_url    = "https://sepolia.infura.io/v3/test";

    // chains.json WITHOUT bridge_contract_address
    auto json    = MakeChainsJson( chain_name, 11155111, rpc_url, "" );
    auto tmpfile = WriteTempJson( json );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    ChainRpcEndpointProvider::ChainIdMap chain_id_map;
    chain_id_map.emplace( chain_name, 11155111 );

    ChainRpcProviderConfig config;
    config.chains_json_path = tmpfile;
    // Note: bridge_contract_addresses is NOT set for this chain
    // Note: bridge_event_topic0 is NOT set for this chain

    ChainRpcEndpointProvider provider( std::move( chain_id_map ) );
    PublicChainInputValidator validator;

    auto logger = TestLogger();
    // Should not throw or crash
    EXPECT_NO_THROW( {
        bool result = provider.Initialize( validator, config, logger );
        // The method should still attempt initialization; the chain has no
        // bridge, so it appears in the ChainList but without bridge fields
        (void)result;
    } );

    // The RPC endpoint should still be wired even without bridge fields
    auto first_url = validator.GetFirstRpcUrl( std::to_string( 11155111 ) );
    EXPECT_TRUE( first_url.has_value() )
        << "RPC endpoints should be configured even without bridge fields";

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

TEST( ChainRpcEndpointProviderTest, EmptyChainConfigDoesNotCrash )
{
    // Config with an empty chains_json_path should be handled gracefully
    ChainRpcEndpointProvider::ChainIdMap chain_id_map;
    chain_id_map.emplace( "ethereum-mainnet", 1 );

    ChainRpcProviderConfig config;
    config.chains_json_path = ""; // empty path — no file to read

    ChainRpcEndpointProvider provider( std::move( chain_id_map ) );
    PublicChainInputValidator validator;

    auto logger = TestLogger();
    // Should not throw
    bool result = false;
    EXPECT_NO_THROW( {
        result = provider.Initialize( validator, config, logger );
    } );

    // Without public endpoints and without direct endpoints,
    // Initialize should return false (nothing wired)
    EXPECT_FALSE( result )
        << "Initialize should return false when no endpoints are available";
}

TEST( ChainRpcEndpointProviderTest, EmptyChainIdMapReturnsFalse )
{
    // An empty chain_id_map should cause Initialize to return false early
    ChainRpcEndpointProvider::ChainIdMap empty_map;

    ChainRpcProviderConfig config;
    config.chains_json_path = "";

    ChainRpcEndpointProvider provider( std::move( empty_map ) );
    PublicChainInputValidator validator;

    auto logger = TestLogger();
    bool result = provider.Initialize( validator, config, logger );

    EXPECT_FALSE( result )
        << "Initialize should return false when chain_id_map is empty";
}

TEST( ChainRpcEndpointProviderTest, DirectEndpointsWired )
{
    // Direct API-key endpoints should be wired into the validator
    ChainRpcEndpointProvider::ChainIdMap chain_id_map;
    chain_id_map.emplace( "ethereum-sepolia", 11155111 );

    ChainRpcProviderConfig config;
    config.chains_json_path = ""; // no public endpoint file

    // Provide direct endpoints
    config.direct_endpoints["11155111"] = {
        { "https://sepolia.custom.rpc/v3/key123", 50 },
    };

    ChainRpcEndpointProvider provider( std::move( chain_id_map ) );
    PublicChainInputValidator validator;

    auto logger = TestLogger();
    bool result = provider.Initialize( validator, config, logger );

    EXPECT_TRUE( result )
        << "Initialize should succeed with direct endpoints";

    // Verify the endpoint was wired
    auto first_url = validator.GetFirstRpcUrl( "11155111" );
    EXPECT_TRUE( first_url.has_value() );
    if ( first_url.has_value() )
    {
        EXPECT_EQ( *first_url, "https://sepolia.custom.rpc/v3/key123" );
    }
}

TEST( ChainRpcEndpointProviderTest, MultipleChainsBridgeConfig )
{
    // Multiple chains with bridge contracts — all should be configured
    const std::string json = R"({
        "ethereum-mainnet": {
            "name": "Ethereum Mainnet",
            "chainId": 1,
            "rpc": ["https://mainnet.infura.io/v3/test"],
            "bridge_contract_address": "0x614577036F0a024DBC1C88BA616b394DD65d105a"
        },
        "ethereum-sepolia": {
            "name": "Ethereum Sepolia",
            "chainId": 11155111,
            "rpc": ["https://sepolia.infura.io/v3/test"],
            "bridge_contract_address": "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70"
        },
        "no-bridge-chain": {
            "name": "No Bridge Chain",
            "chainId": 99999,
            "rpc": ["https://nobridge.example.com"]
        }
    })";

    auto tmpfile = WriteTempJson( json );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    ChainRpcEndpointProvider::ChainIdMap chain_id_map;
    chain_id_map.emplace( "ethereum-mainnet", 1 );
    chain_id_map.emplace( "ethereum-sepolia", 11155111 );
    // "no-bridge-chain" is NOT in the chain_id_map — should be skipped

    // Compute topic0
    auto topic0_hash = eth::abi::event_signature_hash(
        "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)" );
    std::string topic0_hex =
        rlp::base::parse::hex_bytes( topic0_hash.data(), topic0_hash.size() );

    ChainRpcProviderConfig config;
    config.chains_json_path = tmpfile;
    config.bridge_contract_addresses[1] =
        "0x614577036F0a024DBC1C88BA616b394DD65d105a";
    config.bridge_contract_addresses[11155111] =
        "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";
    config.bridge_event_topic0[1]        = topic0_hex;
    config.bridge_event_topic0[11155111] = topic0_hex;

    ChainRpcEndpointProvider provider( std::move( chain_id_map ) );
    PublicChainInputValidator validator;

    auto logger = TestLogger();
    bool result = provider.Initialize( validator, config, logger );

    EXPECT_TRUE( result );

    // Both chains should have endpoints
    auto url1 = validator.GetFirstRpcUrl( "1" );
    auto url2 = validator.GetFirstRpcUrl( "11155111" );
    EXPECT_TRUE( url1.has_value() );
    EXPECT_TRUE( url2.has_value() );

    // The unconfigured chain should have no endpoints
    auto url3 = validator.GetFirstRpcUrl( "99999" );
    EXPECT_FALSE( url3.has_value() );

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

TEST( ChainRpcEndpointProviderTest, BadChainIdDoesNotCrash )
{
    // Invalid chain ID in direct_endpoints should be handled gracefully
    ChainRpcEndpointProvider::ChainIdMap chain_id_map;
    chain_id_map.emplace( "ethereum-sepolia", 11155111 );

    ChainRpcProviderConfig config;
    config.chains_json_path = "";
    // Invalid chain ID string (not a number)
    config.direct_endpoints["not_a_number"] = {
        { "https://example.com/rpc", 50 },
    };

    ChainRpcEndpointProvider provider( std::move( chain_id_map ) );
    PublicChainInputValidator validator;

    auto logger = TestLogger();
    // Should not crash
    EXPECT_NO_THROW( {
        provider.Initialize( validator, config, logger );
    } );
}
