#include "coinprices.hpp"
#include <rapidjson/document.h>
#include "FileManager.hpp"

namespace sgns
{
    CoinGeckoPriceRetriever::CoinGeckoPriceRetriever() 
    {
    }
    std::string decodeChunkedTransfer(const std::string& chunkedData) {
        std::string result;
        std::istringstream iss(chunkedData);
        std::string line;
        
        while (std::getline(iss, line)) {
            // Trim carriage returns if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            // Skip empty lines
            if (line.empty()) {
                continue;
            }
            
            // Try to parse as a hex chunk length
            std::istringstream hexStream(line);
            unsigned int chunkSize;
            if (hexStream >> std::hex >> chunkSize) {
                // If chunk size is 0, we're done
                if (chunkSize == 0) {
                    break;
                }
                
                // Read exactly chunkSize bytes
                char* buffer = new char[chunkSize + 1];
                iss.read(buffer, chunkSize);
                buffer[chunkSize] = '\0';
                
                // Append to result
                result.append(buffer, chunkSize);
                delete[] buffer;
                
                // Skip the trailing CRLF after the chunk
                iss.ignore(2);
            }
            else {
                // Not a valid hex chunk size, maybe part of the actual data
                result += line + "\n";
            }
        }
        
        return result;
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
    std::map<std::string, double> CoinGeckoPriceRetriever::getCurrentPrices(
        const std::vector<std::string>& tokenIds)
    {
        std::map<std::string, double> prices;

        if (tokenIds.empty()) {
            return prices;
        }

        try {
            // Join the token IDs with commas for the API request
            std::string tokenIdsList;
            for (size_t i = 0; i < tokenIds.size(); i++) {
                tokenIdsList += tokenIds[i];
                if (i < tokenIds.size() - 1) {
                    tokenIdsList += ",";
                }
            }
            m_logger->debug("Token IDS: {}", tokenIdsList);
            // Create HTTP request
            auto ioc = std::make_shared<boost::asio::io_context>();
            boost::asio::io_context::executor_type                                   executor = ioc->get_executor();
            boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard( executor );
            std::string url = "https://api.coingecko.com/api/v3/simple/price?ids=" + tokenIdsList + "&vs_currencies=usd";
            FileManager::GetInstance().InitializeSingletons();
            std::string res;
            auto result = FileManager::GetInstance().LoadASync(
                url,
                false,
                false,
                ioc,
                []( const sgns::AsyncError::CustomResult &status )
                {
                    if ( status.has_value() )
                    {
                        std::cout << "Success: " << status.value().message << std::endl;
                    }
                    else
                    {
                        std::cout << "Error: " << status.error() << std::endl;
                    }
                },
                [&res]( std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers )
                {
                    if (!buffers->second.empty()) {
                        res = std::string(buffers->second[0].begin(), buffers->second[0].end());
                    }                    
                },
                "file" );
            ioc->run();
            m_logger->debug("Res Is: {}", res);
            std::string json_str = decodeChunkedTransfer(res);

            // Parse the JSON response
            rapidjson::Document document;
            document.Parse(json_str.c_str());

            // Extract the prices for each token
            for (const auto& tokenId : tokenIds) {
                if (document.HasMember(tokenId.c_str()) &&
                    document[tokenId.c_str()].HasMember("usd")) {
                    prices[tokenId] = document[tokenId.c_str()]["usd"].GetDouble();
                }
            }
        }
        catch (const std::exception& e) {
            m_logger->error("Error getting current prices: {}", e.what());
        }

        return prices;
    }


    // Get historical prices for a list of timestamps
    std::map<std::string, std::map<int64_t, double>> CoinGeckoPriceRetriever::getHistoricalPrices(
        const std::vector<std::string>& tokenIds,
        const std::vector<int64_t>& timestamps)
    {
        std::map<std::string, std::map<int64_t, double>> allPrices;

        if (tokenIds.empty() || timestamps.empty()) {
            return allPrices;
        }

        try {
            // Get current timestamp for 365-day limit checking
            time_t now = std::time(nullptr);
            time_t oneYearAgo = now - (365 * 24 * 60 * 60);


            // Process each timestamp for all tokens
            for (int64_t timestamp : timestamps) {
                // Convert to seconds if in milliseconds
                time_t timestampSec = (timestamp > 9999999999) ? timestamp / 1000 : timestamp;

                // Skip timestamps older than 1 year due to CoinGecko restrictions
                if (timestampSec < oneYearAgo) {
                    m_logger->error("Skipping {}", formatDate(timestamp));
                    continue;
                }

                std::string formattedDate = formatDate(timestamp);

                // Process each token for this timestamp
                for (const auto& tokenId : tokenIds) {
                    // Create HTTP request for this specific date and token
                    // http::response<http::string_body> res = makeHttpRequest(
                    //     "api.coingecko.com",
                    //     "/api/v3/coins/" + tokenId + "/history?date=" + formattedDate);
                    // Create HTTP request
                    auto ioc = std::make_shared<boost::asio::io_context>();
                    boost::asio::io_context::executor_type                                   executor = ioc->get_executor();
                    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard( executor );
                    std::string url = "https://api.coingecko.com/api/v3/coins/" + tokenId + "/history?date=" + formattedDate;
                    FileManager::GetInstance().InitializeSingletons();
                    std::string res;
                    auto result = FileManager::GetInstance().LoadASync(
                        url,
                        false,
                        false,
                        ioc,
                        []( const sgns::AsyncError::CustomResult &status )
                        {
                            if ( status.has_value() )
                            {
                                std::cout << "Success: " << status.value().message << std::endl;
                            }
                            else
                            {
                                std::cout << "Error: " << status.error() << std::endl;
                            }
                        },
                        [&res]( std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers )
                        {
                            if (!buffers->second.empty()) {
                                res = std::string(buffers->second[0].begin(), buffers->second[0].end());
                            }                    
                        },
                        "file" );
                    ioc->run();
                    m_logger->debug("Res Is: {}", res);
                    std::string json_str = decodeChunkedTransfer(res);
                    // Parse the JSON response
                    rapidjson::Document document;
                    document.Parse(json_str.c_str());

                    // Extract the price
                    if (document.HasMember("market_data") &&
                        document["market_data"].HasMember("current_price") &&
                        document["market_data"]["current_price"].HasMember("usd")) {

                        double price = document["market_data"]["current_price"]["usd"].GetDouble();
                        allPrices[tokenId][timestamp] = price;
                    }
                    else {
                        m_logger->error("No price data found for {} on {}", tokenId, formattedDate);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            m_logger->error("Error getting historical prices: {}", e.what());
        }

        return allPrices;
    }

    // Get historical price range (daily prices within a date range)
    std::map<std::string, std::map<int64_t, double>> CoinGeckoPriceRetriever::getHistoricalPriceRange(
        const std::vector<std::string>& tokenIds,
        int64_t from,
        int64_t to)
    {
        std::map<std::string, std::map<int64_t, double>> allPrices;

        if (tokenIds.empty()) {
            return allPrices;
        }

        try {
            // Make sure we don't exceed the 365-day limit for free tier
            time_t now = std::time(nullptr);
            time_t oneYearAgo = now - (365 * 24 * 60 * 60);

            // Convert from milliseconds to seconds if needed
            time_t fromSec = (from > 9999999999) ? from / 1000 : from;

            if (fromSec < oneYearAgo) {
                fromSec = oneYearAgo;
            }

            // Convert timestamps to Unix timestamps in seconds
            int fromUnix = static_cast<int>(fromSec);
            int toUnix = static_cast<int>((to > 9999999999) ? to / 1000 : to);

            // Make sure from is before to (could happen with timestamp conversion issues)
            if (fromUnix >= toUnix) {
                m_logger->error("Error: 'from' date must be before 'to' date");
                return allPrices;
            }

            // Process each token
            for (const auto& tokenId : tokenIds) {
                // std::string endpoint = "/api/v3/coins/" + tokenId +
                //     "/market_chart/range?vs_currency=usd&from=" +
                //     std::to_string(fromUnix) + "&to=" + std::to_string(toUnix);

                m_logger->debug("CoinGecko request for {}", tokenId);

                // Create HTTP request for the range
                // http::response<http::string_body> res = makeHttpRequest(
                //     "api.coingecko.com", endpoint);
                // Create HTTP request
                auto ioc = std::make_shared<boost::asio::io_context>();
                boost::asio::io_context::executor_type                                   executor = ioc->get_executor();
                boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard( executor );
                std::string url = "https://api.coingecko.com/api/v3/coins/" + tokenId + "/market_chart/range?vs_currency=usd&from=" + std::to_string(fromUnix) + "&to=" + std::to_string(toUnix);
                FileManager::GetInstance().InitializeSingletons();
                std::string res;
                auto result = FileManager::GetInstance().LoadASync(
                    url,
                    false,
                    false,
                    ioc,
                    []( const sgns::AsyncError::CustomResult &status )
                    {
                        if ( status.has_value() )
                        {
                            std::cout << "Success: " << status.value().message << std::endl;
                        }
                        else
                        {
                            std::cout << "Error: " << status.error() << std::endl;
                        }
                    },
                    [&res]( std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers )
                    {
                        if (!buffers->second.empty()) {
                            res = std::string(buffers->second[0].begin(), buffers->second[0].end());
                        }                    
                    },
                    "file" );
                ioc->run();
                m_logger->debug("Res Is: {}", res);
                std::string json_str = decodeChunkedTransfer(res);
                // Parse the JSON response
                rapidjson::Document document;
                document.Parse(json_str.c_str());
                if (document.HasParseError()) {
                    m_logger->error("JSON Parse Error: {}", document.GetParseError());
                    return allPrices;
                }
            
                if (!document.IsObject()) {
                    m_logger->error("JSON is not an object!");
                    return allPrices;
                }
            
                // Extract the prices array
                if (document.HasMember("prices") && document["prices"].IsArray()) {
                    const auto& pricesArray = document["prices"];

                    for (rapidjson::SizeType i = 0; i < pricesArray.Size(); i++) {
                        if (pricesArray[i].IsArray() && pricesArray[i].Size() >= 2) {
                            int64_t timestamp = static_cast<int64_t>(pricesArray[i][0].GetDouble());
                            double price = pricesArray[i][1].GetDouble();
                            allPrices[tokenId][timestamp] = price;
                        }
                    }

                }
                else {
                    m_logger->error("No price data found for {} in the specified range", tokenId);
                }

                // Respect rate limits between tokens
                if (&tokenId != &tokenIds.back()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
                }
            }
        }
        catch (const std::exception& e) {
            m_logger->error("Error getting historical price range: {}", e.what());
        }

        return allPrices;
    }
}
