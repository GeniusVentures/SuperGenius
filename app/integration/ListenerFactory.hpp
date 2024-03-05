/**
 * @file       ListenerFactory.hpp
 * @brief      
 * @date       2024-03-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _LISTENER_FACTORY_HPP_
#define _LISTENER_FACTORY_HPP_

#include "api/transport/impl/http/http_listener_impl.hpp"
#include "api/transport/impl/ws/ws_listener_impl.hpp"

class CComponentFactory;
namespace sgns
{
    class ListenerFactory
    {
    public:
        std::shared_ptr<api::Listener> create( const std::string &type, const boost::asio::ip::tcp::endpoint &endpoint )
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );

            auto result = component_factory->GetComponent( "AppStateManager", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize AppStateManager first" );
            }
            auto app_state_manager = std::dynamic_pointer_cast<application::AppStateManager>( result.value() );

            result = component_factory->GetComponent( "RpcContext", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize RpcContext first" );
            }
            auto rpc_context = std::dynamic_pointer_cast<api::RpcContext>( result.value() );

            api::Listener::Configuration listener_config;
            listener_config.endpoint = endpoint;
            if ( type == "ws" )
            {
                return std::make_shared<api::WsListenerImpl>( //
                    app_state_manager,                        //
                    rpc_context,                              //
                    listener_config,                          //
                    api::WsSession::Configuration{}           //
                );
            }
            else if ( type == "http" )
            {
                return std::make_shared<api::HttpListenerImpl>( //
                    app_state_manager,                          //
                    rpc_context,                                //
                    listener_config,                            //
                    api::HttpSession::Configuration{}           //
                );
            }
            else
            {
                throw std::runtime_error( "Unsupported Listener type" );
            }
        }
    };
}


#endif
