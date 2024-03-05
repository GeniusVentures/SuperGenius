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

/*

    ApiService(
        const std::shared_ptr<application::AppStateManager> &app_state_manager,
        std::shared_ptr<api::RpcThreadPool> thread_pool,
        std::vector<std::shared_ptr<Listener>> listeners,
        std::shared_ptr<JRpcServer> server,
        const std::vector<std::shared_ptr<JRpcProcessor>> &processors,
        SubscriptionEnginePtr subscription_engine);

        template <typename Injector>
  sptr<api::ApiService> get_jrpc_api_service(const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<api::ApiService>>(boost::none);
    if (initialized) {
      return initialized.value();
    }
    using SubscriptionEnginePtr = std::shared_ptr<
        subscription::SubscriptionEngine<base::Buffer,
                                         std::shared_ptr<api::Session>,
                                         base::Buffer,
                                         primitives::BlockHash>>;
    auto subscription_engine =
        injector.template create<SubscriptionEnginePtr>();
    auto app_state_manager =
        injector
            .template create<std::shared_ptr<application::AppStateManager>>();
    auto rpc_thread_pool =
        injector.template create<std::shared_ptr<api::RpcThreadPool>>();
    std::vector<std::shared_ptr<api::Listener>> listeners{
        injector.template create<std::shared_ptr<api::HttpListenerImpl>>(),
        injector.template create<std::shared_ptr<api::WsListenerImpl>>(),
    };
    auto server = injector.template create<std::shared_ptr<api::JRpcServer>>();
    std::vector<std::shared_ptr<api::JRpcProcessor>> processors{
        injector
            .template create<std::shared_ptr<api::state::StateJrpcProcessor>>(),
        injector.template create<
            std::shared_ptr<api::author::AuthorJRpcProcessor>>(),
        injector
            .template create<std::shared_ptr<api::chain::ChainJrpcProcessor>>(),
        injector.template create<
            std::shared_ptr<api::system::SystemJrpcProcessor>>()};

    initialized =
        std::make_shared<api::ApiService>(std::move(app_state_manager),
                                          std::move(rpc_thread_pool),
                                          std::move(listeners),
                                          std::move(server),
                                          processors,
                                          std::move(subscription_engine));

    auto state_api = injector.template create<std::shared_ptr<api::StateApi>>();
    state_api->setApiService(initialized.value());
    return initialized.value();
  }
*/

#endif
