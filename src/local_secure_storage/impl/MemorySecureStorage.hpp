/**
 * @file       MemorySecureStorage.hpp
 * @brief      In-memory JSON backend for testing — no keychain access.
 * @date       2026-06-01
 */
#ifndef MEMORY_SECURE_STORAGE_HPP
#define MEMORY_SECURE_STORAGE_HPP

#include "JSONBackend.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <string>
#include <unordered_map>

namespace sgns
{
    /**
     * @brief In-memory JSON backend for tests. Stores data in a map — no OS
     *        keychain access, no password prompts, no cleanup needed.
     */
    class MemorySecureStorage : public JSONBackend
    {
    public:
        explicit MemorySecureStorage( std::string identifier ) : identifier_( std::move( identifier ) )
        {
        }

        std::string GetName() override
        {
            return "MemorySecureStorage";
        }

        outcome::result<rapidjson::Document> LoadJSON() const override
        {
            auto it = store_.find( identifier_ );
            if ( it == store_.end() )
            {
                return rapidjson::Document( rapidjson::Type::kObjectType );
            }

            rapidjson::Document d;
            d.Parse( it->second.c_str(), it->second.size() );
            if ( d.HasParseError() )
            {
                return outcome::failure( std::errc::bad_message );
            }
            return d;
        }

        outcome::result<void> SaveJSON( rapidjson::Document document ) override
        {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer       writer( buffer );
            document.Accept( writer );
            store_[identifier_] = std::string( buffer.GetString(), buffer.GetLength() );
            return outcome::success();
        }

    private:
        std::string                                                identifier_;
        static inline std::unordered_map<std::string, std::string> store_;
    };
} // namespace sgns

#endif // MEMORY_SECURE_STORAGE_HPP
