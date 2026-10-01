/**
 * Source file for the PriceHttpClient facade — status gate before parse,
 * CoinGecko UA + 5s timeouts, transient-only retry with 429 hold-off,
 * PriceQuote production (LPM-05/06/07, D-08..D-14).
 */
#include "PriceHttpClient.hpp"

#include "HTTPClient.hpp"
#include "URLStringUtil.h"

#include <rapidjson/document.h>

#include <fmt/format.h>

#include <thread>

#ifndef SGNS_DEFAULT_CACERT_PATH
#define SGNS_DEFAULT_CACERT_PATH ""
#endif

namespace sgns
{
    namespace
    {
        // Default per-request timeout for the price path (LPM-06: ~5s).
        // Kept as documentation of the default ctor argument value.
        constexpr std::chrono::milliseconds kPriceTimeout{ 5000 };
    } // namespace

    PriceHttpClient::PriceHttpClient( std::string              baseUrl,
                                      RetryConfig               retryConfig,
                                      std::chrono::seconds      holdOffDuration,
                                      RateLimitHoldOff::Clock   clock,
                                      std::chrono::milliseconds requestTimeout,
                                      ResponseFormat            responseFormat )
        : baseUrl_( std::move( baseUrl ) ),
          retryConfig_( retryConfig ),
          holdOffDuration_( holdOffDuration ),
          holdOff_( std::move( clock ) ),
          requestTimeout_( requestTimeout ),
          responseFormat_( responseFormat )
    {
    }

    PriceResult<std::vector<PriceQuote>> PriceHttpClient::FetchPrices( std::shared_ptr<boost::asio::io_context> ioc,
                                                                       const std::vector<std::string>          &tokenIds,
                                                                       const std::string                       &currency )
    {
        attemptsLastFetch_.store( 0 );

        if ( tokenIds.empty() )
        {
            return outcome::failure( PriceFetchFailure{ PriceFetchError::EmptyInput, 0 } );
        }

        // Hold-off gate FIRST (D-13): while held, the tier is skipped
        // entirely — no transport objects, no network calls.
        if ( holdOff_.IsHeldOff() )
        {
            m_logger->warn( "CoinGecko tier held off after 429 — skipping fetch" );
            return outcome::failure( PriceFetchFailure{ PriceFetchError::RateLimitExceeded, 429 } );
        }

        // Split the base URL with the existing parser (D-04 — no new URL
        // machinery). parseHTTPUrl expects host[/path]; strip the scheme.
        std::string base = baseUrl_;
        const auto  schemePos = base.find( "://" );
        const bool  useTLS = ( schemePos != std::string::npos && base.compare( 0, schemePos, "https" ) == 0 );
        if ( schemePos != std::string::npos )
        {
            base = base.substr( schemePos + 3 );
        }
        std::string host;
        std::string basePath;
        std::string port;
        if ( !parseHTTPUrl( base + "/", host, basePath, port ) )
        {
            return outcome::failure( PriceFetchFailure{ PriceFetchError::EmptyInput, 0 } );
        }

        std::string idsJoined;
        for ( size_t i = 0; i < tokenIds.size(); ++i )
        {
            if ( i > 0 )
            {
                idsJoined += ",";
            }
            idsJoined += tokenIds[i];
        }
        // Target builder per tier (03-03): CoinGecko direct vs the
        // token.gnus.ai envelope endpoint (note `vs=`, not `vs_currencies=`).
        std::string target;
        if ( responseFormat_ == ResponseFormat::GnusEnvelope )
        {
            target = "/v1/prices?ids=" + idsJoined + "&vs=" + currency;
        }
        else
        {
            target = "/api/v3/simple/price?ids=" + idsJoined + "&vs_currencies=" + currency;
        }

        http::RequestOptions options;
        options.userAgent        = kUserAgent;
        options.connectTimeout   = requestTimeout_;
        options.handshakeTimeout = requestTimeout_;
        options.readTimeout      = requestTimeout_;
        if ( useTLS )
        {
            options.caCertFile = std::string( SGNS_DEFAULT_CACERT_PATH );
        }

        PriceFetchFailure lastFailure{ PriceFetchError::NetworkError, 0 };

        for ( int attempt = 1; attempt <= retryConfig_.maxAttempts; ++attempt )
        {
            attemptsLastFetch_.store( attempt );

            // Fresh context per attempt: a run()-to-completion ioc cannot be
            // reliably reused for the next sequential blocking exchange, and
            // the retry loop is inherently sequential.
            auto attemptIoc = std::make_shared<boost::asio::io_context>();
            auto client     = std::make_shared<HTTPClient>( host, target, port );
            auto result     = client->ExecuteBlocking( attemptIoc, options );

            if ( !result )
            {
                // Transport failure — classify transiency before mapping
                // (D-14): the classification is carried on the failure so
                // IsTransient gates the retry decision strictly.
                const auto transportClass = static_cast<http::ClientError>( result.error().value() );
                lastFailure              = PriceFetchFailure{ PriceFetchError::NetworkError, 0, transportClass };
                m_logger->warn( "Price fetch attempt {}/{} failed: transport {} ({})",
                                attempt,
                                retryConfig_.maxAttempts,
                                result.error().message(),
                                IsTransientTransport( transportClass ) ? "transient" : "permanent" );
                if ( ShouldRetry( lastFailure, attempt, retryConfig_ ) == RetryDecision::Retry )
                {
                    std::this_thread::sleep_for( retryConfig_.backoffBeforeRetry[attempt - 1] );
                    continue;
                }
                return outcome::failure( lastFailure );
            }

            const auto &response = result.value();

            // STATUS GATE (LPM-05 structural guarantee): no parse of any
            // non-200 body — rapidjson is unreachable below unless 200.
            if ( response.status != 200 )
            {
                PriceFetchFailure failure{ PriceFetchError::HttpStatus, response.status };
                if ( response.status == 403 )
                {
                    failure.code = PriceFetchError::Blocked;
                }
                else if ( response.status == 429 )
                {
                    failure.code = PriceFetchError::RateLimitExceeded;
                    holdOff_.TriggerHoldOff( holdOffDuration_ );
                }
                m_logger->error( "{} from {}{}", failure.Message(), baseUrl_, target );
                return outcome::failure( failure );
            }

            // Single parse branch — only ever reached on status == 200.
            // Parsing is delegated to the tier parsers (03-03): the parser
            // returns status 0; the facade wraps its JsonParseError with the
            // observed response status for facade-parity error reporting.
            // (PriceResult is not default-constructible — assign through a
            // success-typed placeholder rather than default-initializing.)
            PriceResult<std::vector<PriceQuote>> parsed =
                responseFormat_ == ResponseFormat::GnusEnvelope
                    ? ParseGnusPriceEnvelope( response.body, tokenIds, currency )
                    : ParseCoinGeckoSimplePrice( response.body,
                                                 tokenIds,
                                                 currency,
                                                 std::chrono::system_clock::now() );
            if ( !parsed )
            {
                if ( parsed.error().code == PriceFetchError::JsonParseError )
                {
                    m_logger->error( "JSON parse error on 200 body" );
                    return outcome::failure(
                        PriceFetchFailure{ PriceFetchError::JsonParseError, response.status } );
                }
                return outcome::failure( parsed.error() );
            }
            return outcome::success( std::move( parsed.value() ) );
        }

        return outcome::failure( lastFailure );
    }
} // namespace sgns
