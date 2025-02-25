#ifndef _COIN_PRICES_HPP_
#define _COIN_PRICES_HPP_
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <mutex>
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
        CoinGeckoPriceRetriever(const std::string& tokenId);
        
        // Helper method to format Unix timestamp to DD-MM-YYYY for CoinGecko API
        std::string formatDate(int64_t timestamp, bool includeTime = false);

        // Get current price
        double getCurrentPrice();

        // Get historical prices for a list of timestamps
        std::map<int64_t, double> getHistoricalPrices(const std::vector<int64_t>& timestamps);

        // Get historical price range (daily prices within a date range)
        std::map<int64_t, double> getHistoricalPriceRange(int64_t from, int64_t to);

        std::string getTokenSymbol();

    private:
        std::string tokenId_;
        
        // Helper method to make HTTP requests
        http::response<http::string_body> makeHttpRequest(
            const std::string& host, const std::string& target);

    };
}

#endif