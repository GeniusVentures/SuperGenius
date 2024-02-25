/**
 * @file       SuperGeniusDemoApp.cpp
 * @brief      SuperGenius demo source file
 * @date       2024-02-21
 * @author     Henrique A. Klein (henryaklein@gmail.com)
 */
#include "SuperGeniusDemoApp.hpp"
#include "base/logger.hpp"
#include "integration/AppConfigurationFactory.hpp"

SuperGeniusDemoApp::SuperGeniusDemoApp()
{
    std::cout << "SuperGeniusDemoApp Constructed" << std::endl;
}
SuperGeniusDemoApp::~SuperGeniusDemoApp()
{
    std::cout << "SuperGeniusDemoApp Destructed" << std::endl;
}

void SuperGeniusDemoApp::init( int argc, char **argv )
{
    std::cout << "SuperGeniusDemoApp Init" << std::endl;

    auto logger = sgns::base::createLogger( "SuperGenius block node: " );
    //cfg         = std::shared_ptr<sgns::application::AppConfiguration>( std::move( AppConfigurationFactory::create( logger ) ) );
    cfg = AppConfigurationFactory::create( logger );

    cfg->initialize_from_args( sgns::application::AppConfiguration::LoadScheme::kValidating, argc, argv );

    validation_node = std::make_shared<sgns::application::ValidatingNodeApplication>( std::move( cfg ) );
}
void SuperGeniusDemoApp::run( void )
{
    validation_node->run();
}
void SuperGeniusDemoApp::exit( void )
{
}