/**
 * @file       SR25519KeypairFactory.hpp
 * @brief      
 * @date       2024-03-01
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _SR25519_KEYPAIR_FACTORY_HPP_
#define _SR25519_KEYPAIR_FACTORY_HPP_

#include "crypto/sr25519_types.hpp"

namespace sgns
{

    class SR25519KeypairFactory
    {
    public:
        std::shared_ptr<crypto::SR25519Keypair> create()
        {
            return std::make_shared<crypto::SR25519Keypair>();
        }
    };

}
#endif
