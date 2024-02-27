/**
 * @file       SuperGeniusDemoApp.cpp
 * @brief      SuperGenius demo source file
 * @date       2024-02-21
 * @author     Henrique A. Klein (henryaklein@gmail.com)
 */
#include "SuperGeniusDemoApp.hpp"
#include "base/logger.hpp"
#include "integration/AppConfigurationFactory.hpp"
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

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

    const std::string logger_config( R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: true
    groups:
      - name: SuperGeniusDemo
        sink: console
        level: info
        children:
          - name: libp2p
          - name: Gossip
    # ----------------
    )" );
    auto              p2p_log_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
        // Original LibP2P logging config
        std::make_shared<libp2p::log::Configurator>(),
        // Additional logging config for application
        logger_config ) );
    p2p_log_system->configure();

    libp2p::log::setLoggingSystem( p2p_log_system );
    libp2p::log::setLevelOfGroup( "SuperGeniusDemo", soralog::Level::DEBUG );

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