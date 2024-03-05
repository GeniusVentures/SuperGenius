/**
 * @file       JRpcProcessorFactory.hpp
 * @brief      
 * @date       2024-03-05
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _JRPC_PROCESSOR_FACTORY_HPP_
#define _JRPC_PROCESSOR_FACTORY_HPP_

#include "api/service/author/author_jrpc_processor.hpp"
#include "api/service/chain/chain_jrpc_processor.hpp"
#include "api/service/state/state_jrpc_processor.hpp"
#include "api/service/system/system_jrpc_processor.hpp"
#include "api/jrpc/jrpc_server.hpp"

class CComponentFactory;
namespace sgns
{
    class JRpcProcessorFactory
    {
    public:
        std::shared_ptr<api::JRpcProcessor> create( const std::string & type )
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );

            auto result = component_factory->GetComponent( "JRpcServer", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize JRpcServer first" );
            }
            auto jrpc_server = std::dynamic_pointer_cast<api::JRpcServer>( result.value() );

            const std::string api_type = type + "Api";
            result = component_factory->GetComponent( api_type, boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize " + api_type + " first" );
            }
            if ( type == "Author" )
            {
                auto api = std::dynamic_pointer_cast<api::AuthorApi>( result.value() );
                return std::make_shared<api::author::AuthorJRpcProcessor>( jrpc_server, api );
            }
            else if ( type == "Chain" )
            {
                auto api = std::dynamic_pointer_cast<api::ChainApi>( result.value() );
                return std::make_shared<api::chain::ChainJRpcProcessor>( jrpc_server, api );
            }
            else if ( type == "State" )
            {
                auto api = std::dynamic_pointer_cast<api::StateApi>( result.value() );
                return std::make_shared<api::state::StateJRpcProcessor>( jrpc_server, api );
            }
            else if ( type == "System" )
            {
                auto api = std::dynamic_pointer_cast<api::SystemApi>( result.value() );
                return std::make_shared<api::system::SystemJRpcProcessor>( jrpc_server, api );
            }
            else
            {
                throw std::runtime_error("Invalid JRPC processor");
            }
        }
    };
}

#endif
