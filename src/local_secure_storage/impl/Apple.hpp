#pragma once

#include "../ISecureStorage.hpp"

#include <rapidjson/document.h>

namespace sgns {
    class AppleSecureStorage: public ISecureStorage {

    public:
        AppleSecureStorage() = default;

        ~AppleSecureStorage() override = default;

        std::string GetName() override
        {
            return "AppleSecureStorage";
        }

        outcome::result<SecureBufferType> Load( const std::string &key ) override;

        outcome::result<void> Save( const std::string &key, const SecureBufferType &buffer ) override;

        outcome::result<bool> DeleteKey( const std::string &key ) override;

    private:
        outcome::result<rapidjson::Document> LoadJSON() const;

        outcome::result<void> SaveJSON( rapidjson::Document document );
    };
}