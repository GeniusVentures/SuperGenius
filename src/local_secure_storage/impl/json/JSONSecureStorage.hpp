/**
 * @file       JSONSecureStorage.hpp
 * @brief      
 * @date       2024-06-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef JSON_SECURE_STORAGE_HPP
#define JSON_SECURE_STORAGE_HPP

#include "../../ISecureStorage.hpp"

#include <rapidjson/document.h>
#include <boost/filesystem.hpp>

namespace sgns
{
    namespace rj = rapidjson;

    class JSONSecureStorage : public ISecureStorage
    {
    public:
        JSONSecureStorage( boost::filesystem::path directory ) : directory_( std::move( directory ) )
        {
        }

        ~JSONSecureStorage() override = default;

        outcome::result<SecureBufferType> Load( const std::string &key ) override;

        outcome::result<void> Save( const std::string &key, const SecureBufferType &buffer ) override;

        outcome::result<bool> DeleteKey( const std::string &key ) override;

        std::string GetName() override
        {
            return "LocalSecureStorage";
        }

        outcome::result<rj::Document> LoadJSON() const;

        static JSONSecureStorage &GetInstance();

    private:
        boost::filesystem::path directory_;
    };
}

#endif // JSON_SECURE_STORAGE_HPP
