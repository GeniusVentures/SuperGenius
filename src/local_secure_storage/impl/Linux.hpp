#pragma once

#include "JSONBackend.hpp"

#include <libsecret/secret.h>

namespace sgns
{
    class LinuxSecureStorage : public JSONBackend
    {
    public:
        explicit LinuxSecureStorage();

        ~LinuxSecureStorage() override = default;

        std::string GetName() override
        {
            return "LinuxSecureStorage";
        }

    protected:
        outcome::result<rapidjson::Document> LoadJSON() const override;

        outcome::result<void> SaveJSON( rapidjson::Document document ) override;

    private:
        SecretSchema schema;
    };
}
