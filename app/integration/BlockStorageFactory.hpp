/**
 * @file       BlockStorageFactory.hpp
 * @brief      
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_STORAGE_FACTORY_HPP_
#define _BLOCK_STORAGE_FACTORY_HPP_

#include "blockchain/block_storage.hpp"
#include "singleton/CComponentFactory.hpp"
#include "storage/trie/trie_storage.hpp"
#include "blockchain/impl/key_value_block_storage.hpp"
#include "application/key_storage.hpp"
#include "verification/finality/voter_set.hpp"
#include "primitives/authority.hpp"
namespace sgns
{
    class BlockStorageFactory
    {

    public:
        std::shared_ptr<blockchain::BlockStorage> create()
        {
            auto component_factory = SINGLETONINSTANCE( CComponentFactory );
            auto maybe_hasher      = component_factory->GetComponent( "Hasher", boost::none );

            if ( !maybe_hasher )
            {
                throw std::runtime_error( "Initialize Hasher first" );
            }
            auto hasher = std::dynamic_pointer_cast<crypto::Hasher>( maybe_hasher.value() );

            auto maybe_buff_storage = component_factory->GetComponent( "BufferStorage", boost::make_optional( std::string( "rocksdb" ) ) );

            if ( !maybe_buff_storage )
            {
                throw std::runtime_error( "Initialize BufferStorage first" );
            }
            auto buff_storage = std::dynamic_pointer_cast<storage::BufferStorage>( maybe_buff_storage.value() );

            auto maybe_trie_storage = component_factory->GetComponent( "TrieStorage", boost::none );

            if ( !maybe_trie_storage )
            {
                throw std::runtime_error( "Initialize TrieStorage first" );
            }
            auto trie_storage = std::dynamic_pointer_cast<storage::trie::TrieStorage>( maybe_trie_storage.value() );

            auto result = component_factory->GetComponent( "KeyStorage", boost::none );
            if ( !result )
            {
                throw std::runtime_error( "Initialize KeyStorage first" );
            }
            auto key_storage = std::dynamic_pointer_cast<application::KeyStorage>( result.value() );

            //TODO - Fix finalityAPI dependence here!
            auto maybe_storage = blockchain::KeyValueBlockStorage::create( //
                trie_storage->getRootHash(),                               //
                buff_storage,
                hasher, //
                [buff_storage, key_storage]( const primitives::Block &genesis_block )
                {
                    // handle genesis initialization, which happens when there is not
                    // authorities and last completed round in the storage
                    if ( !buff_storage->get( storage::GetAuthoritySetKey() ) )
                    {
                        // insert authorities
                        // TODO - Insert authorities here from finalityAPI
                        primitives::AuthorityList weighted_authorities{ { { key_storage->getLocalEd25519Keypair().public_key }, 1 } };
                        //auto        finality_api             = injector.template create<sptr<runtime::FinalityApi>>();
                        //const auto &weighted_authorities_res = finality_api->authorities( primitives::BlockId( primitives::BlockNumber{ 0 } ) );
                        //BOOST_ASSERT_MSG( weighted_authorities_res, "finality_api_->authorities failed" );
                        //const auto &weighted_authorities = weighted_authorities_res.value();

                        for ( const auto authority : weighted_authorities )
                        {
                            spdlog::info( "Finality authority: {}", authority.id.id.toHex() );
                        }

                        verification::finality::VoterSet voters{ 0 };
                        for ( const auto &weighted_authority : weighted_authorities )
                        {
                            voters.insert( weighted_authority.id.id, weighted_authority.weight );
                            spdlog::debug( "Added to finality authorities: {}, weight: {}", weighted_authority.id.id.toHex(),
                                           weighted_authority.weight );
                        }
                        BOOST_ASSERT_MSG( voters.size() != 0, "Finality voters are empty" );
                        auto authorities_put_res = buff_storage->put( storage::GetAuthoritySetKey(), base::Buffer( scale::encode( voters ).value() ) );
                        if ( !authorities_put_res )
                        {
                            BOOST_ASSERT_MSG( false, "Could not insert authorities" );
                            std::exit( 1 );
                        }
                    }
                } );
            if ( !maybe_storage )
            {
                throw std::runtime_error( "BlockStorage failed to be created" );
            }

            return maybe_storage.value();
        }
    };

}
#endif
