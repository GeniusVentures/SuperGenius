#pragma once

#include <HTTPTypes.hpp>

#include <fmt/format.h>
#include <libp2p/outcome/outcome.hpp>

#include <optional>
#include <string>
#include <utility>

namespace outcome
{
    using libp2p::outcome::failure;
    using libp2p::outcome::result;
    using libp2p::outcome::success;
}

namespace sgns
{
    /// @brief Typed price-fetch error taxonomy (D-15).
    /// @brief The first six values preserve the legacy
    /// CoinGeckoPriceRetriever::PriceError names and ordering byte-for-byte
    /// (coinprices.hpp:22-30) so the still-compiling legacy retriever and the
    /// new facade share vocabulary; HttpStatus and Blocked are new.
    enum class PriceFetchError
    {
        EmptyInput       = 1,
        NetworkError,   // legacy slot 2 — transport-class failures map here
        JsonParseError, // legacy slot 3
        NoDataFound,    // legacy slot 4
        RateLimitExceeded, // legacy slot 5
        DateTooOld,     // legacy slot 6
        HttpStatus,     // new: any non-200 status other than 403/429
        Blocked,        // new: 403 WAF block (CloudFront-style)
    };

    /// @brief Struct error carrier: a code plus the HTTP status that produced
    /// it, so tests and logs assert the numeric status without string-matching.
    /// @brief httpStatus == 0 means no HTTP response was received at all
    /// (pure transport failure).
    /// @brief Existing two-field braced initializers ({code, status}) keep
    /// compiling — transportError is a defaulted member (aggregate init).
    struct PriceFetchFailure
    {
        PriceFetchError code       = PriceFetchError::NetworkError;
        unsigned        httpStatus = 0;

        /// @brief Transport classification when code == NetworkError (D-14);
        /// nullopt = unclassified.
        std::optional<http::ClientError> transportError;

        /// @brief Human-readable failure text; embeds the numeric status when
        /// one exists. The facade logs this verbatim (criterion 1: the log
        /// contains the literal "403"/"429").
        std::string Message() const
        {
            switch ( code )
            {
                case PriceFetchError::EmptyInput:
                    return "Empty Input";
                case PriceFetchError::NetworkError:
                    return httpStatus != 0 ? fmt::format( "Network Error: HTTP {}", httpStatus )
                                           : std::string( "Network Error" );
                case PriceFetchError::JsonParseError:
                    return httpStatus != 0 ? fmt::format( "JSON parse error: HTTP {}", httpStatus )
                                           : std::string( "JSON parse error" );
                case PriceFetchError::NoDataFound:
                    return "No data found";
                case PriceFetchError::RateLimitExceeded:
                    return httpStatus != 0 ? fmt::format( "rate limit exceeded: HTTP {}", httpStatus )
                                           : std::string( "Rate limit exceeded" );
                case PriceFetchError::DateTooOld:
                    return "Date exceeds year limit";
                case PriceFetchError::HttpStatus:
                    return fmt::format( "HTTP {}", httpStatus );
                case PriceFetchError::Blocked:
                    return fmt::format( "price fetch blocked: HTTP {}", httpStatus );
            }
            return "Unknown error";
        }
    };

    /// @brief result alias carrying the status-bearing failure type.
    /// @brief The terminate policy is required for a non-error_code error
    /// type (boost::outcome's default_policy assumes std::error_code and
    /// drags in exception_ptr machinery that cannot convert). Observers used
    /// here (bool/operator*/error) are policy-safe.
    template <typename T>
    using PriceResult =
        boost::outcome_v2::basic_result<T, PriceFetchFailure, boost::outcome_v2::policy::terminate>;
} // namespace sgns
