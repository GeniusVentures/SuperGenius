/**
 * @file       AuthorApiFactory.hpp
 * @brief      
 * @date       2024-02-26
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _AUTHOR_API_FACTORY_HPP_
#define _AUTHOR_API_FACTORY_HPP_

#include "api/service/author/impl/author_api_impl.hpp"
#include "singleton/CComponentFactory.hpp"
#include "transaction_pool/transaction_pool.hpp"
#include "crypto/hasher.hpp"
#include "network/extrinsic_gossiper.hpp"

class AuthorApiFactory
{
public:
    //TODO - Removed "validate_transaction from runtime::TaggedTransactionQueue". Need to replace it
    static std::shared_ptr<sgns::api::AuthorApi> create()
    {
        auto component_factory = SINGLETONINSTANCE( CComponentFactory );
        auto result            = component_factory->GetComponent( "TransactionPool", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize TransactionPool first" );
        }
        auto trans_pool = std::dynamic_pointer_cast<sgns::transaction_pool::TransactionPool>( result.value() );

        result = component_factory->GetComponent( "Hasher", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize Hasher first" );
        }
        auto hasher = std::dynamic_pointer_cast<sgns::crypto::Hasher>( result.value() );

        result = component_factory->GetComponent( "ExtrinsicGossiper", boost::none );

        if ( !result )
        {
            throw std::runtime_error( "Initialize ExtrinsicGossiper first" );
        }
        auto gossiper = std::dynamic_pointer_cast<sgns::network::ExtrinsicGossiper>( result.value() );

        return std::make_shared<sgns::api::AuthorApiImpl>( trans_pool, hasher, gossiper );
    }
};


#endif
