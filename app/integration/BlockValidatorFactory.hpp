/**
 * @file       BlockValidatorFactory.hpp
 * @brief      
 * @date       2024-02-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_VALIDATOR_FACTORY_HPP_
#define _BLOCK_VALIDATOR_FACTORY_HPP_

#include "verification/validation/production_block_validator.hpp"
#include "integration/CComponentFactory.hpp"
#include "blockchain/block_tree.hpp"
#include "crypto/hasher.hpp"
#include "crypto/sr25519_provider.hpp"

class BlockValidatorFactory
{
public:
    //TODO - Check the removal of TaggedTransactionQueue
    static std::shared_ptr<sgns::verification::BlockValidator> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "BlockTree", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockTree first" );
        }
        auto block_tree = std::dynamic_pointer_cast<sgns::blockchain::BlockTree>( result.value() );

        result = component_factory->GetComponent( "Hasher", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize Hasher first" );
        }
        auto hasher = std::dynamic_pointer_cast<sgns::crypto::Hasher>( result.value() );

        result = component_factory->GetComponent( "VRFProvider", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize VRFProvider first" );
        }
        auto vrf_provider = std::dynamic_pointer_cast<sgns::crypto::VRFProvider>( result.value() );

        result = component_factory->GetComponent( "SR25519Provider", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize SR25519Provider first" );
        }
        auto sr25519_provider = std::dynamic_pointer_cast<sgns::crypto::SR25519Provider>( result.value() );

        return std::make_shared<sgns::verification::ProductionBlockValidator>( //
            block_tree,                                                        //
            hasher,                                                            //
            vrf_provider,                                                      //
            sr25519_provider                                                   //
        );
    }
};

#endif
