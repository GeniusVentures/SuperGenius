#ifndef SGNS_JSON_BACKEND_HPP
#define SGNS_JSON_BACKEND_HPP
#include "../ISecureStorage.hpp"

#include <rapidjson/document.h>

namespace sgns
{
    class JSONBackend : public ISecureStorage
    {
    public:
        JSONBackend() = default;

        ~JSONBackend() override = default;

        std::string GetName() override
        {
            return "JSONBackend";
        }

        outcome::result<SecureBufferType> Load( const std::string &key ) override;

        outcome::result<void> Save( const std::string &key, const SecureBufferType &buffer ) override;

        outcome::result<bool> DeleteKey( const std::string &key ) override;

        virtual outcome::result<rapidjson::Document> LoadJSON() const = 0;

        virtual outcome::result<void> SaveJSON( rapidjson::Document document ) = 0;
    };
}

#endif // SGNS_JSON_BACKEND_HPP
