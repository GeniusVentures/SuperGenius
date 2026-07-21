/**
 * @file       chain_rpc_endpoint_provider_test.cpp
 * @brief      Unit tests for ChainRpcEndpointProvider file-driven initialization,
 *             observer notification, and graceful degradation.
 * @date       2026-06-17
 */
#include <gtest/gtest.h>

#include <atomic>
#include <fstream>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "account/ChainRpcEndpointProvider.hpp"
#include "account/PublicChainInputValidator.hpp"

namespace fs = std::filesystem;

using namespace sgns;

// ─── Test Helpers ───────────────────────────────────────────────────────────

/// @brief Recording observer that captures OnRpcEndpointsReady calls.
class RecordingObserver final : public IBridgeInitObserver
{
public:
    std::vector<ChainContractPair> received_chains;
    bool                           was_called = false;

    void OnRpcEndpointsReady( std::vector<ChainContractPair> chains ) override
    {
        received_chains = std::move( chains );
        was_called      = true;
    }
};

/// @brief Write a bridge_chains_config.json-format file (object keyed by chain
///        name, each value an object with numeric "chain_id" and string
///        "bridge_contract_address").
fs::path WriteTempConfigJson( const std::string &content )
{
    auto          path = fs::temp_directory_path() / "test_bridge_chains_config.json";
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    out << content;
    out.close();
    return path;
}

/// @brief Build a single-chain bridge_chains_config.json entry.
std::string MakeConfigJsonEntry( const std::string &chain_name, uint64_t chain_id, const std::string &contract_addr )
{
    return R"(")" + chain_name + R"(": { "chain_id": )" + std::to_string( chain_id ) +
           R"(, "bridge_contract_address": ")" + contract_addr + R"(" })";
}

/// @brief Build a full bridge_chains_config.json object from one or more entries.
std::string MakeConfigJson( const std::string &entries )
{
    return "{ " + entries + " }";
}

/// @brief Canned chainid.network-format dataset (string-form rpc) for tests —
///        no network. Covers Sepolia (11155111) and Mainnet (1).
std::optional<std::string> CannedChainlistJson()
{
    return std::string{ R"([
        {"name":"Ethereum Sepolia","chainId":11155111,
         "rpc":["https://sepolia.a.example","https://sepolia.b.example","https://sepolia.c.example"]},
        {"name":"Ethereum","chainId":1,
         "rpc":["https://mainnet.a.example","https://mainnet.b.example","https://mainnet.c.example"]}
    ])" };
}

// ─── Test: Observer notified on success ─────────────────────────────────────

TEST( ChainRpcEndpointProviderTest, NotifiesObserversWithChains )
{
    // Write a config with 2 chains — both should be accepted and reported.
    std::string entries = MakeConfigJsonEntry( "ethereum-sepolia",
                                               11155111,
                                               "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" ) +
                          ", " +
                          MakeConfigJsonEntry( "ethereum-mainnet", 1, "0x614577036F0a024DBC1C88BA616b394DD65d105a" );

    auto tmpfile = WriteTempConfigJson( MakeConfigJson( entries ) );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    RecordingObserver         recorder;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    PublicChainInputValidator validator;

    provider.AddObserver( recorder );
    bool result = provider.Initialize( tmpfile, validator );

    EXPECT_TRUE( result );
    EXPECT_TRUE( recorder.was_called );
    ASSERT_EQ( recorder.received_chains.size(), 2u );
    EXPECT_EQ( recorder.received_chains[0].chain_id, 11155111u );
    EXPECT_EQ( recorder.received_chains[1].chain_id, 1u );

    // Validator should have been registered for both chains
    auto url1 = validator.GetFirstRpcUrl( "11155111" );
    auto url2 = validator.GetFirstRpcUrl( "1" );
    EXPECT_TRUE( url1.has_value() );
    EXPECT_TRUE( url2.has_value() );

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

// ─── Test: Observer NOT notified when Initialize fails ──────────────────────

TEST( ChainRpcEndpointProviderTest, DoesNotNotifyObserversWhenInitializeFails )
{
    // Malformed JSON should return false and not notify observers.
    auto tmpfile = WriteTempConfigJson( "{" ); // invalid JSON
    ASSERT_TRUE( fs::exists( tmpfile ) );

    RecordingObserver         recorder;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    PublicChainInputValidator validator;

    provider.AddObserver( recorder );
    bool result = provider.Initialize( tmpfile, validator );

    EXPECT_FALSE( result );
    EXPECT_FALSE( recorder.was_called );
    EXPECT_TRUE( recorder.received_chains.empty() );

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

// ─── Test: Chain without chain_id is skipped ────────────────────────────────

TEST( ChainRpcEndpointProviderTest, ChainWithoutChainIdIsSkipped )
{
    // One entry has chain_id, the other does not — only the valid one counts.
    std::string entry_with = MakeConfigJsonEntry( "ethereum-sepolia",
                                                  11155111,
                                                  "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" );
    std::string entry_without =
        R"("no-id-chain": { "bridge_contract_address": "0x0000000000000000000000000000000000000000" })";

    auto tmpfile = WriteTempConfigJson( MakeConfigJson( entry_with + ", " + entry_without ) );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    RecordingObserver         recorder;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    PublicChainInputValidator validator;

    provider.AddObserver( recorder );
    bool result = provider.Initialize( tmpfile, validator );

    // The one valid chain lets Initialize return true
    EXPECT_TRUE( result );
    EXPECT_TRUE( recorder.was_called );
    // Observer should receive only 1 chain (the one with chain_id)
    ASSERT_EQ( recorder.received_chains.size(), 1u );
    EXPECT_EQ( recorder.received_chains[0].chain_name, "ethereum-sepolia" );

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

// ─── Test: Malformed JSON returns false without crashing ────────────────────

TEST( ChainRpcEndpointProviderTest, MalformedJsonReturnsFalse )
{
    auto tmpfile = WriteTempConfigJson( "{ not valid json" );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    PublicChainInputValidator validator;

    bool result = false;
    EXPECT_NO_THROW( { result = provider.Initialize( tmpfile, validator ); } );
    EXPECT_FALSE( result );

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

// ─── Test: Missing file returns false ───────────────────────────────────────

TEST( ChainRpcEndpointProviderTest, MissingFileReturnsFalse )
{
    fs::path nonexistent = fs::temp_directory_path() / "nonexistent_config_12345.json";

    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    PublicChainInputValidator validator;

    bool result = provider.Initialize( nonexistent, validator );
    EXPECT_FALSE( result );
}

// ─── Test: Empty JSON object returns false ──────────────────────────────────

TEST( ChainRpcEndpointProviderTest, EmptyJsonReturnsFalse )
{
    auto tmpfile = WriteTempConfigJson( "{}" );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    PublicChainInputValidator validator;

    bool result = provider.Initialize( tmpfile, validator );
    EXPECT_FALSE( result ) << "Initialize should return false when no chain entries are present";

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

// ─── Test: Multiple observers all notified ──────────────────────────────────

TEST( ChainRpcEndpointProviderTest, MultipleObserversAllNotified )
{
    std::string entry = MakeConfigJsonEntry( "ethereum-sepolia",
                                             11155111,
                                             "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" );

    auto tmpfile = WriteTempConfigJson( MakeConfigJson( entry ) );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    RecordingObserver         recorder1;
    RecordingObserver         recorder2;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    PublicChainInputValidator validator;

    provider.AddObserver( recorder1 );
    provider.AddObserver( recorder2 );
    bool result = provider.Initialize( tmpfile, validator );

    EXPECT_TRUE( result );
    EXPECT_TRUE( recorder1.was_called );
    EXPECT_TRUE( recorder2.was_called );
    ASSERT_EQ( recorder1.received_chains.size(), 1u );
    ASSERT_EQ( recorder2.received_chains.size(), 1u );
    EXPECT_EQ( recorder1.received_chains[0].chain_id, 11155111u );
    EXPECT_EQ( recorder2.received_chains[0].chain_id, 11155111u );

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

// ─── Test: Initialize succeeds with no observers registered ──────────────────

TEST( ChainRpcEndpointProviderTest, SucceedsWithNoObservers )
{
    std::string entry = MakeConfigJsonEntry( "ethereum-sepolia",
                                             11155111,
                                             "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" );

    auto tmpfile = WriteTempConfigJson( MakeConfigJson( entry ) );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    PublicChainInputValidator validator;

    // No AddObserver call — should still succeed without crashing
    bool result = provider.Initialize( tmpfile, validator );
    EXPECT_TRUE( result );

    // Validator should still have been registered
    auto url = validator.GetFirstRpcUrl( "11155111" );
    EXPECT_TRUE( url.has_value() );

    // Cleanup
    std::error_code ec;
    fs::remove( tmpfile, ec );
}

// ─── Test: a cancelled (stale) init must not publish anything ───────────────

TEST( ChainRpcEndpointProviderTest, CancelledInitDoesNotRegisterOrNotify )
{
    // Models an account switch mid-fetch: Initialize() runs the (canned) fetch,
    // then the cancellation predicate returns true, so it must abort BEFORE
    // registering the validator (raw pointer in the global IInputValidator
    // registry) or notifying observers.
    std::string entry = MakeConfigJsonEntry( "ethereum-sepolia",
                                             11155111,
                                             "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" );
    auto tmpfile = WriteTempConfigJson( MakeConfigJson( entry ) );
    ASSERT_TRUE( fs::exists( tmpfile ) );

    RecordingObserver         recorder;
    ChainRpcEndpointProvider  provider;
    provider.SetChainlistFetcher( CannedChainlistJson );
    provider.AddObserver( recorder );

    PublicChainInputValidator validator;
    bool result = provider.Initialize( tmpfile, validator, []() { return true; } );

    EXPECT_FALSE( result ) << "A cancelled init must return false";
    EXPECT_FALSE( recorder.was_called ) << "A cancelled init must not notify observers";
    EXPECT_FALSE( validator.GetFirstRpcUrl( "11155111" ).has_value() )
        << "A cancelled init must not wire/register any endpoint";

    std::error_code ec;
    fs::remove( tmpfile, ec );
}

// ─── Tests: synchronized, insert-only validator registration ──────────────

TEST( ChainRpcEndpointProviderTest, ExistingValidatorMustBeRemovedBeforeReplacementCanRegister )
{
    const std::string cid = "7777777";

    auto a = std::make_unique<PublicChainInputValidator>();
    ASSERT_TRUE( a->RegisterForChain( cid ) );
    ASSERT_EQ( IInputValidator::Get( cid ), static_cast<const IInputValidator *>( a.get() ) );

    auto b = std::make_unique<PublicChainInputValidator>();
    EXPECT_FALSE( b->RegisterForChain( cid ) );
    EXPECT_EQ( IInputValidator::Get( cid ), static_cast<const IInputValidator *>( a.get() ) )
        << "A second registration must not overwrite the current owner";
    IInputValidator::UnregisterIf( cid, b.get() );
    EXPECT_EQ( IInputValidator::Get( cid ), static_cast<const IInputValidator *>( a.get() ) )
        << "A non-owner must not remove the current registration";

    a.reset();
    ASSERT_EQ( IInputValidator::Get( cid ), nullptr );

    ASSERT_TRUE( b->RegisterForChain( cid ) );
    EXPECT_EQ( IInputValidator::Get( cid ), static_cast<const IInputValidator *>( b.get() ) )
        << "A new validator may claim the chain after the old owner unregisters";

    b.reset();
    EXPECT_EQ( IInputValidator::Get( cid ), nullptr )
        << "A validator must self-deregister on destruction (no dangling pointer)";
}

TEST( ChainRpcEndpointProviderTest, ConcurrentRegistrationSelectsExactlyOneOwner )
{
    const std::string cid          = "8888888";
    constexpr size_t  kContenders = 16u;

    std::vector<std::unique_ptr<PublicChainInputValidator>> validators;
    validators.reserve( kContenders );
    for ( size_t i = 0u; i < kContenders; ++i )
    {
        validators.push_back( std::make_unique<PublicChainInputValidator>() );
    }

    std::atomic<size_t> ready{ 0u };
    std::atomic<size_t> registered{ 0u };
    std::atomic<bool>   start{ false };
    std::vector<std::thread> contenders;
    contenders.reserve( kContenders );

    for ( size_t i = 0u; i < kContenders; ++i )
    {
        contenders.emplace_back(
            [&, i]()
            {
                ready.fetch_add( 1u, std::memory_order_release );
                while ( !start.load( std::memory_order_acquire ) )
                {
                    std::this_thread::yield();
                }
                if ( validators[i]->RegisterForChain( cid ) )
                {
                    registered.fetch_add( 1u, std::memory_order_relaxed );
                }
            } );
    }

    while ( ready.load( std::memory_order_acquire ) != kContenders )
    {
        std::this_thread::yield();
    }
    start.store( true, std::memory_order_release );

    for ( auto &contender : contenders )
    {
        contender.join();
    }

    EXPECT_EQ( registered.load( std::memory_order_relaxed ), 1u );
    const auto *owner = IInputValidator::Get( cid );
    ASSERT_NE( owner, nullptr );

    size_t owner_matches = 0u;
    for ( const auto &validator : validators )
    {
        owner_matches += ( owner == validator.get() ) ? 1u : 0u;
    }
    EXPECT_EQ( owner_matches, 1u );

    validators.clear();
    EXPECT_EQ( IInputValidator::Get( cid ), nullptr );
}
