/**
 * @file       ED25519KeyPairFactory.hpp
 * @brief      
 * @date       2024-03-04
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ED25519_KEYPAIR_FACTORY_HPP_
#define _ED25519_KEYPAIR_FACTORY_HPP_
#include "crypto/ed25519_types.hpp"

namespace sgns
{
    class ED25519KeyPairFactory
    {
    public:
        std::shared_ptr<crypto::ED25519Keypair> create()
        {
            return std::make_shared<crypto::ED25519Keypair>();
        }
    };
}

#endif
