#include <gtest/gtest.h>
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
#include <coinprices/coinprices.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

class PriceRetrievalTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}
};


TEST_F(PriceRetrievalTest, GetCurrentPrice) 
{
    auto ioc = std::make_shared<boost::asio::io_context>();
    sgns::CoinGeckoPriceRetriever retriever; // Genius token ID on CoinGecko
    std::vector<std::string> tokenIds;
    tokenIds.push_back("genius-ai");
    //tokenIds.push_back("bitcoin");
    auto prices = retriever.getCurrentPrices(tokenIds);
    //std::string symbol = retriever.getTokenSymbol();
    for (const auto& [token, price] : prices.value())
    {
        std::cout << token << ": $" << std::fixed << std::setprecision(4) << price << std::endl;
        EXPECT_GT(price, 0.0);
    }
}
    

TEST_F(PriceRetrievalTest, GetHistoricalPrices) 
{
    auto ioc = std::make_shared<boost::asio::io_context>();
    sgns::CoinGeckoPriceRetriever retriever;

    // Get current time
    time_t now = std::time(nullptr);
    
    // Create timestamps for dates within the past year for CoinGecko free tier
    // Note: Using dates relative to 'now' instead of hardcoded
    std::vector<int64_t> timestamps = {
        (now - 300 * 24 * 60 * 60) * 1000, // ~10 months ago
        (now - 180 * 24 * 60 * 60) * 1000, // ~6 months ago
        //(now - 90 * 24 * 60 * 60) * 1000,  // ~3 months ago
        //(now - 30 * 24 * 60 * 60) * 1000   // ~1 month ago
    };
    
    std::cout << "Testing with dates:" << std::endl;
    for (int64_t ts : timestamps) {
        std::cout << "- " << retriever.formatDate(ts) << std::endl;
    }
    std::vector<std::string> tokenIds;
    tokenIds.push_back("genius-ai");
    auto prices = retriever.getHistoricalPrices(tokenIds, timestamps);
    
    // We expect all timestamps to be within the allowed range
    EXPECT_EQ(prices.value()["genius-ai"].size(), timestamps.size());
    
    for (const auto& [timestamp, price] : prices.value()["genius-ai"]) {
        std::cout << "Price on " << retriever.formatDate(timestamp) << ": $" << price << std::endl;
        EXPECT_GT(price, 0.0);
    }
}

TEST_F(PriceRetrievalTest, GetHistoricalPriceRange) 
{
    auto ioc = std::make_shared<boost::asio::io_context>();
    sgns::CoinGeckoPriceRetriever retriever;
    
    // Get now and 30 days ago in seconds
    time_t now = std::time(nullptr);
    time_t thirtyDaysAgo = now - (30 * 24 * 60 * 60);
    
    std::cout << "Date range for historical data:" << std::endl;
    std::cout << "From: " << retriever.formatDate(thirtyDaysAgo * 1000) << std::endl;
    std::cout << "To: " << retriever.formatDate(now * 1000) << std::endl;
    std::vector<std::string> tokenIds;
    tokenIds.push_back("genius-ai");
    // Convert to milliseconds for the API function
    auto prices = retriever.getHistoricalPriceRange(tokenIds, thirtyDaysAgo * 1000, now * 1000);
    
    EXPECT_GT(prices.value()["genius-ai"].size(), 0);
    
    std::cout << "Price range from " << retriever.formatDate(thirtyDaysAgo * 1000) 
              << " to " << retriever.formatDate(now * 1000) << ":" << std::endl;
    
    //Just print the first few and last few prices to avoid flooding the output
    size_t count = 0;
    for (const auto& [timestamp, price] : prices.value()["genius-ai"]) {
        if (count < 5 || count > prices.value().size() - 5) {
            std::cout << "  " << retriever.formatDate(timestamp, true) << ": $" << price << std::endl;
        } else if (count == 5) {
            std::cout << "  ... (" << (prices.value()["genius-ai"].size() - 10) << " more entries) ..." << std::endl;
        }
        count++;
    }
}