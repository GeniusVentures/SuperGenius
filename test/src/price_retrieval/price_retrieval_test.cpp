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
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

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

enum class PriceService {
    DexScreener,
    CoinGecko
};

namespace
{
    class PriceRetriever
    {
    public:
        virtual ~PriceRetriever() = default;
        virtual double getPrice() = 0;
        virtual std::string getTokenSymbol() = 0;
    };

    class DexScreenerPriceRetriever : public PriceRetriever
    {
    public:
        DexScreenerPriceRetriever(const std::string& poolAddress) 
            : poolAddress_(poolAddress)
        {
        }

        double getPrice() override
        {
            try {
                // Set up the SSL context
                ssl::context ctx(ssl::context::tlsv12_client);
                ctx.set_default_verify_paths();

                // Set up the IO context and resolver
                net::io_context ioc;
                tcp::resolver resolver(ioc);
                
                // Resolve the host
                auto const results = resolver.resolve("api.dexscreener.io", "443");

                // Set up the SSL stream
                beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
                
                // Set SNI Hostname (many servers need this to handshake successfully)
                if(!SSL_set_tlsext_host_name(stream.native_handle(), "api.dexscreener.io"))
                {
                    beast::error_code ec{static_cast<int>(::ERR_get_error()), 
                                        net::error::get_ssl_category()};
                    throw beast::system_error{ec};
                }

                // Connect to the host
                beast::get_lowest_layer(stream).connect(results);
                
                // Perform the SSL handshake
                stream.handshake(ssl::stream_base::client);

                // Set up the HTTP request
                std::string target = "/latest/dex/pairs/ethereum/" + poolAddress_;
                http::request<http::string_body> req{http::verb::get, target, 11};
                req.set(http::field::host, "api.dexscreener.io");
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                
                // Print the request URL for debugging
                std::cout << "DexScreener request URL: https://api.dexscreener.io" << target << std::endl;

                // Send the HTTP request
                http::write(stream, req);

                // Receive the HTTP response
                beast::flat_buffer buffer;
                http::response<http::string_body> res;
                http::read(stream, buffer, res);

                // Check response status
                if (res.result_int() != 200) {
                    std::cerr << "HTTP error: " << res.result_int() << " " 
                              << res.reason() << std::endl;
                    std::cerr << "Response body: " << res.body() << std::endl;
                    return 0.0;
                }

                // Log the raw response for debugging
                std::cout << "DexScreener raw response: " << res.body().substr(0, 200) << "..." << std::endl;

                // Parse the JSON response
                rapidjson::Document document;
                document.Parse(res.body().c_str());

                // Check for JSON parse errors
                if (document.HasParseError()) {
                    std::cerr << "JSON parse error: " << rapidjson::GetParseError_En(document.GetParseError()) 
                             << " at offset " << document.GetErrorOffset() << std::endl;
                    return 0.0;
                }

                // Navigate to the price data
                if (document.HasMember("pairs") && document["pairs"].IsArray() && document["pairs"].Size() > 0) {
                    const auto& pair = document["pairs"][0];
                    
                    // Store the token symbol
                    if (pair.HasMember("baseToken") && pair["baseToken"].HasMember("symbol")) {
                        tokenSymbol_ = pair["baseToken"]["symbol"].GetString();
                    }
                    
                    // Get the price
                    if (pair.HasMember("priceUsd") && pair["priceUsd"].IsString()) {
                        try {
                            return std::stod(pair["priceUsd"].GetString());
                        } catch (const std::exception& e) {
                            std::cerr << "Error converting price: " << e.what() << std::endl;
                        }
                    }
                }

                // Beast SSL streams need to perform a proper shutdown
                beast::error_code ec;
                stream.shutdown(ec);
                if(ec == net::error::eof) {
                    ec = {};
                }
                if(ec) {
                    throw beast::system_error{ec};
                }
            }
            catch (const std::exception& e) {
                std::cerr << "DexScreener Error: " << e.what() << std::endl;
            }

            return 0.0;
        }

        std::string getTokenSymbol() override {
            return tokenSymbol_;
        }

    private:
        std::string poolAddress_;
        std::string tokenSymbol_;
    };

    class CoinGeckoPriceRetriever : public PriceRetriever
    {
    public:
        CoinGeckoPriceRetriever(const std::string& tokenId) 
            : tokenId_(tokenId)
        {
        }

        double getPrice() override
        {
            try {
                // Set up the SSL context
                ssl::context ctx(ssl::context::tlsv12_client);
                ctx.set_default_verify_paths();

                // Set up the IO context and resolver
                net::io_context ioc;
                tcp::resolver resolver(ioc);
                
                // Resolve the host
                auto const results = resolver.resolve("api.coingecko.com", "443");

                // Set up the SSL stream
                beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
                
                // Set SNI Hostname
                if(!SSL_set_tlsext_host_name(stream.native_handle(), "api.coingecko.com"))
                {
                    beast::error_code ec{static_cast<int>(::ERR_get_error()), 
                                        net::error::get_ssl_category()};
                    throw beast::system_error{ec};
                }

                // Connect to the host
                beast::get_lowest_layer(stream).connect(results);
                
                // Perform the SSL handshake
                stream.handshake(ssl::stream_base::client);

                // Set up the HTTP request
                std::string target = "/api/v3/simple/price?ids=" + tokenId_ + "&vs_currencies=usd";
                http::request<http::string_body> req{http::verb::get, target, 11};
                req.set(http::field::host, "api.coingecko.com");
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

                // Send the HTTP request
                http::write(stream, req);

                // Receive the HTTP response
                beast::flat_buffer buffer;
                http::response<http::string_body> res;
                http::read(stream, buffer, res);

                // Parse the JSON response
                rapidjson::Document document;
                document.Parse(res.body().c_str());

                // Check for JSON parse errors
                if (document.HasParseError()) {
                    std::cerr << "JSON parse error: " << rapidjson::GetParseError_En(document.GetParseError()) 
                             << " at offset " << document.GetErrorOffset() << std::endl;
                    return 0.0;
                }

                // Get the token symbol (for CoinGecko, we'll just use the tokenId)
                tokenSymbol_ = tokenId_;

                // Extract the price
                if (document.HasMember(tokenId_.c_str()) && 
                    document[tokenId_.c_str()].HasMember("usd")) {
                    return document[tokenId_.c_str()]["usd"].GetDouble();
                }

                // Beast SSL streams need to perform a proper shutdown
                beast::error_code ec;
                stream.shutdown(ec);
                if(ec == net::error::eof) {
                    ec = {};
                }
                if(ec) {
                    throw beast::system_error{ec};
                }
            }
            catch (const std::exception& e) {
                std::cerr << "CoinGecko Error: " << e.what() << std::endl;
            }

            return 0.0;
        }

        std::string getTokenSymbol() override {
            return tokenSymbol_;
        }

    private:
        std::string tokenId_;
        std::string tokenSymbol_;
    };
}

TEST_F(PriceRetrievalTest, GetTokenPriceDexScreener) 
{
    std::unique_ptr<PriceRetriever> retriever = 
        std::make_unique<DexScreenerPriceRetriever>("0x614577036F0a024DBC1C88BA616b394DD65d105a"); // Genius token pool
    
    double price = retriever->getPrice();
    std::string symbol = retriever->getTokenSymbol();
    
    std::cout << "DexScreener - Current " << symbol << " price: $" << price << std::endl;
    
    EXPECT_GT(price, 0.0);
}

TEST_F(PriceRetrievalTest, GetTokenPriceCoinGecko) 
{
    std::unique_ptr<PriceRetriever> retriever = 
        std::make_unique<CoinGeckoPriceRetriever>("genius-ai"); // Genius token ID on CoinGecko
    
    double price = retriever->getPrice();
    std::string symbol = retriever->getTokenSymbol();
    
    std::cout << "CoinGecko - Current " << symbol << " price: $" << price << std::endl;
    
    EXPECT_GT(price, 0.0);
}

TEST_F(PriceRetrievalTest, CompareServices) 
{
    std::unique_ptr<PriceRetriever> dexScreener = 
        std::make_unique<DexScreenerPriceRetriever>("0x614577036F0a024DBC1C88BA616b394DD65d105a");
    
    std::unique_ptr<PriceRetriever> coinGecko = 
        std::make_unique<CoinGeckoPriceRetriever>("genius-ai");
    
    double dexScreenerPrice = dexScreener->getPrice();
    double coinGeckoPrice = coinGecko->getPrice();
    
    std::cout << "DexScreener price: $" << dexScreenerPrice << std::endl;
    std::cout << "CoinGecko price: $" << coinGeckoPrice << std::endl;
    
    if (dexScreenerPrice > 0 && coinGeckoPrice > 0) {
        double priceDifference = std::abs(dexScreenerPrice - coinGeckoPrice);
        double percentDifference = (priceDifference / ((dexScreenerPrice + coinGeckoPrice) / 2)) * 100;
        
        std::cout << "Difference: $" << priceDifference << " (" << percentDifference << "%)" << std::endl;
        
        // Prices should be relatively close (within 5% of each other)
        EXPECT_LT(percentDifference, 5.0);
    }
}