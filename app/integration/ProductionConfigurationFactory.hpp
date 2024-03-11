/**
 * @file       ProductionConfigurationFactory.hpp
 * @brief      
 * @date       2024-02-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _PRODUCTION_CONFIGURATION_FACTORY_HPP_
#define _PRODUCTION_CONFIGURATION_FACTORY_HPP_

#include "primitives/production_configuration.hpp"

//TODO - Check if this factory is needed
class ProductionConfigurationFactory
{
    public:
    //TODO - Check if "production_api_impl" not necessary here
    static std::shared_ptr<sgns::primitives::ProductionConfiguration> create()
    {
        //Creating a empty config
        return std::make_shared<sgns::primitives::ProductionConfiguration>();
    }

};

#endif
