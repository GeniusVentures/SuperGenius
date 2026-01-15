/**
 * @file       JSONSecureStorage.hpp
 * @brief      
 * @date       2024-06-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once

#include "../../ISecureStorage.hpp"

#include <rapidjson/document.h>

namespace sgns
{
    namespace rj = rapidjson;

    class JSONSecureStorage : public ISecureStorage
    {
    public:
        JSONSecureStorage( std::string directory ) : directory_( std::move( directory ) ) {}

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
        std::string directory_;
    };
}
