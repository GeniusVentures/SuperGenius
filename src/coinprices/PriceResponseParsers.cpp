/**
 * Source file for PriceResponseParsers — the two tier parsers (03-03).
 * ParseCoinGeckoSimplePrice is the parse loop moved behavior-identically
 * from PriceHttpClient.cpp:153-175; ParseGnusPriceEnvelope implements the
 * Phase-1 envelope.ts contract.
 */
#include "PriceResponseParsers.hpp"

#include <rapidjson/document.h>

#include <utility>

namespace sgns
{
    PriceResult<std::vector<PriceQuote>> ParseCoinGeckoSimplePrice( const std::string              &body,
                                                                    const std::vector<std::string> &tokenIds,
                                                                    const std::string              &currency,
                                                                    std::chrono::system_clock::time_point fetchTime )
    {
        rapidjson::Document document;
        document.Parse( body.c_str() );
        if ( document.HasParseError() )
        {
            return outcome::failure( PriceFetchFailure{ PriceFetchError::JsonParseError, 0 } );
        }

        std::vector<PriceQuote> quotes;
        for ( const auto &id : tokenIds )
        {
            // IsNumber covers int and double literals — CoinGecko may
            // serialize whole-number prices without a decimal point
            if ( document.IsObject() && document.HasMember( id.c_str() ) && document[id.c_str()].IsObject()
                 && document[id.c_str()].HasMember( currency.c_str() )
                 && document[id.c_str()][currency.c_str()].IsNumber() )
            {
                PriceQuote quote;
                quote.asset     = id;
                quote.currency  = currency;
                quote.price     = document[id.c_str()][currency.c_str()].GetDouble();
                quote.timestamp = fetchTime; // D-14: fetch time
                quote.source    = PriceSource::CoinGecko;
                quote.stale     = false;
                quotes.push_back( std::move( quote ) );
            }
            // Unknown ids stay absent — partial coverage, not an error
        }

        if ( quotes.empty() )
        {
            return outcome::failure( PriceFetchFailure{ PriceFetchError::NoDataFound, 0 } );
        }
        return outcome::success( std::move( quotes ) );
    }

    PriceResult<std::vector<PriceQuote>> ParseGnusPriceEnvelope( const std::string              &body,
                                                                 const std::vector<std::string> &tokenIds,
                                                                 const std::string              &currency )
    {
        rapidjson::Document document;
        document.Parse( body.c_str() );
        if ( document.HasParseError() || !document.IsObject() )
        {
            return outcome::failure( PriceFetchFailure{ PriceFetchError::JsonParseError, 0 } );
        }
        if ( !document.HasMember( "currency" ) || !document["currency"].IsString()
             || !document.HasMember( "prices" ) || !document["prices"].IsObject()
             || !document.HasMember( "fetchedAt" ) || !document["fetchedAt"].IsInt64()
             || !document.HasMember( "source" ) || !document["source"].IsString()
             || !document.HasMember( "stale" ) || !document["stale"].IsBool() )
        {
            return outcome::failure( PriceFetchFailure{ PriceFetchError::JsonParseError, 0 } );
        }

        // LANDMINE (Phase-1 D-11): fetchedAt is epoch SECONDS and the max
        // across the returned ids — one shared timestamp for all quotes.
        const auto fetchedAt = std::chrono::system_clock::time_point{
            std::chrono::seconds{ document["fetchedAt"].GetInt64() }
        };

        // LANDMINE (Phase-1 D-06): exactly two source kinds exist; the
        // envelope source names the producing upstream, NOT the serving
        // tier. Unknown source strings are malformed, not silently mapped.
        const std::string source( document["source"].GetString() );
        PriceSource       quoteSource;
        if ( source == "coingecko" )
        {
            quoteSource = PriceSource::CoinGecko;
        }
        else if ( source == "coingecko-cache" )
        {
            quoteSource = PriceSource::GnusPriceService;
        }
        else
        {
            return outcome::failure( PriceFetchFailure{ PriceFetchError::JsonParseError, 0 } );
        }

        const bool envelopeStale = document["stale"].GetBool();
        const auto &prices       = document["prices"];

        std::vector<PriceQuote> quotes;
        for ( const auto &id : tokenIds )
        {
            if ( prices.HasMember( id.c_str() ) && prices[id.c_str()].IsNumber() )
            {
                PriceQuote quote;
                quote.asset     = id;
                quote.currency  = currency;
                quote.price     = prices[id.c_str()].GetDouble();
                quote.timestamp = fetchedAt; // shared max-of-ids timestamp (D-11)
                quote.source    = quoteSource;
                quote.stale     = envelopeStale;
                quotes.push_back( std::move( quote ) );
            }
            // Absent ids stay absent — partial coverage, not an error (D-09)
        }

        if ( quotes.empty() )
        {
            return outcome::failure( PriceFetchFailure{ PriceFetchError::NoDataFound, 0 } );
        }
        return outcome::success( std::move( quotes ) );
    }
} // namespace sgns
