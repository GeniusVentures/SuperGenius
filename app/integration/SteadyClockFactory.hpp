/**
 * @file       SteadyClockFactory.hpp
 * @brief      
 * @date       2024-03-04
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _STEADY_CLOCK_FACTORY_HPP_
#define _STEADY_CLOCK_FACTORY_HPP_

#include "clock/impl/clock_impl.hpp"

namespace sgns
{
    class SteadyClockFactory
    {
    public:
        std::shared_ptr<sgns::clock::SteadyClock> create()
        {
            return std::make_shared<sgns::clock::SteadyClockImpl>();
        }
    };
}

#endif