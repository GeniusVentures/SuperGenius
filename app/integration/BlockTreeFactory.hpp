/**
 * @file       BlockTreeFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _BLOCK_TREE_FACTORY_HPP_
#define _BLOCK_TREE_FACTORY_HPP_

class BlockTreeFactory
{
public:
    static std::shared_ptr<sgns::blockchain::BlockTree> create( const std::string &db_path )
    {
        auto header_repo_ = std::make_shared<sgns::blockchain::KeyValueBlockHeaderRepository>( BufferStorageFactory::create( "rocksdb", db_path ),
                                                                                               HasherFactory::create() );

        auto result = sgns::blockchain::BlockTreeImpl::create( //
            header_repo_,                                      //
        );

        if ( result )
        {
            return result.value();
        }
        else
        {
            throw std::runtime_error( "BlockTree not created" );
        }
    }
}

#endif
