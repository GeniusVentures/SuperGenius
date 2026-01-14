#pragma once

#include "JSONBackend.hpp"

#include <libsecret/secret.h>

namespace sgns
{
    class LinuxSecureStorage : public JSONBackend
    {
    public:
        explicit LinuxSecureStorage(std::string identifier);

        ~LinuxSecureStorage() override = default;

        std::string GetName() override
        {
            return "LinuxSecureStorage";
        }

        outcome::result<rapidjson::Document> LoadJSON() const override;

        outcome::result<void> SaveJSON( rapidjson::Document document ) override;

    private:
        std::string identifier_;
        SecretSchema schema_;
    };
}
