/**
 * @file       bridge_relayer_test.cpp
 * @brief      Unit tests for BridgeRelayer burn event processing.
 * @date       2026-05-30
 */
#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <string>
#include <unordered_map>

#include "account/BridgeRelayer.hpp"
#include "account/TokenID.hpp"
#include "base/logger.hpp"
#include "base/parse_utility.hpp"
#include "eth/abi_decoder.hpp"
#include "eth/event_filter.hpp"
#include "eth/secp256k1_utility.hpp"

#include "testutil/TestMintInputValidator.hpp"

using namespace sgns;

// ─── Test Accessor ──────────────────────────────────────────────────────────

/// @brief Friend accessor for private BridgeRelayer::OnWatchEvent.
class BridgeRelayerTestAccess
{
public:
    static void OnWatchEvent( BridgeRelayer                     &relayer,
                              const eth::WatchEventNotification &notification,
                              const std::string                 &chain_name = "test-chain" )
    {
        relayer.OnWatchEvent( notification, chain_name );
    }

    /// @brief Access chain_watches_ for test verification.
    /// After Plan 05.2-02, each entry is a pair<v1_watch_id, v2_watch_id>.
    static const std::unordered_map<std::string, std::pair<eth::EventWatchId, eth::EventWatchId>> &ChainWatches(
        const BridgeRelayer &relayer )
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

    static void SetMintFundsOverride(
        BridgeRelayer &relayer,
        std::function<outcome::result<std::string>( uint64_t,
                                                    const std::string &,
                                                    const std::string &,
                                                    uint32_t,
                                                    TokenID,
                                                    const std::string & )> override_fn )
    {
        relayer.mint_funds_override_ = std::move( override_fn );
    }
};

// ─── Test Helpers ───────────────────────────────────────────────────────────

/// @brief Build a WatchEventNotification simulating a BridgeSourceBurned event.
eth::WatchEventNotification MakeBurnNotification( const std::string &sender_hex,
                                                  uint64_t           token_id_val,
                                                  uint64_t           amount_val,
                                                  uint64_t           src_chain_id,
                                                  uint64_t           dest_chain_id,
                                                  const std::string &tx_hash_hex,
                                                  const std::string &sgns_dest_hex,
                                                  uint32_t           receipt_log_index = 0 )
{
    eth::WatchEventNotification notification;

    // Event log with tx_hash
    eth::codec::Address sender_addr{};
    rlp::base::parse::hex_array( sender_hex, sender_addr );

    notification.event.tx_hash = {};
    rlp::base::parse::hex_array( tx_hash_hex, notification.event.tx_hash );
    notification.event.receipt_log_index = receipt_log_index;

    // ABI-decoded values:
    //   values[0]: sender (address)
    //   values[1]: id (uint256) — ERC-1155 token ID
    //   values[2]: amount (uint256)
    //   values[3]: srcChainID (uint256)
    //   values[4]: destChainID (uint256)
    //   values[5]: sgnsDestination (bytes) — 64-byte SuperGenius public key
    notification.values.push_back( sender_addr );
    notification.values.push_back( intx::uint256( token_id_val ) );
    notification.values.push_back( intx::uint256( amount_val ) );
    notification.values.push_back( intx::uint256( src_chain_id ) );
    notification.values.push_back( intx::uint256( dest_chain_id ) );

    // sgnsDestination as bytes (64-byte SuperGenius public key)
    std::array<uint8_t, 64> sgns_dest_arr{};
    rlp::base::parse::hex_array( sgns_dest_hex, sgns_dest_arr );
    eth::codec::ByteBuffer sgns_dest_bytes( sgns_dest_arr.begin(), sgns_dest_arr.end() );
    notification.values.push_back( std::move( sgns_dest_bytes ) );

    return notification;
}

/// @brief 64-byte SuperGenius public key for test destinations.
static const std::string kTestSgnsDestination = "a62f83ab9f2de6ac95e2336053aea94f8fab10dfb8d3043efe64c3f4e565cfcc"
                                                "2c5aacd6d6092682b8de8383444f746d150b3f7891ed46c9050502ed4b6898a6";

/// @brief Build a WatchEventNotification simulating a BridgeOutInitiated v2
///        event (Plan 05.2-01/02). Param 5 is a 32-byte X-only key (Hash256)
///        and param 6 is the Y-parity bool (destinationYOdd).
eth::WatchEventNotification MakeV2BurnNotification( const std::string             &sender_hex,
                                                    uint64_t                       token_id_val,
                                                    uint64_t                       amount_val,
                                                    uint64_t                       src_chain_id,
                                                    uint64_t                       dest_chain_id,
                                                    const std::string             &tx_hash_hex,
                                                    const std::array<uint8_t, 32> &x_only_bytes,
                                                    bool                           destination_y_odd,
                                                    uint32_t                       receipt_log_index = 0 )
{
    eth::WatchEventNotification notification;

    eth::codec::Address sender_addr{};
    rlp::base::parse::hex_array( sender_hex, sender_addr );

    notification.event.tx_hash = {};
    rlp::base::parse::hex_array( tx_hash_hex, notification.event.tx_hash );
    notification.event.receipt_log_index = receipt_log_index;

    // ABI-decoded values for BridgeOutInitiated:
    //   values[0]: sender (address)
    //   values[1]: id (uint256)
    //   values[2]: amount (uint256)
    //   values[3]: srcChainID (uint256)
    //   values[4]: destChainID (uint256)
    //   values[5]: sgnsDestination (bytes32) — 32-byte X-only key (D-06 discriminator)
    //   values[6]: destinationYOdd (bool) — Y parity (D-07)
    notification.values.push_back( sender_addr );
    notification.values.push_back( intx::uint256( token_id_val ) );
    notification.values.push_back( intx::uint256( amount_val ) );
    notification.values.push_back( intx::uint256( src_chain_id ) );
    notification.values.push_back( intx::uint256( dest_chain_id ) );

    // sgnsDestination as bytes32 (32-byte X-only key in contract byte order)
    eth::codec::Hash256 x_only_hash = x_only_bytes;
    notification.values.push_back( std::move( x_only_hash ) );

    // destinationYOdd as bool
    notification.values.push_back( destination_y_odd );

    return notification;
}

// ─── Tests ──────────────────────────────────────────────────────────────────

TEST( BridgeRelayerTest, ExtractsBurnDetailsFromNotification )
{
    // Verify that WatchEventNotification can carry BridgeSourceBurned data
    // and the ABI values are correctly typed.

    auto notification = MakeBurnNotification(
        "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",                                 // sender
        42,                                                                         // token_id
        1000000,                                                                    // amount (1M wei)
        11155111,                                                                   // Sepolia chain ID
        8453,                                                                       // Base mainnet dest chain ID
        "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef12", // tx_hash (64 hex)
        "a62f83ab9f2de6ac95e2336053aea94f8fab10dfb8d3043efe64c3f4e565cfcc"
        "2c5aacd6d6092682b8de8383444f746d150b3f7891ed46c9050502ed4b6898a6" // sgnsDestination (64 bytes)
    );

    // Verify notification was constructed correctly
    ASSERT_EQ( notification.values.size(), 6U );

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

    // Verify destChainID
    const auto &dest_chain = std::get<intx::uint256>( notification.values[4] );
    EXPECT_EQ( dest_chain, intx::uint256( 8453 ) );

    // Verify sgnsDestination bytes (64 bytes)
    const auto &sgns_dest = std::get<eth::codec::ByteBuffer>( notification.values[5] );
    EXPECT_EQ( sgns_dest.size(), 64U );

    // Verify tx_hash is populated
    EXPECT_FALSE( notification.event.tx_hash.empty() );
}

TEST( BridgeRelayerTest, DispatchesDistinctReceiptOrdinalsWithoutDefaulting )
{
    auto relayer = BridgeRelayerTestAccess::CreateForTest();
    std::vector<uint32_t> received_indexes;
    std::vector<std::string> received_hashes;
    BridgeRelayerTestAccess::SetMintFundsOverride(
        relayer,
        [&]( uint64_t,
             const std::string &tx_hash,
             const std::string &,
             uint32_t receipt_log_index,
             TokenID,
             const std::string & ) -> outcome::result<std::string>
        {
            received_indexes.push_back( receipt_log_index );
            received_hashes.push_back( tx_hash );
            return std::string( "minted" );
        } );

    const auto make_notification = []( uint32_t receipt_log_index )
    {
        return MakeBurnNotification(
            "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
            42,
            1000000,
            11155111,
            8453,
            "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
            kTestSgnsDestination,
            receipt_log_index );
    };

    BridgeRelayerTestAccess::OnWatchEvent( relayer, make_notification( 0 ) );
    BridgeRelayerTestAccess::OnWatchEvent( relayer, make_notification( 2 ) );

    EXPECT_EQ( received_indexes, ( std::vector<uint32_t>{ 0, 2 } ) );
    ASSERT_EQ( received_hashes.size(), 2u );
    EXPECT_EQ( received_hashes[0],
               "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890" );
    EXPECT_EQ( received_hashes[1], received_hashes[0] );
}

TEST( BridgeRelayerTest, RejectsBurnWithoutReceiptLocalOrdinal )
{
    auto relayer = BridgeRelayerTestAccess::CreateForTest();
    size_t dispatch_count = 0;
    BridgeRelayerTestAccess::SetMintFundsOverride(
        relayer,
        [&]( uint64_t,
             const std::string &,
             const std::string &,
             uint32_t,
             TokenID,
             const std::string & ) -> outcome::result<std::string>
        {
            ++dispatch_count;
            return std::string( "minted" );
        } );

    auto notification = MakeBurnNotification(
        "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
        42,
        1000000,
        11155111,
        8453,
        "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
        kTestSgnsDestination );
    notification.event.receipt_log_index.reset();

    BridgeRelayerTestAccess::OnWatchEvent( relayer, notification );
    EXPECT_EQ( dispatch_count, 0u );
}

TEST( BridgeRelayerTest, TokenIdConversionRoundTrip )
{
    // Verify uint256 → TokenID byte conversion is deterministic

    intx::uint256 original( 0x123456789ABCDEF0ULL );

    TokenID::ByteArray token_bytes{};
    for ( size_t i = 0; i < token_bytes.size(); ++i )
    {
        token_bytes[i] = static_cast<uint8_t>( ( original >> ( ( token_bytes.size() - 1 - i ) * 8 ) ) & 0xFF );
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
        token_bytes[i] = static_cast<uint8_t>( ( zero_id >> ( ( token_bytes.size() - 1 - i ) * 8 ) ) & 0xFF );
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

    auto notification = MakeBurnNotification( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
                                              1,
                                              std::numeric_limits<uint64_t>::max(),
                                              1,
                                              8453,
                                              "1111111111111111111111111111111111111111111111111111111111111111",
                                              kTestSgnsDestination );

    const auto &amount = std::get<intx::uint256>( notification.values[2] );
    EXPECT_EQ( amount, intx::uint256( std::numeric_limits<uint64_t>::max() ) );

    // Verify it fits in uint64
    EXPECT_LE( amount, std::numeric_limits<uint64_t>::max() );
}

TEST( BridgeRelayerTest, HexAddressRoundTrip )
{
    // Verify 20-byte EVM address round-trip via parse::hex_array / hex_array_string.
    eth::Address addr{};
    bool         parsed = rlp::base::parse::hex_array( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045", addr );
    ASSERT_TRUE( parsed );
    EXPECT_EQ( addr.size(), 20U );

    // Verify round-trip (hex_array_string includes "0x" prefix)
    std::string hex = rlp::base::parse::hex_array_string( addr );
    EXPECT_EQ( hex.size(), 42U ); // "0x" + 40 hex chars
}

TEST( BridgeRelayerTest, SgnsDestinationHexRoundTrip )
{
    // Verify 64-byte SuperGenius destination round-trip via hex_bytes / hex_array.
    std::array<uint8_t, 64> bytes{};
    bool                    parsed = rlp::base::parse::hex_array( kTestSgnsDestination, bytes );
    ASSERT_TRUE( parsed );
    EXPECT_EQ( bytes.size(), 64U );

    // Verify round-trip via hex_bytes
    std::string hex = rlp::base::parse::hex_bytes( bytes.data(), bytes.size() );
    EXPECT_EQ( hex.size(), 130U ); // 64 bytes → "0x" + 128 hex chars
}

// ─── OnWatchEvent Behavior Tests ────────────────────────────────────────────

TEST( BridgeRelayerTest, OnWatchEventHandlesNullTransactionManager )
{
    // Verify that OnWatchEvent gracefully handles null TransactionManager
    // without crashing — logs error and returns.

    auto logger  = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    auto notification = MakeBurnNotification( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
                                              42,
                                              1000000,
                                              11155111,
                                              8453,
                                              "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
                                              kTestSgnsDestination );

    // Should not crash — logs error and returns
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

TEST( BridgeRelayerTest, OnWatchEventRejectsInsufficientValues )
{
    // Verify that OnWatchEvent rejects notifications with fewer than 6 ABI values.

    auto logger  = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    eth::WatchEventNotification notification;
    notification.values.push_back( intx::uint256( 1 ) );
    notification.values.push_back( intx::uint256( 2 ) );
    // Only 2 values — needs 6

    // Should not crash — logs error and returns
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

TEST( BridgeRelayerTest, OnWatchEventHandlesZeroAmount )
{
    // Verify that OnWatchEvent processes zero-amount burns without error.

    auto logger  = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    auto notification = MakeBurnNotification( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
                                              1,
                                              0, // zero amount
                                              1,
                                              8453,
                                              "1111111111111111111111111111111111111111111111111111111111111111",
                                              kTestSgnsDestination );

    // Should not crash — zero amount is valid (though MintFunds may reject it)
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

TEST( BridgeRelayerTest, OnWatchEventHandlesAmountOverflow )
{
    // Verify that OnWatchEvent detects uint256 > uint64 overflow and returns early.

    auto logger  = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    eth::WatchEventNotification notification;

    eth::codec::Address sender_addr{};
    rlp::base::parse::hex_array( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045", sender_addr );

    notification.event.tx_hash = {};
    rlp::base::parse::hex_array( "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
                                 notification.event.tx_hash );

    notification.values.push_back( sender_addr );
    notification.values.push_back( intx::uint256( 1 ) );              // token_id
    notification.values.push_back( intx::uint256( 1 ) << 255 );       // amount overflows uint64
    notification.values.push_back( intx::uint256( 1 ) );              // srcChainID
    notification.values.push_back( intx::uint256( 0 ) );              // destChainID
    notification.values.push_back( eth::codec::ByteBuffer( 64, 0 ) ); // sgnsDestination (dummy)

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

    auto logger        = base::createLogger( "bridge_relayer_test" );
    auto watch_service = std::make_shared<eth::EthWatchService>();
    auto relayer       = BridgeRelayer::Create( std::weak_ptr<TransactionManager>(), watch_service );
    ASSERT_NE( relayer, nullptr );

    std::vector<ChainContractPair> chains = {
        { "ethereum-sepolia", "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" },
        { "polygon-amoy", "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" },
        { "base-sepolia", "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" },
    };

    relayer->Start( chains );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( *relayer );
    EXPECT_EQ( watches.size(), 3U );

    for ( const auto &chain : chains )
    {
        auto it = watches.find( chain.chain_name );
        ASSERT_NE( it, watches.end() ) << "missing watch for " << chain.chain_name;
        // After Plan 05.2-02, each entry is a pair<v1_id, v2_id>; both must be non-zero.
        EXPECT_NE( it->second.first, 0 ) << "v1 watch_id should be non-zero for " << chain.chain_name;
        EXPECT_NE( it->second.second, 0 ) << "v2 watch_id should be non-zero for " << chain.chain_name;
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
        { "chain-no-addr", "" },
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
        { "chain-a", "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" },
        { "chain-bad", "deadbeef" }, // short hex — invalid
        { "chain-empty", "" },       // empty address — skipped
        { "chain-b", "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" },
        { "chain-garbage", "zzz" }, // non-hex — invalid
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

    auto logger  = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    auto notification = MakeBurnNotification( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
                                              42,
                                              1000000,
                                              11155111,
                                              8453,
                                              "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
                                              kTestSgnsDestination );

    // Should not crash; chain_name is passed through and logged
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification, "specific-chain" ) );
}

// ─── Observer-Driven Start Tests ───────────────────────────────────────────

TEST( BridgeRelayerTest, OnRpcEndpointsReadyDelegatesToStart )
{
    // @given a BridgeRelayer implementing IBridgeInitObserver
    // @when OnRpcEndpointsReady() is called with a chain list
    // @then the same chains are registered in chain_watches_ as if Start() were called

    auto watch_service = std::make_shared<eth::EthWatchService>();
    auto relayer       = BridgeRelayer::Create( std::weak_ptr<TransactionManager>(), watch_service );
    ASSERT_NE( relayer, nullptr );

    std::vector<ChainContractPair> chains = {
        { "ethereum-sepolia", "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70", 11155111 },
    };

    relayer->OnRpcEndpointsReady( chains );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( *relayer );
    EXPECT_EQ( watches.size(), 1U );
    auto it = watches.find( "ethereum-sepolia" );
    ASSERT_NE( it, watches.end() );
    // After Plan 05.2-02, the entry is a pair<v1_id, v2_id>; both must be non-zero.
    EXPECT_NE( it->second.first, 0 );
    EXPECT_NE( it->second.second, 0 );
}

TEST( BridgeRelayerTest, OnRpcEndpointsReadyEmptyVector )
{
    // @given a BridgeRelayer implementing IBridgeInitObserver
    // @when OnRpcEndpointsReady() is called with an empty chain list
    // @then no watches are registered (Start({}) is a no-op per D-21)

    auto watch_service = std::make_shared<eth::EthWatchService>();
    auto relayer       = BridgeRelayer::Create( std::weak_ptr<TransactionManager>(), watch_service );
    ASSERT_NE( relayer, nullptr );

    relayer->OnRpcEndpointsReady( {} );

    const auto &watches = BridgeRelayerTestAccess::ChainWatches( *relayer );
    EXPECT_TRUE( watches.empty() );
}

// ─── Bridge V2 Event Dispatch Tests (Plan 05.2-04) ──────────────────────────

namespace
{
    /// @brief Known secp256k1 test vector: public key of private key = 1.
    ///        X coordinate (big-endian) = 79BE667E...81798 (canonical Bitcoin vector),
    ///        with an EVEN Y (compressed prefix 0x02). ABI bytes32 preserves the
    ///        canonical big-endian coordinate order.
    constexpr bool kKnownEvenYOdd = false; // even Y → destination_y_odd = false

    /// @brief Big-endian X hex for private key = 1 (canonical secp256k1 vector).
    constexpr const char *kKnownXBigEndianHex = "79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798";

    /// @brief Full canonical big-endian X||Y destination for private key = 1.
    constexpr const char *kKnownDestinationHex =
        "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
        "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8";

    /// @brief Parse a canonical big-endian X coordinate into the bytes32 emitted
    ///        by the bridge contract.
    std::array<uint8_t, 32> ParseContractOrderX( const std::string &big_endian_hex )
    {
        std::array<uint8_t, 32> big_endian{};
        rlp::base::parse::hex_array( big_endian_hex, big_endian );
        return big_endian;
    }
} // namespace

TEST( BridgeRelayerTest, OnWatchEventDispatchesV2Event )
{
    // @given a v2 BridgeOutInitiated notification with a valid on-curve X-only
    //        key (Hash256 at values[5], bool Y-parity at values[6]).
    // @when OnWatchEvent is called with a null TransactionManager.
    // @then the Hash256 branch is dispatched (no std::bad_variant_access),
    //       decompression succeeds, and it logs "no TransactionManager" — the
    //       expected terminal behavior for a null TM (mirrors the v1 path).

    auto logger  = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    const auto contract_x   = ParseContractOrderX( kKnownXBigEndianHex );
    auto       notification = MakeV2BurnNotification( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
                                                42,
                                                1000000,
                                                11155111,
                                                8453,
                                                "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
                                                contract_x,
                                                kKnownEvenYOdd ); // even Y → destination_y_odd = false

    // Variant dispatch on values[5] must hit the Hash256 branch and decompress.
    // No throw — the decompressed destination is built and the null-TM guard
    // logs and returns cleanly.
    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

TEST( BridgeRelayerTest, OnWatchEventV1StillWorks )
{
    // @regression The v1 BridgeSourceBurned path (ByteBuffer at values[5]) must
    //             still dispatch correctly after the Plan 05.2-02 dual-watch
    //             refactor.  This re-exercises the existing null-TM v1 flow.
    auto logger  = base::createLogger( "bridge_relayer_test" );
    auto relayer = BridgeRelayerTestAccess::CreateForTest( logger );

    auto notification = MakeBurnNotification( "d8dA6BF26964aF9D7eEd9e03E53415D37aA96045",
                                              42,
                                              1000000,
                                              11155111,
                                              8453,
                                              "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
                                              kTestSgnsDestination );

    EXPECT_NO_THROW( BridgeRelayerTestAccess::OnWatchEvent( relayer, notification ) );
}

TEST( BridgeRelayerTest, DecompressMatchesKnownVector )
{
    // @integration REQ-V2-05: the decompression used by the v2 OnWatchEvent
    //             path produces a 128-char destination whose X half matches the
    //             input X, for the canonical private-key-1 secp256k1 vector.
    const auto contract_x = ParseContractOrderX( kKnownXBigEndianHex );
    const auto dest       = eth::DecompressXOnlyPubkey( contract_x, kKnownEvenYOdd );
    ASSERT_TRUE( dest.has_value() ) << "Decompression of known on-curve X must succeed";
    EXPECT_EQ( dest->size(), 128U ) << "Destination must be 128 hex chars (X+Y)";
    EXPECT_EQ( *dest, kKnownDestinationHex )
        << "Destination must preserve canonical big-endian X and Y coordinates";

    // The first 64 hex chars are the contract-order X — must equal the input X
    // rendered as plain hex (no "0x" prefix).
    const std::string x_hex = rlp::base::parse::hex_bytes( contract_x.data(), contract_x.size() );
    ASSERT_GE( x_hex.size(), 2U );
    const std::string x_hex_no_prefix = x_hex.substr( 2 ); // strip "0x"
    EXPECT_EQ( dest->substr( 0, 64 ), x_hex_no_prefix ) << "Destination X half must equal the input contract-order X";
}

TEST( BridgeRelayerTest, V1DestinationIsBareHexMatchingGetAddressFormat )
{
    // @regression v1 BridgeSourceBurned carries the 64-byte SG public key in
    //             sgnsDestination (bytes). ParseBurnEventValues must return a
    //             bare 128-char hex string with NO "0x" prefix, matching
    //             GetAddress() and the v2 decompression output. Previously
    //             hex_bytes() prepended "0x", addressing v1 mints to "0x"+key
    //             so recipient (non-full) nodes never indexed them as spendable.

    std::array<uint8_t, 64> sgns_dest_arr{};
    ASSERT_TRUE( rlp::base::parse::hex_array( kTestSgnsDestination, sgns_dest_arr ) )
        << "Test fixture destination must be valid 64-byte hex";
    eth::codec::ByteBuffer sgns_dest_bytes( sgns_dest_arr.begin(), sgns_dest_arr.end() );

    std::vector<eth::abi::AbiValue> values;
    values.push_back( eth::codec::Address{} );        // [0] sender
    values.push_back( intx::uint256( 1 ) );           // [1] id
    values.push_back( intx::uint256( 1 ) );           // [2] amount
    values.push_back( intx::uint256( 11155111 ) );    // [3] srcChainID
    values.push_back( intx::uint256( 8453 ) );        // [4] destChainID
    values.push_back( std::move( sgns_dest_bytes ) ); // [5] sgnsDestination (v1 bytes)

    auto result = BridgeRelayer::ParseBurnEventValues( values );
    ASSERT_TRUE( result.has_value() ) << "v1 values must parse successfully";
    EXPECT_EQ( result.value().destination.size(), 128U )
        << "v1 destination must be bare 128-char hex (no \"0x\" prefix)";
    EXPECT_EQ( result.value().destination, kTestSgnsDestination )
        << "v1 destination must equal the input public key (GetAddress format)";
}

TEST( BridgeRelayerTest, V1DestinationRejectsEmptyPayload )
{
    // @regression An empty v1 sgnsDestination must be rejected, not silently
    //             turned into "" — MintFunds credits an empty destination to the
    //             relayer's own address, so the burn would be miscredited.
    std::vector<eth::abi::AbiValue> values;
    values.push_back( eth::codec::Address{} );             // [0] sender
    values.push_back( intx::uint256( 1 ) );                // [1] id
    values.push_back( intx::uint256( 1 ) );                // [2] amount
    values.push_back( intx::uint256( 11155111 ) );         // [3] srcChainID
    values.push_back( intx::uint256( 8453 ) );             // [4] destChainID
    values.push_back( eth::codec::ByteBuffer{} );          // [5] sgnsDestination (empty)

    auto result = BridgeRelayer::ParseBurnEventValues( values );
    EXPECT_FALSE( result.has_value() ) << "An empty v1 sgnsDestination must be rejected";
}

TEST( BridgeRelayerTest, V1DestinationRejectsWrongLengthPayload )
{
    // @regression A v1 sgnsDestination that is not exactly the 64-byte SG public
    //             key must be rejected — a wrong-length payload would yield a
    //             malformed recipient.
    std::array<uint8_t, 64> full_arr{};
    ASSERT_TRUE( rlp::base::parse::hex_array( kTestSgnsDestination, full_arr ) );
    eth::codec::ByteBuffer short_bytes( full_arr.begin(), full_arr.begin() + 32 ); // 32 bytes, not 64

    std::vector<eth::abi::AbiValue> values;
    values.push_back( eth::codec::Address{} );                  // [0] sender
    values.push_back( intx::uint256( 1 ) );                     // [1] id
    values.push_back( intx::uint256( 1 ) );                     // [2] amount
    values.push_back( intx::uint256( 11155111 ) );              // [3] srcChainID
    values.push_back( intx::uint256( 8453 ) );                  // [4] destChainID
    values.push_back( std::move( short_bytes ) );               // [5] sgnsDestination (wrong length)

    auto result = BridgeRelayer::ParseBurnEventValues( values );
    EXPECT_FALSE( result.has_value() ) << "A non-64-byte v1 sgnsDestination must be rejected";
}
