/**
 * @file       SuperGeniusDemoApp.hpp
 * @brief      SuperGenius App demo header file
 * @date       2024-02-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _SUPERGENIUS_DEMO_APP_HPP_
#define _SUPERGENIUS_DEMO_APP_HPP_
#include <memory>

#include "application/app_config.hpp"
#include "application/impl/validating_node_application.hpp"

class SuperGeniusDemoApp
{
public:
    SuperGeniusDemoApp();
    ~SuperGeniusDemoApp();
    void init( int argc, char **argv );
    void run( void );
    void exit( void );

private:
    std::shared_ptr<sgns::application::AppConfiguration>          cfg;
    std::shared_ptr<sgns::application::ValidatingNodeApplication> validation_node;
};

#endif