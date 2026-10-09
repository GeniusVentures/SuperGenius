/**
 * Source file for the PriceRetryPolicy — transient-only retry classification,
 * fixed backoff schedule, and the CoinGecko 429 hold-off (D-11/D-12/D-13).
 */
#include "PriceRetryPolicy.hpp"

namespace sgns
{
    bool IsTransientTransport( http::ClientError error )
    {
        switch ( error )
        {
            case http::ClientError::TIMEOUT:
            case http::ClientError::CONNECT_FAILED:
            case http::ClientError::RESOLVE_FAILED:
                return true;
            case http::ClientError::TLS_HANDSHAKE_FAILED:
            case http::ClientError::TLS_CA_LOAD_FAILED:
            case http::ClientError::WRITE_FAILED:
            case http::ClientError::READ_INTERRUPTED:
            case http::ClientError::NO_HEADER:
                break;
        }
        return false;
    }

    bool IsTransient( const PriceFetchFailure &error )
    {
        // Only pure transport failures are retryable (D-11): any HTTP status
        // at all (403/429/404/5xx...) means the server answered — retrying
        // cannot change the answer.
        if ( error.httpStatus != 0 )
        {
            return false;
        }
        if ( error.code != PriceFetchError::NetworkError )
        {
            return false;
        }
        // D-14 — strict transport gating: TLS-handshake/CA-load/write
        // failures stop being blindly retried; only TIMEOUT/CONNECT_FAILED/
        // RESOLVE_FAILED (per IsTransientTransport) are transient. An
        // UNCLASSIFIED {NetworkError, 0} is NOT transient — every facade
        // path populates the field.
        return error.transportError && IsTransientTransport( *error.transportError );
    }

    RetryDecision ShouldRetry( const PriceFetchFailure &error, int attempt, const RetryConfig &config )
    {
        if ( !IsTransient( error ) )
        {
            return RetryDecision::GiveUp;
        }
        if ( attempt >= config.maxAttempts )
        {
            return RetryDecision::GiveUp;
        }
        return RetryDecision::Retry;
    }
} // namespace sgns
