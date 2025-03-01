#ifndef _COIN_PRICES_HPP_
#define _COIN_PRICES_HPP_
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include "boost/asio.hpp"
#include "base/logger.hpp"
#include <outcome/outcome.hpp>


namespace sgns
{
    class CoinGeckoPriceRetriever
    {
    public:
        // Define error types for specific failures
        enum class PriceError {
            EmptyInput = 1,
            NetworkError,
            JsonParseError,
            NoDataFound,
            RateLimitExceeded,
            DateTooOld
        };
        CoinGeckoPriceRetriever();
        
        // Helper method to format Unix timestamp to DD-MM-YYYY for CoinGecko API
        std::string formatDate(int64_t timestamp, bool includeTime = false);

        // Get current prices for the specified tokens
        outcome::result<std::map<std::string, double>> getCurrentPrices(const std::vector<std::string>& tokenIds);

        // Get historical prices for the specified tokens at the given timestamps
        outcome::result<std::map<std::string, std::map<int64_t, double>>> getHistoricalPrices(
            const std::vector<std::string>& tokenIds,
            const std::vector<int64_t>& timestamps);

        // Get historical price range for the specified tokens within the date range
        outcome::result<std::map<std::string, std::map<int64_t, double>>> getHistoricalPriceRange(
            const std::vector<std::string>& tokenIds,
            int64_t from,
            int64_t to);

    private:
        base::Logger m_logger = sgns::base::createLogger( "CoinPrices" );
    };
}

#endif