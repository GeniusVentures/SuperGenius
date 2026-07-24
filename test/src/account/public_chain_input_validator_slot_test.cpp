#include <gtest/gtest.h>

#include <cstdint>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "account/PublicChainInputValidator.hpp"

namespace
{
    using sgns::PublicChainInputValidator;
    using sgns::WeightedRpcEndpoint;

    // Independent SHA-256 reference vectors for the URL strings below
    // (hex), used to validate GetSlotHash returns the exact digest.
    const std::string kDirectUrlHex =
        "e2c29ceef383507e9526c614e573f977582b386d7dbbad3732701f8eb93ca432";
    const std::string kPublic1UrlHex =
        "8138533af946acc5b4d124a69b7d4a60f5967e3f0f1810f2275d25e0d6e76662";
    const std::string kPublic2UrlHex =
        "6c15f1c209ad55fd55d41e4b0f355d0728d926c977dbe7ff41a284fb96f13c3b";

    std::string ToHex( const std::vector<uint8_t> &bytes )
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve( bytes.size() * 2 );
        for ( const auto b : bytes )
        {
            out.push_back( kHex[( b >> 4 ) & 0x0F] );
            out.push_back( kHex[b & 0x0F] );
        }
        return out;
    }

    WeightedRpcEndpoint MakeEndpoint( std::string url, uint8_t weight )
    {
        WeightedRpcEndpoint ep;
        ep.url              = std::move( url );
        ep.consensus_weight = weight;
        return ep;
    }

    TEST( PublicChainInputValidatorSlotHashTest, SlotZeroReturnsHashOfFirstDirectApiEndpoint )
    {
        PublicChainInputValidator validator;
        validator.SetRpcEndpoints( "1",
                                   { MakeEndpoint( "https://direct.example.com/rpc", 50 ),
                                     MakeEndpoint( "https://public1.example.com/rpc", 25 ) } );

        const auto hash = validator.GetSlotHash( 0, "1" );
        EXPECT_EQ( hash.size(), 32u );
        EXPECT_EQ( ToHex( hash ), kDirectUrlHex );
    }

    TEST( PublicChainInputValidatorSlotHashTest, SlotOneReturnsHashOfFirstPublicEndpoint )
    {
        PublicChainInputValidator validator;
        validator.SetRpcEndpoints( "1",
                                   { MakeEndpoint( "https://direct.example.com/rpc", 50 ),
                                     MakeEndpoint( "https://public1.example.com/rpc", 25 ),
                                     MakeEndpoint( "https://public2.example.com/rpc", 25 ) } );

        const auto hash = validator.GetSlotHash( 1, "1" );
        EXPECT_EQ( hash.size(), 32u );
        EXPECT_EQ( ToHex( hash ), kPublic1UrlHex );
    }

    TEST( PublicChainInputValidatorSlotHashTest, SlotTwoReturnsHashOfSecondPublicEndpoint )
    {
        PublicChainInputValidator validator;
        validator.SetRpcEndpoints( "1",
                                   { MakeEndpoint( "https://public1.example.com/rpc", 25 ),
                                     MakeEndpoint( "https://public2.example.com/rpc", 25 ) } );

        const auto hash = validator.GetSlotHash( 2, "1" );
        EXPECT_EQ( hash.size(), 32u );
        EXPECT_EQ( ToHex( hash ), kPublic2UrlHex );
    }

    TEST( PublicChainInputValidatorSlotHashTest, SlotHashEmptyWhenNoQualifyingEndpoint )
    {
        PublicChainInputValidator validator;
        // Only PUBLIC endpoints -> slot 0 (DIRECT_API) must be empty.
        validator.SetRpcEndpoints( "1",
                                   { MakeEndpoint( "https://public1.example.com/rpc", 25 ) } );

        EXPECT_TRUE( validator.GetSlotHash( 0, "1" ).empty() );

        // No second PUBLIC endpoint -> slot 2 must be empty.
        EXPECT_TRUE( validator.GetSlotHash( 2, "1" ).empty() );
    }

    TEST( PublicChainInputValidatorSlotHashTest, SlotHashEmptyForUnknownChainId )
    {
        PublicChainInputValidator validator;
        EXPECT_TRUE( validator.GetSlotHash( 0, "unknown" ).empty() );
    }

    TEST( PublicChainInputValidatorSlotHashTest, SlotHashEmptyForUnknownSlotIndex )
    {
        PublicChainInputValidator validator;
        validator.SetRpcEndpoints( "1",
                                   { MakeEndpoint( "https://direct.example.com/rpc", 50 ) } );
        // Fail-closed: unknown slot index returns empty (abstention).
        EXPECT_TRUE( validator.GetSlotHash( 3, "1" ).empty() );
        EXPECT_TRUE( validator.GetSlotHash( 99, "1" ).empty() );
    }

    TEST( PublicChainInputValidatorSlotTest, ConcurrentPublicationCannotMixVoteSlotGenerations )
    {
        PublicChainInputValidator validator;
        const auto config_a = std::vector<WeightedRpcEndpoint>{
            MakeEndpoint( "https://a-direct.example/rpc", 50 ),
            MakeEndpoint( "https://a-public-1.example/rpc", 25 ),
            MakeEndpoint( "https://a-public-2.example/rpc", 25 )
        };
        const auto config_b = std::vector<WeightedRpcEndpoint>{
            MakeEndpoint( "https://b-direct.example/rpc", 50 ),
            MakeEndpoint( "https://b-public-1.example/rpc", 25 ),
            MakeEndpoint( "https://b-public-2.example/rpc", 25 )
        };

        validator.SetRpcEndpoints( "1", config_a );
        const auto snapshot_a = validator.GetVoteRpcSnapshot();
        validator.SetRpcEndpoints( "1", config_b );
        const auto snapshot_b = validator.GetVoteRpcSnapshot();
        ASSERT_TRUE( snapshot_a.has_value() );
        ASSERT_TRUE( snapshot_b.has_value() );

        std::promise<void> start_promise;
        auto start = start_promise.get_future().share();
        std::atomic<bool> failed{ false };
        std::thread reader( [&] {
            start.wait();
            for ( size_t i = 0; i < 400; ++i )
            {
                const auto vote = validator.GetVoteRpcSnapshot();
                if ( !vote.has_value()
                     || ( vote->slot_hashes != snapshot_a->slot_hashes
                          && vote->slot_hashes != snapshot_b->slot_hashes ) )
                {
                    failed.store( true );
                    return;
                }
                EXPECT_EQ( validator.GetFirstConfiguredChainId(), std::optional<std::string>( "1" ) );
                EXPECT_TRUE( validator.GetFirstRpcUrl( "1" ).has_value() );
                EXPECT_EQ( validator.GetSlotHash( 0, "1" ).size(), 32u );
            }
        } );
        std::thread writer( [&] {
            start.wait();
            for ( size_t i = 0; i < 400; ++i )
            {
                validator.SetRpcEndpoints( "1", i % 2 == 0 ? config_a : config_b );
            }
        } );

        start_promise.set_value();
        reader.join();
        writer.join();
        EXPECT_FALSE( failed.load() );
    }
} // namespace
