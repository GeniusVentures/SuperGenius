#include "blockchain/block_header_repository.hpp"

#include "base/visitor.hpp"

namespace sgns::blockchain {

    outcome::result<primitives::BlockNumber> BlockHeaderRepository::getNumberById( const primitives::BlockId &id ) const
    {
        return visit_in_place(
            id, []( const primitives::BlockNumber &n ) { return n; },
            [this]( const base::Hash256 &hash ) { return getNumberByHash( hash ); } );
    }

  /**
   * @param id of a block which hash is returned
   * @return block hash or a none optional if the corresponding block header
   * is not in storage or a storage error
   */
  outcome::result<base::Hash256> BlockHeaderRepository::getHashById( const primitives::BlockId &id ) const
  {
      return visit_in_place(
          id, [this]( const primitives::BlockNumber &n ) { return getHashByNumber( n ); },
          []( const base::Hash256 &hash ) { return hash; } );
  }

}  // namespace sgns::blockchain
