// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/event_filter.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace
{
    template <typename Array>
    Array Filled( uint8_t seed )
    {
        Array value{};
        for ( size_t i = 0; i < value.size(); ++i )
        {
            value[i] = static_cast<uint8_t>( seed + i );
        }
        return value;
    }

    eth::codec::LogEntry MakeLog( const eth::codec::Address &address,
                                  const eth::codec::Hash256 &topic )
    {
        eth::codec::LogEntry log;
        log.address = address;
        log.topics.push_back( topic );
        return log;
    }

    std::vector<eth::MatchedEvent> ObserveReceipt( uint32_t first_block_log_index )
    {
        const auto bridge_address = Filled<eth::codec::Address>( 0x20 );
        const auto other_address = Filled<eth::codec::Address>( 0x40 );
        const auto burn_topic = Filled<eth::codec::Hash256>( 0x60 );

        eth::codec::Receipt receipt;
        receipt.status = true;
        receipt.logs = {
            MakeLog( other_address, Filled<eth::codec::Hash256>( 0x70 ) ),
            MakeLog( bridge_address, burn_topic ),
            MakeLog( other_address, Filled<eth::codec::Hash256>( 0x80 ) ),
            MakeLog( bridge_address, burn_topic ),
        };

        eth::EventFilter filter;
        filter.addresses.push_back( bridge_address );
        filter.topics.push_back( burn_topic );

        std::vector<eth::MatchedEvent> observed;
        eth::EventWatcher watcher;
        watcher.watch( std::move( filter ),
                       [&]( const eth::MatchedEvent &event ) { observed.push_back( event ); } );
        watcher.process_receipt( receipt,
                                 Filled<eth::codec::Hash256>( 0x90 ),
                                 123,
                                 Filled<eth::codec::Hash256>( 0xa0 ),
                                 first_block_log_index );
        return observed;
    }
} // namespace

TEST( BridgeEventIdentityTest, ReceiptLocalOrdinalIncludesUnrelatedLogs )
{
    const auto observed = ObserveReceipt( 41 );

    ASSERT_EQ( observed.size(), 2u );
    EXPECT_EQ( observed[0].log_index, 42u );
    EXPECT_EQ( observed[1].log_index, 44u );
    ASSERT_TRUE( observed[0].receipt_log_index.has_value() );
    ASSERT_TRUE( observed[1].receipt_log_index.has_value() );
    EXPECT_EQ( *observed[0].receipt_log_index, 1u );
    EXPECT_EQ( *observed[1].receipt_log_index, 3u );
}

TEST( BridgeEventIdentityTest, ReInclusionChangesBlockIndexButNotReceiptOrdinal )
{
    const auto first = ObserveReceipt( 8 );
    const auto re_included = ObserveReceipt( 107 );

    ASSERT_EQ( first.size(), 2u );
    ASSERT_EQ( re_included.size(), 2u );
    EXPECT_NE( first[0].log_index, re_included[0].log_index );
    EXPECT_NE( first[1].log_index, re_included[1].log_index );
    EXPECT_EQ( first[0].receipt_log_index, re_included[0].receipt_log_index );
    EXPECT_EQ( first[1].receipt_log_index, re_included[1].receipt_log_index );
}

TEST( BridgeEventIdentityTest, BlockWideObservationCannotInventReceiptOrdinal )
{
    const auto bridge_address = Filled<eth::codec::Address>( 0x20 );
    const auto burn_topic = Filled<eth::codec::Hash256>( 0x60 );

    eth::EventFilter filter;
    filter.addresses.push_back( bridge_address );
    filter.topics.push_back( burn_topic );

    std::optional<eth::MatchedEvent> observed;
    eth::EventWatcher watcher;
    watcher.watch( std::move( filter ),
                   [&]( const eth::MatchedEvent &event ) { observed = event; } );
    watcher.process_block_logs( { MakeLog( bridge_address, burn_topic ) },
                                123,
                                Filled<eth::codec::Hash256>( 0xa0 ) );

    ASSERT_TRUE( observed.has_value() );
    EXPECT_FALSE( observed->receipt_log_index.has_value() );
}
