/**
 * @file       FinalityFactory.hpp
 * @brief      
 * @date       2024-03-01
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _FINALITY_FACTORY_HPP_
#define _FINALITY_FACTORY_HPP_

#include "verification/finality/impl/finality_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "application/app_state_manager.hpp"
#include "verification/finality/environment.hpp"
#include "storage/buffer_map_types.hpp"
#include "crypto/ed25519_provider.hpp"
#include "crypto/ed25519_types.hpp"
#include "clock/clock.hpp"
#include "verification/authority/authority_manager.hpp"

namespace sgns
{
    class FinalityFactory
    {
    public:
        std::shared_ptr<verification::finality::Finality> create( const std::shared_ptr<boost::asio::io_context> io_context )
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );

            auto result = component_factory->GetComponent( "AppStateManager", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize AppStateManager first" );
            }
            auto app_state_manager = std::dynamic_pointer_cast<application::AppStateManager>( result.value() );

            result = component_factory->GetComponent( "Environment", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize Environment first" );
            }
            auto environment = std::dynamic_pointer_cast<verification::finality::Environment>( result.value() );

            result = component_factory->GetComponent( "BufferStorage", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize BufferStorage first" );
            }
            auto buff_storage = std::dynamic_pointer_cast<storage::BufferStorage>( result.value() );

            result = component_factory->GetComponent( "ED25519Provider", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize ED25519Provider first" );
            }
            auto ed25519_provider = std::dynamic_pointer_cast<crypto::ED25519Provider>( result.value() );

            result = component_factory->GetComponent( "ED25519Keypair", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize ED25519Keypair first" );
            }
            auto ed25519_keypair = std::dynamic_pointer_cast<crypto::ED25519Keypair>( result.value() );

            result = component_factory->GetComponent( "SteadyClock", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize SteadyClock first" );
            }
            auto clock = std::dynamic_pointer_cast<clock::SteadyClock>( result.value() );

            /*
        result = component_factory->GetComponent("IOContext", boost::none);

        if (!result)
        {
            throw std::runtime_error("Initialize IOContext first");
        }
        auto io_context = std::dynamic_pointer_cast<IOContext>(result.value());
        */

            result = component_factory->GetComponent( "AuthorityManager", boost::none );

            if ( !result )
            {
                throw std::runtime_error( "Initialize AuthorityManager first" );
            }
            auto authority_manager = std::dynamic_pointer_cast<authority::AuthorityManager>( result.value() );

            return std::make_shared<verification::finality::FinalityImpl>( //
                app_state_manager,                                         //
                environment,                                               //
                buff_storage,                                              //
                ed25519_provider,                                          //
                *ed25519_keypair,                                          //
                clock,                                                     //
                io_context,                                                //
                authority_manager                                         //
            );
        }
    };

}

#endif
