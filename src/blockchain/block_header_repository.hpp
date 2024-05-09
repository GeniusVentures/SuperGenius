#ifndef SUPERGENIUS_CORE_BLOCKCHAIN_BLOCK_HEADER_REPOSITORY_HPP
#define SUPERGENIUS_CORE_BLOCKCHAIN_BLOCK_HEADER_REPOSITORY_HPP

#include <boost/optional.hpp>

#include "outcome/outcome.hpp"
#include "base/blob.hpp"
#include "primitives/block_header.hpp"
#include "primitives/block_id.hpp"
#include "integration/IComponent.hpp"

namespace sgns::blockchain
{
    /**
   * Status of a block
   */
    enum class BlockStatus
    {
        InChain,
        Unknown
    };

    /**
   * An interface to a storage with block headers that provides several
   * convenience methods, such as getting block number by its hash and vice
   * versa or getting a block status
   */
    class BlockHeaderRepository : public IComponent
    {
    public:
        /**
        * @param hash - a blake2_256 hash of an SCALE encoded block header
        * @return the number of the block with the provided hash in case one is in
        * the storage or an error
        */
        [[nodiscard]] virtual outcome::result<primitives::BlockNumber> getNumberByHash(
            const base::Hash256 &hash ) const = 0;

        /**
        * @param number - the number of a block, contained in a block header
        * @return the hash of the block with the provided number in case one is in
        * the storage or an error
        */
        [[nodiscard]] virtual outcome::result<base::Hash256> getHashByNumber(
            const primitives::BlockNumber &number ) const = 0;

        /**
        * @return block header with corresponding id or an error
        */
        [[nodiscard]] virtual outcome::result<primitives::BlockHeader> getBlockHeader(
            const primitives::BlockId &id ) const = 0;

        virtual outcome::result<primitives::BlockHash> putBlockHeader( const primitives::BlockHeader &header ) = 0;

        virtual outcome::result<void> removeBlockHeader( const primitives::BlockId &id ) = 0;

        /**
        * @param id of a block which status is returned
        * @return status of a block or a storage error
        */
        [[nodiscard]] virtual outcome::result<sgns::blockchain::BlockStatus> getBlockStatus(
            const primitives::BlockId &id ) const = 0;

        /**
        * @param id of a block which number is returned
        * @return block number or a none optional if the corresponding block header
        * is not in storage or a storage error
        */
        [[nodiscard]] outcome::result<primitives::BlockNumber> getNumberById( const primitives::BlockId &id ) const;

        /**
        * @param id of a block which hash is returned
        * @return block hash or a none optional if the corresponding block header
        * is not in storage or a storage error
        */
        [[nodiscard]] outcome::result<base::Hash256> getHashById( const primitives::BlockId &id ) const;
    };
}

#endif
