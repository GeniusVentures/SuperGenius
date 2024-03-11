/**
 * @file       ED25519ProviderFactory.hpp
 * @brief      
 * @date       2024-03-04
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ED25519_PROVIDER_FACTORY_HPP_
#define _ED25519_PROVIDER_FACTORY_HPP_

#include "crypto/ed25519/ed25519_provider_impl.hpp"

namespace sgns
{

    class ED25519ProviderFactory
    {
    public:
        std::shared_ptr<crypto::ED25519Provider> create()
        {
            return std::make_shared<crypto::ED25519ProviderImpl>();
        }
    };
}

#endif
