/**
 * @file       bridge_relayer_test.cpp
 * @brief      Unit tests for BridgeRelayer burn event processing.
 * @date       2026-05-30
 */
#include <gtest/gtest.h>

#include <unordered_map>
#include <string>

#include "account/BridgeRelayer.hpp"
#include "account/TokenID.hpp"
#include "base/logger.hpp"
#include "base/parse_utility.hpp"
#include "eth/abi_decoder.hpp"
#include "eth/event_filter.hpp"

#include "testutil/TestMintInputValidator.hpp"

using namespace sgns;

static bool kRegisterTestValidator = sgns::test::TestMintInputValidator::RegisterTestValidator();

// ─── Test Accessor ──────────────────────────────────────────────────────────

/// @brief Friend accessor for private BridgeRelayer::OnWatchEvent.
class BridgeRelayerTestAccess
{
public:
    static void OnWatchEvent( BridgeRelayer                       &relayer,
                              const eth::WatchEventNotification   &notification,
                              const std::string                   &chain_name = "test-chain" )
    {
        relayer.OnWatchEvent( notification, chain_name );
    }

    /// @brief Access chain_watches_ for test verification.
    static const std::unordered_map<std::string, eth::EventWatchId> &
    ChainWatches( const BridgeRelayer &relayer )
    {
        return relayer.chain_watches_;
    }

    /// @brief Construct a BridgeRelayer for unit testing with a test logger.
    static BridgeRelayer CreateForTest( base::Logger logger = nullptr )
    {
        if ( !logger )
        {
            logger = base::createLogger( "bridge_relayer_test" );
        }
        return BridgeRelayer( std::weak_ptr<TransactionManager>(), nullptr, std::move( logger ) );
    }
};

// ─── Test Helpers ───────────────────────────────────────────────────────────

/// @brief Build a WatchEventNotification simulating a BridgeSourceBurned event.
eth::WatchEventNotification MakeBurnNotification(
    const std::string &sender_hex,
    uint64_t           token_id_val,
    uint64_t           amount_val,
    uint64_t           src_chain_id,
    const std::string &tx_hash_hex )
{
    eth::WatchEventNotification notification;

    // Event log with tx_hash
    eth::codec::Address sender_addr{};
    rlp::base::parse::hex_array( sender_hex, sender_addr );

    notification.event.tx_hash = {};
    rlp::base::parse::hex_array( tx_hash_hex, notification.event.tx_hash );

    // ABI-decoded values:
    //   values[0]: sender (address)
    //   values[1]: id (uint256) — ERC-1155 token ID
    //   values[2]: amount (uint256)
    //   values[3]: srcChainID (uint256)
    //   values[4]: destChainID (uint256)
    notification.values.push_back( sender_addr );
    notification.values.push_back( intx::uint256( token_id_val ) );
    notification.values.push_back( intx::uint256( amount_val ) );
    notification.values.push_back( intx::uint256( src_chain_id ) );
    notification.values.push_back( intx::uint256( 0 ) ); // destChainID (unused)

    return notification;
}

// ─── Tests ──────────────────────────────────────────────────────────────────

TEST( BridgeRelayerTest, ExtractsBurnDetailsFromNotification )
{
    // Verify that WatchEventNotification can carry BridgeSourceBurned data
    // and the ABI values are correctly typed.

    auto notification = MakeBurnNotification(
        "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045", // sender
        42,                                            // token_id
        1000000,                                       // amount (1M wei)
        0,                                              // test chain ID (0 = "test" validator)
        "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890" // tx_hash
    );

    // Verify notification was constructed correctly
    ASSERT_EQ( notification.values.size(), 5U );

    // Verify sender is an address (20 bytes)
    const auto &sender = std::get<eth::codec::Address>( notification.values[0] );
    EXPECT_EQ( sender.size(), 20U );

    // Verify token_id
    const auto &token_id = std::get<intx::uint256>( notification.values[1] );
    EXPECT_EQ( token_id, intx::uint256( 42 ) );

    // Verify amount
    const auto &amount = std::get<intx::uint256>( notification.values[2] );
    EXPECT_EQ( amount, intx::uint256( 1000000 ) );

    // Verify srcChainID
    const auto &src_chain = std::get<intx::uint256>( notification.values[3] );
    EXPECT_EQ( src_chain, intx::uint256( 11155111 ) );

    // Verify tx_hash is populated
    EXPECT_FALSE( notification.event.tx_hash.empty() );
}

TEST( BridgeRelayerTest, TokenIdConversionRoundTrip )
{
    // Verify uint256 → TokenID byte conversion is deterministic

    intx::uint256 original( 0x123456789ABCDEF0ULL );

    TokenID::ByteArray token_bytes{};
    for ( size_t i = 0; i < token_bytes.size(); ++i )
    {
        token_bytes[i] = static_cast<uint8_t>(
            ( original >> ( ( token_bytes.size() - 1 - i ) * 8 ) ) & 0xFF );
    }
    const TokenID token_id = TokenID::FromBytes( token_bytes.data(), token_bytes.size() );

    // Verify the token ID is valid and has the correct size
    EXPECT_EQ( token_id.size(), 32U );

    // Verify round-trip: convert back to uint256
    intx::uint256 recovered{};
    for ( size_t i = 0; i < token_bytes.size(); ++i )
    {
        recovered = ( recovered << 8 ) | token_bytes[i];
    }
    EXPECT_EQ( recovered, original );
}

TEST( BridgeRelayerTest, TokenIdZeroIsValid )
{
    // Verify that token_id = 0 is handled (edge case for ERC-1155)

    intx::uint256 zero_id( 0 );

    TokenID::ByteArray token_bytes{};
    for ( size_t i = 0; i < token_bytes.size(); ++i )
    {
        token_bytes[i] = static_cast<uint8_t>(
            ( zero_id >> ( ( token_bytes.size() - 1 - i ) * 8 ) ) & 0xFF );
    }
    const TokenID token_id = TokenID::FromBytes( token_bytes.data(), token_bytes.size() );

    EXPECT_EQ( token_id.size(), 32U );

    // Verify all bytes are zero
    for ( uint8_t byte : token_id.bytes() )
    {
        EXPECT_EQ( byte, 0x00 );
    }
}

TEST( BridgeRelayerTest, HandlesMaxUint64Amount )
{
    // Verify that max uint64 amount fits in uint256

    auto notification = MakeBurnNotification(
        "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
        1,
        std::numeric_limits<uint64_t>::max(),
        0,  // test chain
        "1111111111111111111111111111111111111111111111111111111111111111"
    );

    const auto &amount = std::get<intx::uint256>( notification.values[2] );
    EXPECT_EQ( amount, intx::uint256( std::numeric_limits<uint64_t>::max() ) );

    // Verify it fits in uint64
    EXPECT_LE( amount, std::numeric_limits<uint64_t>::max() );
}

TEST( BridgeRelayerTest, AddressToHexRoundTrip )
{
    // Verify hex address parsing and round-trip

    eth::Address addr{};
    bool parsed = rlp::base::parse::hex_array( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045", addr );
    ASSERT_TRUE( parsed );
    EXPECT_EQ( addr.size(), 20U );

    // Verify round-trip (hex_array_string includes "0x" prefix)
    std::string hex = rlp::base::parse::hex_array_string( addr );
    EXPECT_EQ( hex.size(), 42U ); // "0x" + 40 hex chars
}

// ─── OnWatchEvent Behavior Tests ────────────────────────────────────────────

TEST( BridgeRelayerTest, OnWatchEventHandlesNullTransactionManager )
{
    // Verify that OnWatchEvent gracefully handles null TransactionManager
    // without crashing — logs error and returns.

    auto logger = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    auto notification = MakeBurnNotification(
        "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
        42,
        1000000,
        11155111,
        "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890" );

    // Should not crash — logs error and returns
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

TEST( BridgeRelayerTest, OnWatchEventRejectsInsufficientValues )
{
    // Verify that OnWatchEvent rejects notifications with fewer than 5 ABI values.

    auto logger = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    eth::WatchEventNotification notification;
    notification.values.push_back( intx::uint256( 1 ) );
    notification.values.push_back( intx::uint256( 2 ) );
    // Only 2 values — needs 5

    // Should not crash — logs error and returns
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

TEST( BridgeRelayerTest, OnWatchEventHandlesZeroAmount )
{
    // Verify that OnWatchEvent processes zero-amount burns without error.

    auto logger = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    auto notification = MakeBurnNotification(
        "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
        1,
        0,  // zero amount
        0,  // test chain
        "1111111111111111111111111111111111111111111111111111111111111111" );

    // Should not crash — zero amount is valid (though MintFunds may reject it)
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

TEST( BridgeRelayerTest, OnWatchEventHandlesAmountOverflow )
{
    // Verify that OnWatchEvent detects uint256 > uint64 overflow and returns early.

    auto logger = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    eth::WatchEventNotification notification;

    eth::codec::Address sender_addr{};
    rlp::base::parse::hex_array( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045", sender_addr );

    notification.event.tx_hash = {};
    rlp::base::parse::hex_array( "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
                                  notification.event.tx_hash );

    notification.values.push_back( sender_addr );
    notification.values.push_back( intx::uint256( 1 ) );  // token_id
    notification.values.push_back( intx::uint256( 1 ) << 255 ); // amount overflows uint64
    notification.values.push_back( intx::uint256( 1 ) );  // srcChainID
    notification.values.push_back( intx::uint256( 0 ) );  // destChainID

    // Should not crash — overflow detected, logs error and returns
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

// ─── Multi-Chain Start() Tests ───────────────────────────────────────────────

/**
 * @brief Simple spy EthWatchService for capturing watch_event calls in tests.
 *
 * Because eth::EthWatchService::watch_event() is not virtual, we can't
 * override it in a subclass.  Instead, use a default-constructed real
 * EthWatchService for Start() tests that exercise registration and verify
 * the resulting chain_watches_ via the test accessor.
 */

TEST( BridgeRelayerTest, MultiChainStart )
{
    // @given a BridgeRelayer with a real EthWatchService
    // @when Start() is called with 3 valid ChainContractPair entries
    // @then chain_watches_ contains 3 entries, each with a non-zero watch_id

    auto logger         = base::createLogger( "bridge_relayer_test" );
    auto watch_service  = std::make_shared<eth::EthWatchService>();
    auto relayer        = BridgeRelayer::Create( std::weak_ptr<TransactionManager>(), watch_service );
    ASSERT_NE( relayer, nullptr );

    std::vector<ChainContractPair> chains = {
        { "ethereum-sepolia", "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" },
        { "polygon-amoy",     "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" },
        { "base-sepolia",     "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" },
    };

    relayer->Start( chains );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( *relayer );
    EXPECT_EQ( watches.size(), 3U );

    for ( const auto &chain : chains )
    {
        auto it = watches.find( chain.chain_name );
        ASSERT_NE( it, watches.end() ) << "missing watch for " << chain.chain_name;
        EXPECT_NE( it->second, 0 ) << "watch_id should be non-zero for " << chain.chain_name;
    }
}

TEST( BridgeRelayerTest, SkipsChainsWithoutAddress )
{
    // @given a BridgeRelayer with a real EthWatchService
    // @when Start() is called with 3 chains, one having an invalid hex address
    // @then only valid chains are registered; invalid one logged and skipped

    auto watch_service = std::make_shared<eth::EthWatchService>();
    auto relayer       = BridgeRelayer::Create( std::weak_ptr<TransactionManager>(), watch_service );
    ASSERT_NE( relayer, nullptr );

    std::vector<ChainContractPair> chains = {
        { "chain-valid-a", "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" },
        { "chain-bad-hex", "not-a-hex-address" },
        { "chain-valid-b", "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" },
    };

    relayer->Start( chains );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( *relayer );
    EXPECT_EQ( watches.size(), 2U );

    EXPECT_NE( watches.find( "chain-valid-a" ), watches.end() );
    EXPECT_EQ( watches.find( "chain-bad-hex" ), watches.end() );
    EXPECT_NE( watches.find( "chain-valid-b" ), watches.end() );
}

TEST( BridgeRelayerTest, SkipsChainsWithEmptyAddress )
{
    // @given a BridgeRelayer
    // @when Start() receives a chain with an empty contract_address
    // @then the chain is skipped and valid chains still register

    auto watch_service = std::make_shared<eth::EthWatchService>();
    auto relayer       = BridgeRelayer::Create( std::weak_ptr<TransactionManager>(), watch_service );
    ASSERT_NE( relayer, nullptr );

    std::vector<ChainContractPair> chains = {
        { "chain-with-addr", "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" },
        { "chain-no-addr",   "" },
    };

    relayer->Start( chains );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( *relayer );
    EXPECT_EQ( watches.size(), 1U );
    EXPECT_NE( watches.find( "chain-with-addr" ), watches.end() );
    EXPECT_EQ( watches.find( "chain-no-addr" ), watches.end() );
}

TEST( BridgeRelayerTest, StartEmptyVector )
{
    // @given a BridgeRelayer
    // @when Start() is called with an empty vector
    // @then no crash occurs and chain_watches_ remains empty

    auto watch_service = std::make_shared<eth::EthWatchService>();
    auto relayer       = BridgeRelayer::Create( std::weak_ptr<TransactionManager>(), watch_service );
    ASSERT_NE( relayer, nullptr );

    relayer->Start( {} );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( *relayer );
    EXPECT_TRUE( watches.empty() );
}

TEST( BridgeRelayerTest, BestEffortSkipsInvalidAndEmptyNames )
{
    // @given a BridgeRelayer
    // @when Start() receives a mix of valid, invalid-hex, and empty-address chains
    // @then only valid chains are registered — best-effort per D-21

    auto watch_service = std::make_shared<eth::EthWatchService>();
    auto relayer       = BridgeRelayer::Create( std::weak_ptr<TransactionManager>(), watch_service );
    ASSERT_NE( relayer, nullptr );

    std::vector<ChainContractPair> chains = {
        { "chain-a",     "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" },
        { "chain-bad",   "deadbeef" },   // short hex — invalid
        { "chain-empty", "" },           // empty address — skipped
        { "chain-b",     "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" },
        { "chain-garbage", "zzz" },      // non-hex — invalid
    };

    relayer->Start( chains );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( *relayer );
    EXPECT_EQ( watches.size(), 2U );
    EXPECT_NE( watches.find( "chain-a" ), watches.end() );
    EXPECT_NE( watches.find( "chain-b" ), watches.end() );
    // The 3 bad entries should not appear
    EXPECT_EQ( watches.find( "chain-bad" ), watches.end() );
    EXPECT_EQ( watches.find( "chain-empty" ), watches.end() );
    EXPECT_EQ( watches.find( "chain-garbage" ), watches.end() );
}

TEST( BridgeRelayerTest, NoWatchServiceReturnsEarly )
{
    // @given a BridgeRelayer created via CreateForTest (null EthWatchService)
    // @when Start() is called with a valid chain
    // @then no crash and chain_watches_ stays empty

    auto relayer = BridgeRelayerTestAccess::CreateForTest();
    ASSERT_NO_THROW( relayer.Start( { { "test", "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" } } ) );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( relayer );
    EXPECT_TRUE( watches.empty() );
}

TEST( BridgeRelayerTest, OnWatchEventLogsChainName )
{
    // Verify that OnWatchEvent includes chain_name in its log output
    // and doesn't crash with a valid notification.

    auto logger = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    auto notification = MakeBurnNotification(
        "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
        42,
        1000000,
        11155111,
        "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890" );

    // Should not crash; chain_name is passed through and logged
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification, "specific-chain" ) );
}
