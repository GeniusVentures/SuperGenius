#pragma once

#include "JSONBackend.hpp"

#include <rapidjson/document.h>

namespace sgns
{
    class WindowsSecureStorage : public JSONBackend
    {
    public:
        std::string GetName() override
        {
            return "WindowsSecureStorage";
        }

    protected:
        outcome::result<rapidjson::Document> LoadJSON() const override;

        outcome::result<void> SaveJSON( rapidjson::Document document ) override;
    };
}
