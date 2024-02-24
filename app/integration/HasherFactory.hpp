/**
 * @file       HasherFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _HASHER_FACTORY_HPP_
#define _HASHER_FACTORY_HPP_

#include "crypto/hasher/hasher_impl.hpp"

class HasherFactory
{
public:
    static std::shared_ptr<sgns::crypto::Hasher> create()
    {
        return std::make_shared<sgns::crypto::HasherImpl>();
    }
};
#endif
