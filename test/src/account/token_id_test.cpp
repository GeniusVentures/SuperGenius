/**
 * @file       token_id_test.cpp
 * @brief      Unit tests for TokenID conversion helpers.
 * @date       2026-06-03
 */
#include <gtest/gtest.h>

#include "account/TokenID.hpp"
#include "rlp/intx.hpp"

using namespace sgns;

namespace
{
    /**
     * @brief Builds a uint256 value with all four 64-bit words populated.
     * @return A deterministic 256-bit test value.
     */
    intx::uint256 MakeFullWidthTokenValue()
    {
        return ( intx::uint256( 0x0102030405060708ULL ) << 192 ) | ( intx::uint256( 0x1112131415161718ULL ) << 128 ) |
               ( intx::uint256( 0x2122232425262728ULL ) << 64 ) | intx::uint256( 0x3132333435363738ULL );
    }

    const TokenID::ByteArray kFullWidthBigEndian{ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x11, 0x12, 0x13,
                                                  0x14, 0x15, 0x16, 0x17, 0x18, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                                  0x27, 0x28, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };

    const TokenID::ByteArray kFullWidthLittleEndian{ 0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x28, 0x27, 0x26,
                                                     0x25, 0x24, 0x23, 0x22, 0x21, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13,
                                                     0x12, 0x11, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 };
} // namespace

/**
 * @given A small uint256 token value.
 * @when It is converted to TokenID.
 * @then The value is stored as a 32-byte big-endian identifier.
 */
TEST( TokenIDTest, FromUint256StoresSmallValuesAsBigEndian )
{
    const TokenID token_id = TokenID::FromUint256( intx::uint256( 0x123456789ABCDEF0ULL ), TokenID::Endianness::BIG );

    ASSERT_EQ( token_id.size(), 32U );
    const auto &bytes = token_id.bytes();
    for ( size_t i = 0; i < 24; ++i )
    {
        EXPECT_EQ( bytes[i], 0x00 );
    }

    EXPECT_EQ( bytes[24], 0x12 );
    EXPECT_EQ( bytes[25], 0x34 );
    EXPECT_EQ( bytes[26], 0x56 );
    EXPECT_EQ( bytes[27], 0x78 );
    EXPECT_EQ( bytes[28], 0x9A );
    EXPECT_EQ( bytes[29], 0xBC );
    EXPECT_EQ( bytes[30], 0xDE );
    EXPECT_EQ( bytes[31], 0xF0 );
}

/**
 * @given A zero uint256 token value.
 * @when It is converted to TokenID.
 * @then The TokenID is valid and GNUS-equivalent.
 */
TEST( TokenIDTest, FromUint256ZeroIsValidGnusEquivalent )
{
    const TokenID token_id = TokenID::FromUint256( intx::uint256( 0 ), TokenID::Endianness::BIG );

    EXPECT_EQ( token_id.size(), 32U );
    EXPECT_TRUE( token_id.IsGNUS() );
    for ( uint8_t byte : token_id.bytes() )
    {
        EXPECT_EQ( byte, 0x00 );
    }
}

/**
 * @given A uint256 token value with high and low words populated.
 * @when It is converted to TokenID.
 * @then Word order and byte order are both canonical big-endian.
 */
TEST( TokenIDTest, FromUint256StoresHighWordsInCanonicalOrder )
{
    const intx::uint256 value = MakeFullWidthTokenValue();

    const TokenID token_id = TokenID::FromUint256( value, TokenID::Endianness::BIG );

    EXPECT_EQ( token_id.size(), 32U );
    EXPECT_EQ( token_id.bytes(), kFullWidthBigEndian );
}

/**
 * @given A uint256 token value with high and low words populated.
 * @when It is converted to TokenID with little-endian byte order.
 * @then Word order and byte order are both little-endian.
 */
TEST( TokenIDTest, FromUint256StoresHighWordsInLittleEndianOrder )
{
    const intx::uint256 value = MakeFullWidthTokenValue();

    const TokenID token_id = TokenID::FromUint256( value, TokenID::Endianness::LITTLE );

    EXPECT_EQ( token_id.size(), 32U );
    EXPECT_EQ( token_id.bytes(), kFullWidthLittleEndian );
}
