/**
 * @file       JSONSecureStorage.hpp
 * @brief      
 * @date       2024-06-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once

#include "../../ISecureStorage.hpp"

namespace sgns
{

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

        static JSONSecureStorage &GetInstance();

    private:
        std::string directory_;
    };
}
