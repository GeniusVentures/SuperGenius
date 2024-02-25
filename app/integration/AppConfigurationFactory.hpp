/**
 * @file       AppConfigurationFactory.hpp
 * @brief      Factory to create AppConfiguration derived classes
 * @date       2024-02-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _APP_CONFIGURATION_FACTORY_HPP_
#define _APP_CONFIGURATION_FACTORY_HPP_

#include "application/impl/app_config_impl.hpp"

class AppConfigurationFactory
{
public:
    static std::shared_ptr<sgns::application::AppConfiguration> create( sgns::base::Logger logger )
    {
        return std::make_shared<sgns::application::AppConfigurationImpl>( std::move( logger ) );
    }
};

#endif
