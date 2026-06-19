#ifndef WINDOWS_SECURE_STORAGE_HPP
#define WINDOWS_SECURE_STORAGE_HPP

#include "JSONBackend.hpp"

#include <rapidjson/document.h>

namespace sgns
{
    class WindowsSecureStorage : public JSONBackend
    {
    public:
        explicit WindowsSecureStorage( std::string identifier );

        std::string GetName() override
        {
            return "WindowsSecureStorage";
        }

        outcome::result<rapidjson::Document> LoadJSON() const override;

        outcome::result<void> SaveJSON( rapidjson::Document document ) override;

    private:
        std::string identifier_;
    };
}

#endif // WINDOWS_SECURE_STORAGE_HPP
