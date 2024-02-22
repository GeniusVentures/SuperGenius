/**
 * @file       SystemClockFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _SYSTEM_CLOCK_FACTORY_HPP_
#define _SYSTEM_CLOCK_FACTORY_HPP_

#include "application/impl/local_key_storage.hpp"

class SystemClockFactory
{
public:
    static std::shared_ptr<sgns::clock::SystemClock> create()
    {
        return std::make_shared<sgns::clock::SystemClockImpl>();
    }
};

#endif