/**
 * @file       ISecureStorage.hpp
 * @brief      Secure Storage Interface class
 * @date       2024-06-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _I_SECURE_STORAGE_HPP_
#define _I_SECURE_STORAGE_HPP_

#include <string>
#include <vector>
#include <cstdint>
#include "outcome/outcome.hpp"
#include "singleton/IComponent.hpp"

namespace sgns
{
    class ISecureStorage : public IComponent, public std::enable_shared_from_this<ISecureStorage>
    {
    public:
        virtual ~ISecureStorage() = default;

        using SecureBufferType = std::string;

        virtual outcome::result<SecureBufferType> Load( const std::string &key, const std::string directory = "" ) = 0;
        virtual outcome::result<void>             Save( const std::string &key, const SecureBufferType &buffer, const std::string directory = "" )   = 0;
    };
}

#endif
