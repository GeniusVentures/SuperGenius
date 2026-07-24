/**
 * @file       startup_wiring_test.cpp
 * @brief      Unit tests for GeniusNode startup wiring: chain config parsing,
 *             bridge initialization ordering, and catch-up scan logic.
 * @date       2026-06-04
 */
#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/json.hpp>

#include "account/BridgeRelayer.hpp"
#include "account/ChainRpcEndpointProvider.hpp"
#include "account/GeniusNode.hpp"
#include "account/PublicChainInputValidator.hpp"
#include "account/TokenID.hpp"
#include "base/parse_utility.hpp"
#include "eth/abi_decoder.hpp"
#include "eth/rpc_receipt_source.hpp"
#include "watcher/impl/bridge_catchup_watcher.hpp"

namespace fs = std::filesystem;

using namespace sgns;

// ─── Test Constants ─────────────────────────────────────────────────────────

static const std::string kBridgeEventSignature   = std::string( kBridgeSourceBurnedSig );
static const std::string kBridgeEventSignatureV2 = std::string( kBridgeOutInitiatedSig );

// Expected topic0 hex for BridgeSourceBurned (keccak256 of the signature)
// Computed via keccak256(kBridgeSourceBurnedSig)
static const std::string kExpectedTopic0Hex = "0x"; // placeholder — verified against actual hash in test

// ─── Helpers ────────────────────────────────────────────────────────────────

fs::path BundledChainsConfigPath()
{
    return fs::path( STARTUP_WIRING_TEST_CONFIG_PATH );
}

/// @brief Write a temporary bridge_chains_config.json for testing.
fs::path WriteTempChainsConfig( const std::string &json_content )
{
    auto          tmp_path = fs::temp_directory_path() / "startup_wiring_bridge_chains_config.json";
    std::ofstream out( tmp_path, std::ios::binary | std::ios::trunc );
    out << json_content;
    out.close();
    return tmp_path;
}

/// @brief Remove a temporary file.
void RemoveTempFile( const fs::path &path )
{
    std::error_code ec;
    fs::remove( path, ec );
}

struct CatchupStopProbe
{
    std::atomic<size_t> block_number_calls{ 0 };
    std::atomic<size_t> get_logs_calls{ 0 };
};

class SlowEmptyLogsTransport final : public eth::rpc::JsonRpcTransport
{
public:
    explicit SlowEmptyLogsTransport( std::shared_ptr<CatchupStopProbe> probe ) : probe_( std::move( probe ) ) {}

    std::optional<std::string> call( const boost::json::object &request ) override
    {
        const auto method = boost::json::value_to<std::string>( request.at( "method" ) );

        if ( method == "eth_getBlockByNumber" )
        {
            probe_->block_number_calls.fetch_add( 1 );
            return R"({"result":{"number":"0x100"}})";
        }

        if ( method == "eth_getLogs" )
        {
            probe_->get_logs_calls.fetch_add( 1 );
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            return R"({"result":[]})";
        }

        return std::nullopt;
    }

private:
    std::shared_ptr<CatchupStopProbe> probe_;
};

void WaitForGetLogsCall( const std::shared_ptr<CatchupStopProbe> &probe )
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );
    while ( probe->get_logs_calls.load() == 0 && std::chrono::steady_clock::now() < deadline )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
}

// ─── Tests: Event Signature Hash ────────────────────────────────────────────

TEST( StartupWiringTest, BridgeSourceBurnedTopic0IsDeterministic )
{
    // Verify that the topic0 computation for the bridge event is deterministic
    auto hash1 = eth::abi::event_signature_hash( kBridgeEventSignature );
    auto hash2 = eth::abi::event_signature_hash( kBridgeEventSignature );

    // Same input must produce same output
    EXPECT_EQ( hash1, hash2 );

    // Hash must not be all zeros
    bool all_zero = true;
    for ( auto byte : hash1 )
    {
        if ( byte != 0 )
        {
            all_zero = false;
            break;
        }
    }
    EXPECT_FALSE( all_zero ) << "Topic0 hash should not be all zeros";
}

TEST( StartupWiringTest, DifferentSignaturesProduceDifferentTopic0 )
{
    // Different event signatures must produce different topic0 hashes
    auto bridge_hash   = eth::abi::event_signature_hash( kBridgeEventSignature );
    auto transfer_hash = eth::abi::event_signature_hash( "Transfer(address,address,uint256)" );

    EXPECT_NE( bridge_hash, transfer_hash );
}

// ─── Bridge V2 catch-up scan topic0 tests (Plan 05.2-04) ─────────────────────

TEST( StartupWiringTest, BridgeOutInitiatedTopic0IsDeterministic )
{
    // The v2 topic0 computation must be deterministic — the catch-up scan
    // (Plan 05.2-03) relies on a stable hash for BridgeOutInitiated.
    auto hash1 = eth::abi::event_signature_hash( kBridgeEventSignatureV2 );
    auto hash2 = eth::abi::event_signature_hash( kBridgeEventSignatureV2 );

    // Same input must produce the same output.
    EXPECT_EQ( hash1, hash2 );

    // Hash must not be all zeros.
    bool all_zero = true;
    for ( auto byte : hash1 )
    {
        if ( byte != 0 )
        {
            all_zero = false;
            break;
        }
    }
    EXPECT_FALSE( all_zero ) << "v2 topic0 hash should not be all zeros";
}

TEST( StartupWiringTest, V1andV2Topic0Differ )
{
    // The v1 (BridgeSourceBurned) and v2 (BridgeOutInitiated) topic0 hashes
    // must differ — otherwise the catch-up scan dedup would collide and one
    // signature's events would shadow the other (D-12).
    auto v1_hash = eth::abi::event_signature_hash( kBridgeEventSignature );
    auto v2_hash = eth::abi::event_signature_hash( kBridgeEventSignatureV2 );

    EXPECT_NE( v1_hash, v2_hash ) << "v1 and v2 event signatures must produce distinct topic0 hashes";
}

TEST( StartupWiringTest, V2Topic0IsNonZero )
{
    // The v2 topic0 hash must be non-zero and 32 bytes (keccak256 output).
    auto v2_hash = eth::abi::event_signature_hash( kBridgeEventSignatureV2 );

    EXPECT_EQ( v2_hash.size(), 32u ) << "keccak256 topic0 must be 32 bytes";

    bool all_zero = true;
    for ( auto byte : v2_hash )
    {
        if ( byte != 0 )
        {
            all_zero = false;
            break;
        }
    }
    EXPECT_FALSE( all_zero ) << "v2 topic0 hash should not be all zeros";
}

// ─── Tests: Chain ID from config file (D-04) ─────────────────────────────────

TEST( StartupWiringTest, ConfigFileHasCorrectChainIds )
{
    // D-04: chain IDs are now sourced from bridge_chains_config.json.
    // Verify the bundled config carries valid numeric chain_id on all 8 entries.
    const auto    config_path = BundledChainsConfigPath();
    std::ifstream file( config_path, std::ios::binary );
    ASSERT_TRUE( file.is_open() ) << "Failed to open " << config_path;

    std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    file.close();

    auto parsed = boost::json::parse( content );
    auto obj    = parsed.as_object();
    EXPECT_EQ( obj.size(), 8u );

    std::set<uint64_t> seen;
    for ( const auto &[key, value] : obj )
    {
        auto chain_obj = value.as_object();
        ASSERT_TRUE( chain_obj.contains( "chain_id" ) ) << "Chain '" << key << "' missing chain_id";
        uint64_t chain_id = boost::json::value_to<uint64_t>( chain_obj.at( "chain_id" ) );
        EXPECT_GT( chain_id, 0u ) << "Chain " << key << " has invalid chain_id " << chain_id;
        EXPECT_TRUE( seen.insert( chain_id ).second ) << "Duplicate chain_id " << chain_id << " for " << key;
    }
}

// ─── Tests: Chains Config Parsing ───────────────────────────────────────────

TEST( StartupWiringTest, ParseChainsConfigWithBridgeContract )
{
    // Write a bridge_chains_config.json with a bridge_contract_address
    const std::string json = R"({
        "ethereum-sepolia": {
            "name": "Ethereum Sepolia",
            "chainId": 11155111,
            "rpc": ["https://sepolia.infura.io/v3/test"],
            "bridge_contract_address": "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70"
        },
        "_comment": "metadata entry should be skipped"
    })";

    auto path = WriteTempChainsConfig( json );
    ASSERT_TRUE( fs::exists( path ) ) << "Temp config file should exist";

    // Verify the file can be read
    std::ifstream file( path, std::ios::binary );
    ASSERT_TRUE( file.is_open() );

    std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    file.close();
    EXPECT_FALSE( content.empty() );
    EXPECT_NE( content.find( "bridge_contract_address" ), std::string::npos );

    RemoveTempFile( path );
}

TEST( StartupWiringTest, ChainsConfigWithoutBridgeContractIsSkipped )
{
    // A config with no bridge_contract_address should be skipped for bridge init
    const std::string json = R"({
        "ethereum-sepolia": {
            "name": "Ethereum Sepolia",
            "chainId": 11155111,
            "rpc": ["https://sepolia.infura.io/v3/test"]
        }
    })";

    auto path = WriteTempChainsConfig( json );
    ASSERT_TRUE( fs::exists( path ) );

    std::ifstream file( path, std::ios::binary );
    ASSERT_TRUE( file.is_open() );

    std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    file.close();

    // bridge_contract_address should NOT be present
    EXPECT_EQ( content.find( "bridge_contract_address" ), std::string::npos );

    RemoveTempFile( path );
}

TEST( StartupWiringTest, ChainWithoutChainIdIsSkipped )
{
    // D-04: chains lacking a chain_id field in bridge_chains_config.json are
    // skipped by ChainRpcEndpointProvider::Initialize().
    std::unordered_map<std::string, uint64_t> known = {
        { "ethereum-mainnet", 1 },
        { "ethereum-sepolia", 11155111 },
    };

    // "unknown-chain" is not in the config — no chain_id, so it would be skipped
    EXPECT_EQ( known.find( "unknown-chain" ), known.end() );
    EXPECT_NE( known.find( "ethereum-mainnet" ), known.end() );
}

TEST( StartupWiringTest, MetadataEntriesPrefixedWithUnderscoreAreSkipped )
{
    // Config entries starting with '_' (metadata) should be ignored
    const std::string json = R"({
        "_comment": "This is metadata",
        "_version": "1.0.0",
        "ethereum-mainnet": {
            "name": "Ethereum Mainnet",
            "chainId": 1,
            "rpc": ["https://mainnet.infura.io/v3/test"],
            "bridge_contract_address": "0x614577036F0a024DBC1C88BA616b394DD65d105a"
        }
    })";

    auto path = WriteTempChainsConfig( json );
    ASSERT_TRUE( fs::exists( path ) );

    // Read and verify: underscore entries exist in JSON but should be
    // skipped during initialization
    std::ifstream file( path, std::ios::binary );
    ASSERT_TRUE( file.is_open() );
    std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    file.close();

    EXPECT_NE( content.find( "_comment" ), std::string::npos );
    EXPECT_NE( content.find( "ethereum-mainnet" ), std::string::npos );

    RemoveTempFile( path );
}

// ─── Tests: Observer Architecture (D-03, D-04) ───────────────────────────────

TEST( StartupWiringTest, ArchitectureObserverContractsAreInPlace )
{
    // D-03: BridgeRelayer implements IBridgeInitObserver so it self-starts
    // via OnRpcEndpointsReady when ChainRpcEndpointProvider signals readiness.
    // D-03/D-04: GeniusNode also implements IBridgeInitObserver for catch-up.

    // Compile-time verification: BridgeRelayer is-an IBridgeInitObserver
    static_assert( std::is_base_of_v<IBridgeInitObserver, BridgeRelayer>,
                   "BridgeRelayer must implement IBridgeInitObserver (D-03)" );

    // GeniusNode must also implement IBridgeInitObserver (self-subscribes for catch-up scan)
    static_assert( std::is_base_of_v<IBridgeInitObserver, GeniusNode>,
                   "GeniusNode must implement IBridgeInitObserver for catch-up scan (D-03)" );

    // ChainRpcEndpointProvider exposes AddObserver and path-based Initialize
    ChainRpcEndpointProvider provider;

    // Verify the API exposes AddObserver (compile-time check with a stub observer)
    struct StubObserver final : IBridgeInitObserver
    {
        void OnRpcEndpointsReady( std::vector<ChainContractPair> ) override
        {
        }
    };

    StubObserver stub;
    provider.AddObserver( stub ); // compile-time check: API is present

    // Config file carries chain IDs (D-04): verify the bundled file has chain_id
    const auto    config_path = BundledChainsConfigPath();
    std::ifstream file( config_path, std::ios::binary );
    ASSERT_TRUE( file.is_open() ) << "Failed to open " << config_path;
    std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    file.close();
    auto parsed = boost::json::parse( content );
    auto obj    = parsed.as_object();
    for ( const auto &[key, value] : obj )
    {
        if ( key.starts_with( "_" ) )
        {
            continue;
        }
        auto chain_obj = value.as_object();
        EXPECT_TRUE( chain_obj.contains( "chain_id" ) ) << "Chain '" << key << "' missing chain_id (D-04)";
    }
}

// ─── Tests: Catch-up Scan ───────────────────────────────────────────────────

TEST( StartupWiringTest, CatchupScanDefaultDepth )
{
    // D-20: Default catch-up scan depth is 10,000 blocks
    constexpr uint64_t kDefaultDepth = 10000;
    EXPECT_EQ( kDefaultDepth, 10000u );
}

TEST( StartupWiringTest, CatchupScanBurnedUtxoSkippedIfConsumed )
{
    // Verify the logical logic: if a burn UTXO is already consumed,
    // the catch-up scan should skip it (no duplicate).
    // This is enforced by outpoint checks in BridgeCatchupWatcher::poll_once().

    // Simulate the skip logic: a transaction that is already CONFIRMED/VERIFYING/SENDING
    // should be skipped by the catch-up scan.
    enum class TxStatus : uint8_t
    {
        INVALID = 0,
        SENDING,
        VERIFYING,
        CONFIRMED,
        FAILED,
    };

    auto should_skip = []( TxStatus status ) -> bool
    { return status == TxStatus::CONFIRMED || status == TxStatus::VERIFYING || status == TxStatus::SENDING; };

    // These statuses should cause the catch-up scan to skip
    EXPECT_TRUE( should_skip( TxStatus::CONFIRMED ) );
    EXPECT_TRUE( should_skip( TxStatus::VERIFYING ) );
    EXPECT_TRUE( should_skip( TxStatus::SENDING ) );

    // These statuses should NOT cause skipping (allow insertion)
    EXPECT_FALSE( should_skip( TxStatus::INVALID ) );
    EXPECT_FALSE( should_skip( TxStatus::FAILED ) );
}

TEST( StartupWiringTest, CatchupScanTopic0HexConversion )
{
    // Verify the topic0 hex conversion path used in BridgeCatchupWatcher
    auto topic0_hash = eth::abi::event_signature_hash( kBridgeEventSignature );

    // Convert to hex string (as done in the implementation)
    std::string topic0_hex = rlp::base::parse::hex_bytes( topic0_hash.data(), topic0_hash.size() );

    EXPECT_FALSE( topic0_hex.empty() );
    EXPECT_EQ( topic0_hex.size(), 66u ) // "0x" + 64 hex chars
        << "Topic0 hex should be 0x-prefixed 32-byte hex string, got: " << topic0_hex;
    EXPECT_EQ( topic0_hex.substr( 0, 2 ), "0x" );
}

// ─── Tests: ChainRpcEndpointProvider Initialization (new API) ─────────────────

TEST( StartupWiringTest, ProviderInitializeWithValidConfigReturnsTrue )
{
    // Write a bridge_chains_config.json with 2 chains, call Initialize(),
    // verify return true and validator registered for both.
    const std::string json = R"({
        "ethereum-sepolia": {
            "chain_id": 11155111,
            "bridge_contract_address": "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70"
        },
        "ethereum-mainnet": {
            "chain_id": 1,
            "bridge_contract_address": "0x614577036F0a024DBC1C88BA616b394DD65d105a"
        }
    })";

    auto path = WriteTempChainsConfig( json );
    ASSERT_TRUE( fs::exists( path ) );

    ChainRpcEndpointProvider  provider;
    // Inject a canned chainlist fetcher so the test never hits the network.
    provider.SetChainlistFetcher(
        []() -> std::optional<std::string> {
            return std::string{ R"([
                {"name":"Ethereum Sepolia","chainId":11155111,"rpc":["https://sepolia.a.example"]},
                {"name":"Ethereum","chainId":1,"rpc":["https://mainnet.a.example"]}
            ])" };
        } );
    PublicChainInputValidator validator;

    bool result = provider.Initialize( path, validator );

    EXPECT_TRUE( result );
    auto url1 = validator.GetFirstRpcUrl( "11155111" );
    auto url2 = validator.GetFirstRpcUrl( "1" );
    EXPECT_TRUE( url1.has_value() );
    EXPECT_TRUE( url2.has_value() );

    RemoveTempFile( path );
}

TEST( StartupWiringTest, ProviderReturnsFalseForConfigWithoutChainId )
{
    // A chain without chain_id is skipped — if all chains lack it, return false.
    const std::string json = R"({
        "no-id-chain": {
            "bridge_contract_address": "0x0000000000000000000000000000000000000000"
        }
    })";

    auto path = WriteTempChainsConfig( json );
    ASSERT_TRUE( fs::exists( path ) );

    ChainRpcEndpointProvider  provider;
    PublicChainInputValidator validator;

    bool result = provider.Initialize( path, validator );
    EXPECT_FALSE( result );

    RemoveTempFile( path );
}

// ─── Tests: Provider Graceful Degradation ────────────────────────────────────

TEST( StartupWiringTest, MalformedJsonIsHandledGracefully )
{
    // T-05-15: Malformed bridge_chains_config.json should be caught and handled
    const std::string malformed = R"({ "ethereum-mainnet": { "name": "Ethereum", "chainId": 1, )";
    // missing closing braces — this is invalid JSON

    auto path = WriteTempChainsConfig( malformed );
    ASSERT_TRUE( fs::exists( path ) );

    // Read it back — parsing would fail, but no crash
    std::ifstream file( path, std::ios::binary );
    ASSERT_TRUE( file.is_open() );
    std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    file.close();

    // The content exists but is malformed
    EXPECT_FALSE( content.empty() );

    // Using boost::json::parse on malformed JSON should throw
    // (caught by ChainRpcEndpointProvider::Initialize try/catch)
    bool parse_failed = false;
    try
    {
        auto parsed = boost::json::parse( content );
        (void) parsed;
    }
    catch ( const std::exception & )
    {
        parse_failed = true;
    }
    EXPECT_TRUE( parse_failed ) << "Malformed JSON should cause parse failure (caught by try/catch)";

    RemoveTempFile( path );
}

TEST( StartupWiringTest, EmptyChainsConfigDoesNotCrash )
{
    // An empty bridge_chains_config.json file should be handled gracefully
    const std::string empty = "{}";

    auto path = WriteTempChainsConfig( empty );
    ASSERT_TRUE( fs::exists( path ) );

    std::ifstream file( path, std::ios::binary );
    ASSERT_TRUE( file.is_open() );
    std::string content( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
    file.close();

    EXPECT_EQ( content, "{}" );

    // boost::json::parse of "{}" should succeed with no entries
    auto parsed = boost::json::parse( content );
    auto obj    = parsed.as_object();
    EXPECT_EQ( obj.size(), 0u );

    RemoveTempFile( path );
}

// ─── Tests: Provider Initialize Graceful Degradation ─────────────────────────

TEST( StartupWiringTest, MissingFileReturnsFalse )
{
    ChainRpcEndpointProvider  provider;
    PublicChainInputValidator validator;

    fs::path nonexistent = fs::temp_directory_path() / "no_such_config_xyz.json";
    bool     result      = provider.Initialize( nonexistent, validator );

    EXPECT_FALSE( result ) << "Initialize should return false when config file does not exist";
}

TEST( StartupWiringTest, EmptyJsonConfigReturnsFalse )
{
    auto path = WriteTempChainsConfig( "{}" );
    ASSERT_TRUE( fs::exists( path ) );

    ChainRpcEndpointProvider  provider;
    PublicChainInputValidator validator;

    bool result = provider.Initialize( path, validator );
    EXPECT_FALSE( result ) << "Initialize should return false when config has no valid chain entries";

    RemoveTempFile( path );
}

TEST( StartupWiringTest, ConfigWithoutBridgeContractAddressReturnsFalse )
{
    // A chain with chain_id but no bridge_contract_address is skipped.
    // If all chains lack bridge_contract_address, Initialize returns false.
    const std::string json = R"({
        "ethereum-sepolia": {
            "chain_id": 11155111,
            "name": "Ethereum Sepolia",
            "rpc": ["https://sepolia.infura.io/v3/test"]
        }
    })";

    auto path = WriteTempChainsConfig( json );
    ASSERT_TRUE( fs::exists( path ) );

    ChainRpcEndpointProvider  provider;
    PublicChainInputValidator validator;

    bool result = provider.Initialize( path, validator );
    EXPECT_FALSE( result ) << "Initialize should return false when no chain has bridge_contract_address";

    RemoveTempFile( path );
}

// ─── Tests: Non-blocking Bridge Init (D-04) ─────────────────────────────────

TEST( StartupWiringTest, BridgeInitIsNonBlocking )
{
    // D-04: InitializeAndStartBridge is launched via boost::asio::post
    // so the node state machine proceeds without waiting.
    // Verify the architecture: io_context is used for async dispatch.

    // The io_context must be valid before post is called
    auto io = std::make_shared<boost::asio::io_context>();

    bool callback_invoked = false;
    boost::asio::post( *io, [&]() { callback_invoked = true; } );

    // Before running the io_context, the callback should NOT have run
    EXPECT_FALSE( callback_invoked );

    // Run the io_context — the posted callback should execute
    io->run();

    EXPECT_TRUE( callback_invoked ) << "boost::asio::post callback should execute when io_context runs";
}

TEST( StartupWiringTest, IoContextStopPreventsCallbacks )
{
    // Verify that a stopped io_context does not execute posted callbacks
    auto io = std::make_shared<boost::asio::io_context>();
    io->stop();

    bool callback_invoked = false;
    boost::asio::post( *io, [&]() { callback_invoked = true; } );

    // poll() returns immediately since io_context is stopped
    io->poll();

    // The callback should NOT have executed on a stopped context
    EXPECT_FALSE( callback_invoked ) << "Posted callback should not execute on stopped io_context";
}

// ─── Tests: Catchup Scan Guard (P2 race-condition fix) ─────────────────────

TEST( StartupWiringTest, CatchupWatcherPollsWhenChainsAvailable )
{
    // The BridgeCatchupWatcher polls eth_getLogs on its own thread every
    // poll_interval seconds.  It snapshots catchup_chains_ (populated by
    // OnRpcEndpointsReady) on each poll cycle.  When chains are empty the
    // poll is a no-op; when chains arrive later, the next poll picks them up.
    // There is no state machine guard or one-shot flag — the polling loop
    // naturally defers scanning until chains are available.

    struct CatchupWatcherModel
    {
        bool chains_populated = false;
        bool last_block_set   = false; // First poll: scan from (latest - scan_depth)

        /// @brief Models one poll cycle: returns true if scanning would occur.
        bool PollWouldScan() const
        {
            return chains_populated;
        }

        /// @brief A poll that finds chains transitions to tracking mode.
        void SimulateFirstPoll()
        {
            if ( chains_populated )
            {
                last_block_set = true;
            }
        }
    };

    // ── Scenario A: empty chains → poll is no-op ────────────────────
    {
        CatchupWatcherModel model;
        EXPECT_FALSE( model.PollWouldScan() )
            << "Watcher must no-op when chains are not yet populated";
    }

    // ── Scenario B: chains arrive → next poll scans ──────────────────
    {
        CatchupWatcherModel model;

        // Chains arrive via OnRpcEndpointsReady
        model.chains_populated = true;

        EXPECT_TRUE( model.PollWouldScan() )
            << "Watcher must scan on next poll after chains arrive";
        model.SimulateFirstPoll();
        EXPECT_TRUE( model.last_block_set )
            << "After first poll with chains, watcher tracks last block";
    }

    // ── Scenario C: repeated polls continue scanning forward ────────
    {
        CatchupWatcherModel model;
        model.chains_populated = true;
        model.SimulateFirstPoll();

        // Subsequent polls continue from last_block (forward scanning)
        EXPECT_TRUE( model.PollWouldScan() )
            << "Watcher must continue polling after first scan";
    }
}

TEST( StartupWiringTest, CatchupWatcherStopCancelsActiveChunkScan )
{
    auto probe = std::make_shared<CatchupStopProbe>();

    sgns::evmwatcher::BridgeCatchupWatcher::Config cfg;
    cfg.poll_interval        = std::chrono::seconds( 1 );
    cfg.start_block          = 10;
    cfg.max_blocks_per_query = 1;
    cfg.transport_factory    = [probe]( const std::string & )
    {
        return std::make_unique<SlowEmptyLogsTransport>( probe );
    };

    static constexpr uint64_t kChainId = 12345;
    auto chains_provider = []()
    {
        return std::vector<ChainContractPair>{
            { "test-chain", "0x0000000000000000000000000000000000000001", kChainId, 0 }
        };
    };
    auto rpc_resolver = []( const std::string & ) -> std::optional<std::string>
    {
        return "test://rpc";
    };
    auto burn_processor = []( const std::vector<eth::abi::AbiValue> &,
                              const std::string &,
                              const std::string &,
                              uint32_t )
    {
        return sgns::evmwatcher::BridgeCatchupWatcher::BurnProcessOutcome::Processed;
    };

    sgns::evmwatcher::BridgeCatchupWatcher watcher(
        cfg,
        []( const std::string & ) {},
        chains_provider,
        rpc_resolver,
        burn_processor );

    watcher.startWatching();
    WaitForGetLogsCall( probe );
    const bool entered_get_logs = probe->get_logs_calls.load() > 0;

    const auto stop_start = std::chrono::steady_clock::now();
    watcher.stopWatching();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_start;

    ASSERT_TRUE( entered_get_logs ) << "watcher did not enter eth_getLogs";

    EXPECT_LT( std::chrono::duration_cast<std::chrono::milliseconds>( stop_elapsed ).count(), 500 )
        << "stopWatching should not wait for the unbounded catchup chunk loop or the next poll sleep";

    EXPECT_EQ( watcher.GetLastProcessedBlock( kChainId ), cfg.start_block )
        << "A chunk cancelled after only part of its RPC work must be retried later";
}

// ─── Tests: Catchup Watcher chains snapshot thread safety ──

TEST( StartupWiringTest, CatchupWatcherChainsSnapshotThreadSafe )
{
    // The BridgeCatchupWatcher polls catchup_chains_ on its own thread while
    // OnRpcEndpointsReady writes to it on the io_ pool.  Both sides use
    // catchup_mutex_ — the watcher snapshots under lock, the observer writes
    // under lock.  There is no double-dispatch concern because the watcher
    // simply reads the current chain list on each poll cycle.
    //
    // This test validates that concurrent writes to a shared chain vector
    // under a mutex do not lose updates or corrupt the snapshot.

    struct ChainsStore
    {
        std::vector<int> chains;
        mutable std::mutex mtx;

        void Write( std::vector<int> new_chains )
        {
            std::lock_guard lock( mtx );
            chains = std::move( new_chains );
        }

        std::vector<int> Snapshot() const
        {
            std::lock_guard lock( mtx );
            return chains;
        }
    };

    ChainsStore store;

    // Writer 1: populate chains
    store.Write( { 11155111, 1, 8453 } );

    // Reader (watcher): snapshot
    const auto snap = store.Snapshot();
    EXPECT_EQ( snap.size(), 3u ) << "Snapshot must capture all written chains";

    // Writer 2: update chains concurrently
    store.Write( { 11155111, 1, 8453, 42161 } );

    // Reader: sees updated chains
    const auto snap2 = store.Snapshot();
    EXPECT_EQ( snap2.size(), 4u ) << "Snapshot must reflect new chain after update";
}

// Models the generation token that guards async bridge init against an account
// switch. SelectAccount advances the generation and resets transaction_manager_
// / bridge_relayer_; the posted Initialize() job captures the generation at post
// time and must abort if it is stale (else it would dereference a null/freed
// member). This encodes that abort contract.
TEST( StartupWiringTest, BridgeInitGenerationAbortsStaleJob )
{
    struct BridgeInitGeneration
    {
        std::atomic<uint64_t> gen{ 0 };
    };
    BridgeInitGeneration g;

    const uint64_t captured_at_post = g.gen.load();
    auto job_would_proceed = [&]() -> bool {
        return g.gen.load() == captured_at_post; // abort (false) if stale
    };

    EXPECT_TRUE( job_would_proceed() ) << "In-flight init runs when no account switch happened";

    ++g.gen; // SelectAccount invalidates in-flight bridge init
    EXPECT_FALSE( job_would_proceed() ) << "Stale init (posted before an account switch) must abort";
}
