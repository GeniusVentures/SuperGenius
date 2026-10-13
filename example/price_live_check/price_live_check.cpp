// Live smoke check for PriceHttpClient against a real price endpoint.
// Exercises TLS verification with the pinned CA bundle, the SGNS User-Agent,
// and status classification (200 / 403 WAF block / 429 rate limit).
//
// Usage: price_live_check [--url <base>] [--format coingecko|gnus]
//                         [--ids a,b,c] [--currency usd] [--repeat N] [--delay-ms M]
// Exit code: 0 if every fetch succeeded, 1 otherwise.

#include <coinprices/PriceHttpClient.hpp>

#include <boost/asio.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::vector<std::string> Split( const std::string &aText )
    {
        std::vector<std::string> parts;
        std::stringstream        stream( aText );
        std::string              item;
        while ( std::getline( stream, item, ',' ) )
        {
            if ( !item.empty() )
            {
                parts.push_back( item );
            }
        }
        return parts;
    }
}

int main( int argc, char *argv[] )
{
    std::string              url      = "https://api.coingecko.com";
    std::string              format   = "coingecko";
    std::string              currency = "usd";
    std::vector<std::string> ids      = { "genius-ai", "bitcoin" };
    int                      repeat   = 1;
    int                      delayMs  = 2000;

    for ( int i = 1; i + 1 < argc; i += 2 )
    {
        const std::string key = argv[i];
        const std::string val = argv[i + 1];
        if ( key == "--url" )
        {
            url = val;
        }
        else if ( key == "--format" )
        {
            format = val;
        }
        else if ( key == "--ids" )
        {
            ids = Split( val );
        }
        else if ( key == "--currency" )
        {
            currency = val;
        }
        else if ( key == "--repeat" )
        {
            repeat = std::stoi( val );
        }
        else if ( key == "--delay-ms" )
        {
            delayMs = std::stoi( val );
        }
    }

    const auto responseFormat = ( format == "gnus" ) ? sgns::ResponseFormat::GnusEnvelope
                                                     : sgns::ResponseFormat::CoinGeckoSimplePrice;

    sgns::PriceHttpClient client( url,
                                  sgns::RetryConfig{},
                                  std::chrono::seconds( 60 ),
                                  [] { return std::chrono::system_clock::now(); },
                                  std::chrono::milliseconds( 5000 ),
                                  responseFormat );

    std::cout << "endpoint=" << url << " format=" << format << " UA=" << sgns::PriceHttpClient::kUserAgent
              << std::endl;

    int failures = 0;
    for ( int n = 1; n <= repeat; ++n )
    {
        auto ioc    = std::make_shared<boost::asio::io_context>();
        auto result = client.FetchPrices( ioc, ids, currency );

        if ( result )
        {
            std::cout << "[" << n << "] OK attempts=" << client.AttemptsLastFetch() << std::endl;
            for ( const auto &quote : result.value() )
            {
                std::cout << "    " << quote.asset << " " << quote.currency << " = " << quote.price
                          << " (fetchedAt=" << quote.FetchedAtEpochSeconds() << ", stale=" << quote.stale << ")"
                          << std::endl;
            }
        }
        else
        {
            ++failures;
            const auto &failure = result.error();
            std::cout << "[" << n << "] FAIL attempts=" << client.AttemptsLastFetch()
                      << " code=" << static_cast<int>( failure.code ) << " httpStatus=" << failure.httpStatus
                      << " message=\"" << failure.Message() << "\"" << std::endl;
        }

        if ( n < repeat )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( delayMs ) );
        }
    }

    std::cout << "summary: " << ( repeat - failures ) << "/" << repeat << " succeeded" << std::endl;
    return failures == 0 ? 0 : 1;
}
