#pragma once

#include "../ISecureStorage.hpp"

#include <libsecret/secret.h>
#include <rapidjson/document.h>

namespace sgns
{
    class LinuxSecureStorage : public ISecureStorage
    {
    public:
        explicit LinuxSecureStorage();

        ~LinuxSecureStorage() override = default;

        std::string GetName() override
        {
            return "LinuxSecureStorage";
        }

        outcome::result<SecureBufferType> Load( const std::string &key ) override;

        outcome::result<void> Save( const std::string &key, const SecureBufferType &buffer ) override;

        outcome::result<bool> DeleteKey( const std::string &key ) override;

    private:
        outcome::result<rapidjson::Document> LoadJSON() const;

        outcome::result<void> SaveJSON( rapidjson::Document document );

        SecretSchema schema;
    };
}
