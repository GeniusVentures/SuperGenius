#ifndef SGNS_APPLE_SECURE_STORAGE_HPP
#define SGNS_APPLE_SECURE_STORAGE_HPP

#include "JSONBackend.hpp"

namespace sgns
{
    class AppleSecureStorage : public JSONBackend
    {
    public:
        explicit AppleSecureStorage( std::string identifier );

        std::string GetName() override
        {
            return "AppleSecureStorage";
        }

        outcome::result<rapidjson::Document> LoadJSON() const override;

        outcome::result<void> SaveJSON( rapidjson::Document document ) override;

    private:
        std::string identifier_;
    };
}
#endif // SGNS_APPLE_SECURE_STORAGE_HPP
