/**
 * @file       ProposerFactory.hpp
 * @brief      
 * @date       2024-02-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _PROPOSER_FACTORY_HPP_
#define _PROPOSER_FACTORY_HPP_

#include "authorship/impl/proposer_impl.hpp"
#include "integration/CComponentFactory.hpp"
#include "transaction_pool/transaction_pool.hpp"

class ProposerFactory
{
public:
    static std::shared_ptr<sgns::authorship::Proposer> create()
    {
        //TODO - Removed runtime::BlockBuilder because of binaryen dependecy
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );

        auto result = component_factory->GetComponent( "BlockBuilderFactory", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize BlockBuilderFactory first" );
        }
        auto block_builder_factory = std::dynamic_pointer_cast<sgns::authorship::BlockBuilderFactory>( result.value() );

        result = component_factory->GetComponent( "TransactionPool", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Initialize TransactionPool first" );
        }
        auto transaction_pool = std::dynamic_pointer_cast<sgns::transaction_pool::TransactionPool>( result.value() );

        return std::make_shared<sgns::authorship::ProposerImpl>( block_builder_factory, transaction_pool);
    }
};

#endif
