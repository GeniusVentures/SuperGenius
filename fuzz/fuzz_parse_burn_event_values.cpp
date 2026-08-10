// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// libFuzzer harness for BridgeRelayer::ParseBurnEventValues.
//
// Byte-layout scheme (documented, deterministic — no randomness beyond the
// fuzzer's own input bytes):
//   Fuzzer bytes are consumed by a small cursor. We build a fixed-size
//   vector of kSlotCount (>= 7, to exercise both the kExpectedMinParams=6
//   floor and the kDestinationYOddIndex=6 v2 branch) eth::abi::AbiValue
//   slots. For each slot:
//     1. Read one selector byte (0 if input exhausted). `selector % 6`
//        picks which alternative of the AbiValue variant to construct:
//          0 -> codec::Address   (20 bytes, zero-padded if input runs out)
//          1 -> intx::uint256    (32 bytes, zero-padded)
//          2 -> codec::Hash256   (32 bytes, zero-padded)
//          3 -> bool             (1 byte, non-zero = true)
//          4 -> codec::ByteBuffer (length byte mod 128, then that many bytes;
//                                   this covers both the v1 wrong-length case
//                                   and the exact 64-byte valid case)
//          5 -> std::string      (length byte mod 32, then that many bytes)
//     2. Remaining input bytes are consumed to fill the payload; if input
//        runs out mid-slot, the payload is zero-padded.
//
// ParseBurnEventValues internally range-checks every std::get via
// std::holds_alternative — this harness exists to exercise every branch
// (v1 ByteBuffer wrong-length, v2 missing index 6, v2 present) under ASan.

#include "account/BridgeRelayer.hpp"
#include "eth/abi_decoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
    constexpr size_t kSlotCount = 7;

    class ByteCursor
    {
    public:
        ByteCursor( const uint8_t *data, size_t size ) : data_( data ), size_( size )
        {
        }

        uint8_t NextByte()
        {
            if ( pos_ >= size_ )
            {
                return 0;
            }
            return data_[pos_++];
        }

        std::vector<uint8_t> NextBytes( size_t count )
        {
            std::vector<uint8_t> out( count, 0 );
            for ( size_t i = 0; i < count && pos_ < size_; ++i, ++pos_ )
            {
                out[i] = data_[pos_];
            }
            return out;
        }

    private:
        const uint8_t *data_;
        size_t         size_;
        size_t         pos_ = 0;
    };
} // namespace

extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size )
{
    ByteCursor cursor( data, size );

    std::vector<eth::abi::AbiValue> values;
    values.reserve( kSlotCount );

    for ( size_t slot = 0; slot < kSlotCount; ++slot )
    {
        const uint8_t selector = cursor.NextByte() % 6;

        switch ( selector )
        {
            case 0:
            {
                eth::codec::Address address{};
                auto                bytes = cursor.NextBytes( address.size() );
                std::copy( bytes.begin(), bytes.end(), address.begin() );
                values.emplace_back( address );
                break;
            }
            case 1:
            {
                auto     bytes = cursor.NextBytes( 32 );
                intx::uint256 value = 0;
                for ( auto byte : bytes )
                {
                    value = ( value << 8 ) | byte;
                }
                values.emplace_back( value );
                break;
            }
            case 2:
            {
                eth::codec::Hash256 hash{};
                auto                bytes = cursor.NextBytes( hash.size() );
                std::copy( bytes.begin(), bytes.end(), hash.begin() );
                values.emplace_back( hash );
                break;
            }
            case 3:
            {
                const uint8_t byte = cursor.NextByte();
                values.emplace_back( byte != 0 );
                break;
            }
            case 4:
            {
                const size_t          length = cursor.NextByte() % 128;
                eth::codec::ByteBuffer buffer = cursor.NextBytes( length );
                values.emplace_back( buffer );
                break;
            }
            case 5:
            default:
            {
                const size_t          length = cursor.NextByte() % 32;
                std::vector<uint8_t>  raw    = cursor.NextBytes( length );
                values.emplace_back( std::string( raw.begin(), raw.end() ) );
                break;
            }
        }
    }

    // Discard the result — we only care that ParseBurnEventValues never
    // crashes, hangs, or triggers an ASan finding regardless of which
    // variant each slot resolves to or how short the input is.
    auto result = sgns::BridgeRelayer::ParseBurnEventValues( values );
    (void)result;

    return 0;
}
