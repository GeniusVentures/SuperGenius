/**
 * @file       AppStateManagerFactory.hpp
 * @brief      Factory to create AppStateManager derived classes
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _APP_STATE_MANAGER_FACTORY_HPP_
#define _APP_STATE_MANAGER_FACTORY_HPP_

#include "application/impl/app_state_manager_impl.hpp"

class AppStateManagerFactory
{
public:
    static std::shared_ptr<sgns::application::AppStateManager> create()
    {
        return std::make_shared<sgns::application::AppStateManagerImpl>();
    }
};

#endif
