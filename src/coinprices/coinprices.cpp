#include "coinprices.hpp"

namespace sgns
{
    CoinGeckoPriceRetriever::CoinGeckoPriceRetriever(const std::string& tokenId) 
        : tokenId_(tokenId)
    {
    }
    
    // Helper method to format Unix timestamp to DD-MM-YYYY for CoinGecko API
    std::string CoinGeckoPriceRetriever::formatDate(int64_t timestamp, bool includeTime)
    {
        // Convert milliseconds to seconds if needed
        time_t time = (timestamp > 9999999999) ? timestamp / 1000 : timestamp;
        
        std::tm tm = *std::gmtime(&time);
        std::stringstream ss;
        
        if (!includeTime) {
            // Original DD-MM-YYYY format for CoinGecko API
            ss << std::setfill('0')
                << std::setw(2) << tm.tm_mday << "-"
                << std::setw(2) << (tm.tm_mon + 1) << "-"
                << (tm.tm_year + 1900);
        } else {
            // Full date and time format: YYYY-MM-DD HH:MM:SS.mmm
            int milliseconds = 0;
            if (timestamp > 9999999999) {
                milliseconds = timestamp % 1000;
            }
            
            ss << (tm.tm_year + 1900) << "-"
                << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1) << "-"
                << std::setfill('0') << std::setw(2) << tm.tm_mday << " "
                << std::setfill('0') << std::setw(2) << tm.tm_hour << ":"
                << std::setfill('0') << std::setw(2) << tm.tm_min << ":"
                << std::setfill('0') << std::setw(2) << tm.tm_sec;
            
            if (milliseconds > 0) {
                ss << "." << std::setfill('0') << std::setw(3) << milliseconds;
            }
        }
        
        return ss.str();
    }

    // Get current price
    double CoinGeckoPriceRetriever::getCurrentPrice()
    {
        try {
            // Create HTTP request
            http::response<http::string_body> res = makeHttpRequest(
                "api.coingecko.com",
                "/api/v3/simple/price?ids=" + tokenId_ + "&vs_currencies=usd");

            // Parse the JSON response
            rapidjson::Document document;
            document.Parse(res.body().c_str());

            // Extract the price
            if (document.HasMember(tokenId_.c_str()) && 
                document[tokenId_.c_str()].HasMember("usd")) {
                return document[tokenId_.c_str()]["usd"].GetDouble();
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error getting current price: " << e.what() << std::endl;
        }

        return 0.0;
    }

    // Get historical prices for a list of timestamps
    std::map<int64_t, double> CoinGeckoPriceRetriever::getHistoricalPrices(const std::vector<int64_t>& timestamps)
    {
        std::map<int64_t, double> prices;
        
        try {
            // Get current timestamp for 365-day limit checking
            time_t now = std::time(nullptr);
            time_t oneYearAgo = now - (365 * 24 * 60 * 60);
            
            std::cout << "Current date: " << formatDate(now * 1000) << std::endl;
            std::cout << "One year ago: " << formatDate(oneYearAgo * 1000) << std::endl;
            
            // CoinGecko provides historical data in the format: /coins/{id}/history?date={date}
            // where date is DD-MM-YYYY
            for (int64_t timestamp : timestamps) {
                // Convert to seconds if in milliseconds
                time_t timestampSec = (timestamp > 9999999999) ? timestamp / 1000 : timestamp;
                
                // Skip timestamps older than 1 year due to CoinGecko restrictions
                if (timestampSec < oneYearAgo) {
                    std::cout << "Skipping " << formatDate(timestamp) 
                                << " - Beyond 365-day limit for CoinGecko free tier" << std::endl;
                    continue;
                }
                
                std::string formattedDate = formatDate(timestamp);
                
                // Create HTTP request for this specific date
                http::response<http::string_body> res = makeHttpRequest(
                    "api.coingecko.com",
                    "/api/v3/coins/" + tokenId_ + "/history?date=" + formattedDate);

                // Parse the JSON response
                rapidjson::Document document;
                document.Parse(res.body().c_str());

                // Extract the price
                if (document.HasMember("market_data") && 
                    document["market_data"].HasMember("current_price") &&
                    document["market_data"]["current_price"].HasMember("usd")) {
                    
                    double price = document["market_data"]["current_price"]["usd"].GetDouble();
                    prices[timestamp] = price;
                    
                    std::cout << "Historical price for " << formattedDate << ": $" << price << std::endl;
                } else {
                    std::cerr << "No price data found for " << formattedDate << std::endl;
                }
                
                // Respect rate limits - sleep between requests
                std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error getting historical prices: " << e.what() << std::endl;
        }

        return prices;
    }

    // Get historical price range (daily prices within a date range)
    std::map<int64_t, double> CoinGeckoPriceRetriever::getHistoricalPriceRange(int64_t from, int64_t to)
    {
        std::map<int64_t, double> prices;
        
        try {
            // Make sure we don't exceed the 365-day limit for free tier
            time_t now = std::time(nullptr);
            time_t oneYearAgo = now - (365 * 24 * 60 * 60);
            
            // Convert from milliseconds to seconds if needed
            time_t fromSec = (from > 9999999999) ? from / 1000 : from;
            
            if (fromSec < oneYearAgo) {
                std::cout << "Limiting historical request to past 365 days due to CoinGecko free tier restrictions" << std::endl;
                std::cout << "Adjusted from " << formatDate(from) << " to " << formatDate(oneYearAgo * 1000) << std::endl;
                fromSec = oneYearAgo;
            }
            
            // Convert timestamps to Unix timestamps in seconds
            int fromUnix = static_cast<int>(fromSec);
            int toUnix = static_cast<int>((to > 9999999999) ? to / 1000 : to);
            
            // Make sure from is before to (could happen with timestamp conversion issues)
            if (fromUnix >= toUnix) {
                std::cerr << "Error: 'from' date must be before 'to' date" << std::endl;
                std::cerr << "From: " << formatDate(from) << " (" << fromUnix << ")" << std::endl;
                std::cerr << "To: " << formatDate(to) << " (" << toUnix << ")" << std::endl;
                return prices;
            }
            
            std::string endpoint = "/api/v3/coins/" + tokenId_ + 
                                    "/market_chart/range?vs_currency=usd&from=" + 
                                    std::to_string(fromUnix) + "&to=" + std::to_string(toUnix);
            
            std::cout << "CoinGecko request: " << endpoint << std::endl;
            
            // Create HTTP request for the range
            http::response<http::string_body> res = makeHttpRequest(
                "api.coingecko.com", endpoint);

            // Parse the JSON response
            rapidjson::Document document;
            document.Parse(res.body().c_str());

            // Extract the prices array
            if (document.HasMember("prices") && document["prices"].IsArray()) {
                const auto& pricesArray = document["prices"];
                
                for (rapidjson::SizeType i = 0; i < pricesArray.Size(); i++) {
                    if (pricesArray[i].IsArray() && pricesArray[i].Size() >= 2) {
                        int64_t timestamp = static_cast<int64_t>(pricesArray[i][0].GetDouble());
                        double price = pricesArray[i][1].GetDouble();
                        prices[timestamp] = price;
                    }
                }
                
                std::cout << "Retrieved " << prices.size() << " historical price points" << std::endl;
            } else {
                std::cerr << "No price data found for the specified range" << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error getting historical price range: " << e.what() << std::endl;
        }

        return prices;
    }

    std::string CoinGeckoPriceRetriever::getTokenSymbol()
    {
        try {
            // Create HTTP request
            http::response<http::string_body> res = makeHttpRequest(
                "api.coingecko.com",
                "/api/v3/coins/" + tokenId_);

            // Parse the JSON response
            rapidjson::Document document;
            document.Parse(res.body().c_str());

            // Extract the symbol
            if (document.HasMember("symbol")) {
                return document["symbol"].GetString();
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error getting token symbol: " << e.what() << std::endl;
        }

        return tokenId_;
    }
    
    // Helper method to make HTTP requests
    http::response<http::string_body> CoinGeckoPriceRetriever::makeHttpRequest(
        const std::string& host, const std::string& target)
    {
        try {
            // Set up the SSL context
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();

            // Set up the IO context and resolver
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            
            // Resolve the host
            auto const results = resolver.resolve(host, "443");

            // Set up the SSL stream
            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
            
            // Set SNI Hostname
            if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            {
                beast::error_code ec{static_cast<int>(::ERR_get_error()), 
                                    net::error::get_ssl_category()};
                throw beast::system_error{ec};
            }

            // Set a timeout on the operation
            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));

            // Connect to the host
            beast::get_lowest_layer(stream).connect(results);
            
            // Set a timeout on the operation
            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
            
            // Perform the SSL handshake
            stream.handshake(ssl::stream_base::client);

            // Set up the HTTP request
            http::request<http::string_body> req{http::verb::get, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            // Set a timeout on the operation
            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
            
            // Send the HTTP request
            http::write(stream, req);

            // Receive the HTTP response
            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            
            // Set a timeout on the operation
            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
            
            http::read(stream, buffer, res);

            // Check response status
            if (res.result_int() != 200) {
                std::cerr << "HTTP error: " << res.result_int() << " " 
                        << res.reason() << std::endl;
                std::cerr << "Target: " << target << std::endl;
                std::cerr << "Response body: " << res.body() << std::endl;
                throw std::runtime_error("HTTP request failed with status " + 
                                        std::to_string(res.result_int()));
            }

            // Gracefully close the socket - important to avoid "stream truncated" errors
            beast::error_code ec;
            beast::get_lowest_layer(stream).socket().shutdown(tcp::socket::shutdown_both, ec);
            
            return res;
        }
        catch (const beast::error_code& ec) {
            std::cerr << "Beast error: " << ec.message() << std::endl;
            throw;
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in makeHttpRequest: " << e.what() << std::endl;
            throw;
        }
    }

}
