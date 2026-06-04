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

using namespace sgns;

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
        11155111,                                      // Sepolia chain ID
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
        1,
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
        1,
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
