#include "coinprices.hpp"
#include <rapidjson/document.h>
#include "FileManager.hpp"
OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, CoinGeckoPriceRetriever::PriceError, e )
{
    switch ( e )
    {
        case sgns::CoinGeckoPriceRetriever::PriceError::EmptyInput:
            return "Empty Input";
        case sgns::CoinGeckoPriceRetriever::PriceError::NetworkError:
            return "Network Error";
        case sgns::CoinGeckoPriceRetriever::PriceError::JsonParseError:
            return "Json Parse Error";
        case sgns::CoinGeckoPriceRetriever::PriceError::NoDataFound:
            return "No Data";
        case sgns::CoinGeckoPriceRetriever::PriceError::RateLimitExceeded:
            return "Rate limit exceeded";
        case sgns::CoinGeckoPriceRetriever::PriceError::DateTooOld:
            return "Date exceeds year limit";
    }
    return "Unknown error";
}
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

    // Get current price with boost::outcome
    outcome::result<std::map<std::string, double>> CoinGeckoPriceRetriever::getCurrentPrices(
        const std::vector<std::string>& tokenIds)
    {
        std::map<std::string, double> prices;

        if (tokenIds.empty()) {
            return outcome::failure(PriceError::EmptyInput);
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
            boost::asio::io_context::executor_type executor = ioc->get_executor();
            boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(executor);
            std::string url = "https://api.coingecko.com/api/v3/simple/price?ids=" + tokenIdsList + "&vs_currencies=usd";
            FileManager::GetInstance().InitializeSingletons();
            std::string res;
            bool requestSucceeded = true;

            auto result = FileManager::GetInstance().LoadASync(
                url,
                false,
                false,
                ioc,
                [&requestSucceeded](const sgns::AsyncError::CustomResult &status)
                {
                    if (status.has_value()) {
                        std::cout << "Success: " << status.value().message << std::endl;
                    } else {
                        std::cout << "Error: " << status.error() << std::endl;
                        requestSucceeded = false;
                    }
                },
                [&res](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
                {
                    if (!buffers->second.empty()) {
                        res = std::string(buffers->second[0].begin(), buffers->second[0].end());
                    }                    
                },
                "file");
            
            ioc->run();
            
            if (!requestSucceeded) {
                return outcome::failure(PriceError::NetworkError);
            }
            
            m_logger->debug("Res Is: {}", res);
            std::string json_str = decodeChunkedTransfer(res);

            // Parse the JSON response
            rapidjson::Document document;
            document.Parse(json_str.c_str());
            
            if (document.HasParseError()) {
                m_logger->error("JSON Parse Error: {}", document.GetParseError());
                return outcome::failure(PriceError::JsonParseError);
            }

            // Check if the response contains an error message about rate limits
            if (document.IsObject() && document.HasMember("status") && 
                document["status"].IsObject() && document["status"].HasMember("error_code")) {
                int error_code = document["status"]["error_code"].GetInt();
                if (error_code == 429) {
                    return outcome::failure(PriceError::RateLimitExceeded);
                }
            }

            // Extract the prices for each token
            bool foundAnyPrice = false;
            for (const auto& tokenId : tokenIds) {
                if (document.HasMember(tokenId.c_str()) &&
                    document[tokenId.c_str()].HasMember("usd")) {
                    prices[tokenId] = document[tokenId.c_str()]["usd"].GetDouble();
                    foundAnyPrice = true;
                }
            }
            
            if (!foundAnyPrice) {
                return outcome::failure(PriceError::NoDataFound);
            }
        }
        catch (const std::exception& e) {
            m_logger->error("Error getting current prices: {}", e.what());
            return outcome::failure(PriceError::NetworkError);
        }

        return outcome::success(prices);
    }


    // Get historical prices for a list of timestamps with boost::outcome
    outcome::result<std::map<std::string, std::map<int64_t, double>>> CoinGeckoPriceRetriever::getHistoricalPrices(
        const std::vector<std::string>& tokenIds,
        const std::vector<int64_t>& timestamps)
    {
        std::map<std::string, std::map<int64_t, double>> allPrices;

        if (tokenIds.empty() || timestamps.empty()) {
            return outcome::failure(PriceError::EmptyInput);
        }

        try {
            // Get current timestamp for 365-day limit checking
            time_t now = std::time(nullptr);
            time_t oneYearAgo = now - (365 * 24 * 60 * 60);

            bool allTooOld = true;
            
            // Process each timestamp for all tokens
            for (int64_t timestamp : timestamps) {
                // Convert to seconds if in milliseconds
                time_t timestampSec = (timestamp > 9999999999) ? timestamp / 1000 : timestamp;

                // Skip timestamps older than 1 year due to CoinGecko restrictions
                if (timestampSec < oneYearAgo) {
                    m_logger->error("Skipping {}", formatDate(timestamp));
                    continue;
                }
                
                allTooOld = false;
                std::string formattedDate = formatDate(timestamp);

                // Process each token for this timestamp
                for (const auto& tokenId : tokenIds) {
                    // Create HTTP request
                    auto ioc = std::make_shared<boost::asio::io_context>();
                    boost::asio::io_context::executor_type executor = ioc->get_executor();
                    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(executor);
                    std::string url = "https://api.coingecko.com/api/v3/coins/" + tokenId + "/history?date=" + formattedDate;
                    FileManager::GetInstance().InitializeSingletons();
                    std::string res;
                    bool requestSucceeded = true;
                    
                    auto result = FileManager::GetInstance().LoadASync(
                        url,
                        false,
                        false,
                        ioc,
                        [&requestSucceeded](const sgns::AsyncError::CustomResult &status)
                        {
                            if (status.has_value()) {
                                std::cout << "Success: " << status.value().message << std::endl;
                            } else {
                                std::cout << "Error: " << status.error() << std::endl;
                                requestSucceeded = false;
                            }
                        },
                        [&res](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
                        {
                            if (!buffers->second.empty()) {
                                res = std::string(buffers->second[0].begin(), buffers->second[0].end());
                            }                    
                        },
                        "file");
                        
                    ioc->run();
                    
                    if (!requestSucceeded) {
                        continue; // Try the next token instead of failing completely
                    }
                    
                    m_logger->debug("Res Is: {}", res);
                    std::string json_str = decodeChunkedTransfer(res);
                    
                    // Parse the JSON response
                    rapidjson::Document document;
                    document.Parse(json_str.c_str());
                    
                    if (document.HasParseError()) {
                        m_logger->error("JSON Parse Error: {}", document.GetParseError());
                        continue; // Try the next token
                    }
                    
                    // Check if the response contains an error message about rate limits
                    if (document.IsObject() && document.HasMember("status") && 
                        document["status"].IsObject() && document["status"].HasMember("error_code")) {
                        int error_code = document["status"]["error_code"].GetInt();
                        if (error_code == 429) {
                            return outcome::failure(PriceError::RateLimitExceeded);
                        }
                    }

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
                    
                    // Respect rate limits between tokens
                    if (&tokenId != &tokenIds.back()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
                    }
                }
            }
            
            if (allTooOld) {
                return outcome::failure(PriceError::DateTooOld);
            }
            
            if (allPrices.empty()) {
                return outcome::failure(PriceError::NoDataFound);
            }
        }
        catch (const std::exception& e) {
            m_logger->error("Error getting historical prices: {}", e.what());
            return outcome::failure(PriceError::NetworkError);
        }

        return outcome::success(allPrices);
    }

    // Get historical price range with boost::outcome
    outcome::result<std::map<std::string, std::map<int64_t, double>>> CoinGeckoPriceRetriever::getHistoricalPriceRange(
        const std::vector<std::string>& tokenIds,
        int64_t from,
        int64_t to)
    {
        std::map<std::string, std::map<int64_t, double>> allPrices;

        if (tokenIds.empty()) {
            return outcome::failure(PriceError::EmptyInput);
        }

        try {
            // Make sure we don't exceed the 365-day limit for free tier
            time_t now = std::time(nullptr);
            time_t oneYearAgo = now - (365 * 24 * 60 * 60);

            // Convert from milliseconds to seconds if needed
            time_t fromSec = (from > 9999999999) ? from / 1000 : from;

            bool dateTooOld = false;
            if (fromSec < oneYearAgo) {
                fromSec = oneYearAgo;
                dateTooOld = true;
            }

            // Convert timestamps to Unix timestamps in seconds
            int fromUnix = static_cast<int>(fromSec);
            int toUnix = static_cast<int>((to > 9999999999) ? to / 1000 : to);

            // Make sure from is before to (could happen with timestamp conversion issues)
            if (fromUnix >= toUnix) {
                m_logger->error("Error: 'from' date must be before 'to' date");
                return outcome::failure(PriceError::EmptyInput); // Using EmptyInput for invalid date range
            }

            // Process each token
            for (const auto& tokenId : tokenIds) {
                m_logger->debug("CoinGecko request for {}", tokenId);

                // Create HTTP request
                auto ioc = std::make_shared<boost::asio::io_context>();
                boost::asio::io_context::executor_type executor = ioc->get_executor();
                boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(executor);
                std::string url = "https://api.coingecko.com/api/v3/coins/" + tokenId + "/market_chart/range?vs_currency=usd&from=" + std::to_string(fromUnix) + "&to=" + std::to_string(toUnix);
                FileManager::GetInstance().InitializeSingletons();
                std::string res;
                bool requestSucceeded = true;
                
                auto result = FileManager::GetInstance().LoadASync(
                    url,
                    false,
                    false,
                    ioc,
                    [&requestSucceeded](const sgns::AsyncError::CustomResult &status)
                    {
                        if (status.has_value()) {
                            std::cout << "Success: " << status.value().message << std::endl;
                        } else {
                            std::cout << "Error: " << status.error() << std::endl;
                            requestSucceeded = false;
                        }
                    },
                    [&res](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
                    {
                        if (!buffers->second.empty()) {
                            res = std::string(buffers->second[0].begin(), buffers->second[0].end());
                        }                    
                    },
                    "file");
                    
                ioc->run();
                
                if (!requestSucceeded) {
                    continue; // Try the next token instead of failing completely
                }
                
                m_logger->debug("Res Is: {}", res);
                std::string json_str = decodeChunkedTransfer(res);
                
                // Parse the JSON response
                rapidjson::Document document;
                document.Parse(json_str.c_str());
                
                if (document.HasParseError()) {
                    m_logger->error("JSON Parse Error: {}", document.GetParseError());
                    continue; // Try the next token
                }
                
                if (!document.IsObject()) {
                    m_logger->error("JSON is not an object!");
                    continue; // Try the next token
                }
                
                // Check if the response contains an error message about rate limits
                if (document.HasMember("status") && 
                    document["status"].IsObject() && document["status"].HasMember("error_code")) {
                    int error_code = document["status"]["error_code"].GetInt();
                    if (error_code == 429) {
                        return outcome::failure(PriceError::RateLimitExceeded);
                    }
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
            
            if (allPrices.empty()) {
                return outcome::failure(PriceError::NoDataFound);
            }
            
            // If date was too old and we adjusted it, we should indicate this to the caller
            if (dateTooOld) {
                // We still return the data, but with a warning via the PriceError
                // Ideally this would be implemented as a custom "success with info" type in outcome
                m_logger->warn("Some dates were too old and were adjusted to one year ago limit");
            }
        }
        catch (const std::exception& e) {
            m_logger->error("Error getting historical price range: {}", e.what());
            return outcome::failure(PriceError::NetworkError);
        }

        return outcome::success(allPrices);
    }
}