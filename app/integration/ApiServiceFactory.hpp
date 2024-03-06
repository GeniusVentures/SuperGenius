/**
 * @file       ApiServiceFactory.hpp
 * @brief      
 * @date       2024-03-04
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _API_SERVICE_FACTORY_HPP_
#define _API_SERVICE_FACTORY_HPP_

#include "api/service/api_service.hpp"
#include "application/app_state_manager.hpp"
#include "api/transport/rpc_thread_pool.hpp"
#include "api/transport/listener.hpp"
#include "api/jrpc/jrpc_server.hpp"
#include "api/jrpc/jrpc_processor.hpp"
#include "subscription/subscriber.hpp"

class CComponnetFactory;
namespace sgns
{
    class ApiServiceFactory
    {
    public:
        std::shared_ptr<api::ApiService> create()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );

            auto result = component_factory->GetComponent( "AppStateManager", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize AppStateManager first" );
            }
            auto app_state_manager = std::dynamic_pointer_cast<application::AppStateManager>( result.value() );

            result = component_factory->GetComponent( "RpcThreadPool", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize RpcThreadPool first" );
            }
            auto rpc_thread_pool = std::dynamic_pointer_cast<api::RpcThreadPool>( result.value() );

            result = component_factory->GetComponent( "Listener", boost::make_optional( std::string( "http" ) ) );
            if ( !result )
            {
                throw std::runtime_error( "Initialize Http Listener first" );
            }
            auto http_listener = std::dynamic_pointer_cast<api::Listener>( result.value() );

            result = component_factory->GetComponent( "Listener", boost::make_optional( std::string( "ws" ) ) );
            if ( !result )
            {
                throw std::runtime_error( "Initialize Ws Listener first" );
            }
            auto ws_listener = std::dynamic_pointer_cast<api::Listener>( result.value() );

            std::vector<std::shared_ptr<api::Listener>> listeners{ std::move( http_listener ), std::move( ws_listener ) };

            result = component_factory->GetComponent( "JRpcServer", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize JRpcServer first" );
            }
            auto jrpc_server = std::dynamic_pointer_cast<api::JRpcServer>( result.value() );

            result = component_factory->GetComponent( "JRpcProcessor", boost::make_optional( std::string( "State" ) ) );
            if ( !result )
            {
                throw std::runtime_error( "Initialize StateJRpcProcessor first" );
            }
            auto state_jrpc_proc = std::dynamic_pointer_cast<api::JRpcProcessor>( result.value() );

            result = component_factory->GetComponent( "JRpcProcessor", boost::make_optional( std::string( "Author" ) ) );
            if ( !result )
            {
                throw std::runtime_error( "Initialize AuthorJRpcProcessor first" );
            }
            auto author_jrpc_proc = std::dynamic_pointer_cast<api::JRpcProcessor>( result.value() );

            result = component_factory->GetComponent( "JRpcProcessor", boost::make_optional( std::string( "Chain" ) ) );
            if ( !result )
            {
                throw std::runtime_error( "Initialize ChainJRpcProcessor first" );
            }
            auto chain_jrpc_proc = std::dynamic_pointer_cast<api::JRpcProcessor>( result.value() );

            result = component_factory->GetComponent( "JRpcProcessor", boost::make_optional( std::string( "System" ) ) );
            if ( !result )
            {
                throw std::runtime_error( "Initialize SystemJRpcProcessor first" );
            }
            auto system_jrpc_proc = std::dynamic_pointer_cast<api::JRpcProcessor>( result.value() );

            std::vector<std::shared_ptr<api::JRpcProcessor>> processors{
                state_jrpc_proc,  //
                author_jrpc_proc, //
                chain_jrpc_proc,  //
                system_jrpc_proc  //
            };

            //TODO - Not sute about this. Maybe also should be a singleton of sorts
            using SubscriptionEngineType =
                subscription::SubscriptionEngine<base::Buffer, std::shared_ptr<api::Session>, base::Buffer, primitives::BlockHash>;
            auto subscription_engine = std::make_shared<SubscriptionEngineType>();

            return std::make_shared<api::ApiService>( //
                app_state_manager,                    //
                rpc_thread_pool,                      //
                listeners,                            //
                jrpc_server,                          //
                processors,                           //
                subscription_engine                   //
            );
        }
    };
}

#endif
