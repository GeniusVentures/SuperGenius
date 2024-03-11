/**
 * @file       SR25519ProviderFactory.hpp
 * @brief      
 * @date       2024-02-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _SR25519_PROVIDER_FACTORY_HPP_
#define _SR25519_PROVIDER_FACTORY_HPP_

#include "crypto/sr25519/sr25519_provider_impl.hpp"
#include "crypto/random_generator/boost_generator.hpp"

class SR25519ProviderFactory
{
public:
static std::shared_ptr<sgns::crypto::SR25519Provider> create()
{
    return std::make_shared<sgns::crypto::SR25519ProviderImpl>(std::make_shared<sgns::crypto::BoostRandomGenerator>());
}
};

#endif

