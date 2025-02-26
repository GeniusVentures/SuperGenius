#ifndef _COIN_PRICES_HPP_
#define _COIN_PRICES_HPP_
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

namespace sgns
{
    class CoinGeckoPriceRetriever
    {
    public:
        CoinGeckoPriceRetriever();
        
        // Helper method to format Unix timestamp to DD-MM-YYYY for CoinGecko API
        std::string formatDate(int64_t timestamp, bool includeTime = false);

        // Get current prices for the specified tokens
        std::map<std::string, double> getCurrentPrices(const std::vector<std::string>& tokenIds);

        // Get historical prices for the specified tokens at the given timestamps
        std::map<std::string, std::map<int64_t, double>> getHistoricalPrices(
            const std::vector<std::string>& tokenIds,
            const std::vector<int64_t>& timestamps);

        // Get historical price range for the specified tokens within the date range
        std::map<std::string, std::map<int64_t, double>> getHistoricalPriceRange(
            const std::vector<std::string>& tokenIds,
            int64_t from,
            int64_t to);

    private:
        std::string tokenId_;
        
        // Helper method to make HTTP requests
        http::response<http::string_body> makeHttpRequest(
            const std::string& host, const std::string& target);

    };
}

#endif