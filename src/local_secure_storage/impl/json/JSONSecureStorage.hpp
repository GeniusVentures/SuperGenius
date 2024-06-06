/**
 * @file       JSONSecureStorage.hpp
 * @brief      
 * @date       2024-06-06
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _JSON_SECURE_STORAGE_HPP_
#define _JSON_SECURE_STORAGE_HPP_

#include "local_secure_storage/ISecureStorage.hpp"

namespace sgns
{

    class JSONSecureStorage : public ISecureStorage
    {
    public:
        ~JSONSecureStorage() = default;
        outcome::result<SecureBufferType> Load( const std::string &key ) override;
        outcome::result<void>             Save( const std::string &key, const SecureBufferType &buffer ) override;

        std::string GetName() override
        {
            return "JSONSecureStorage";
        }

        static JSONSecureStorage &GetInstance();

    private:
        friend struct JSONRegister;
        static void RegisterComponent();
        JSONSecureStorage() = default;
        static JSONSecureStorage Create();
    };

    struct JSONRegister
    {
        JSONRegister()
        {
            JSONSecureStorage::RegisterComponent();
        }
    };

    static JSONRegister JSONRegister;
}

#endif
