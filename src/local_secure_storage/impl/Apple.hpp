#pragma once

#include "JSONBackend.hpp"

namespace sgns
{
    class AppleSecureStorage : public JSONBackend
    {
    public:
        AppleSecureStorage() = default;

        ~AppleSecureStorage() override = default;

        std::string GetName() override
        {
            return "AppleSecureStorage";
        }

    protected:
        outcome::result<rapidjson::Document> LoadJSON() const override;

        outcome::result<void> SaveJSON( rapidjson::Document document ) override;
    };
}
