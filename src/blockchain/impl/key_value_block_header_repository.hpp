#ifndef SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_KEY_VALUE_BLOCK_HEADER_REPOSITORY_HPP
#define SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_KEY_VALUE_BLOCK_HEADER_REPOSITORY_HPP

#include "blockchain/block_header_repository.hpp"

#include "crypto/hasher.hpp"
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>

namespace sgns::blockchain
{

    class KeyValueBlockHeaderRepository : public BlockHeaderRepository
    {
    public:
        KeyValueBlockHeaderRepository( std::shared_ptr<crdt::GlobalDB> db, std::shared_ptr<crypto::Hasher> hasher,
                                       const std::string &net_id );

        ~KeyValueBlockHeaderRepository() override = default;

        auto getNumberByHash( const base::Hash256 &hash ) const -> outcome::result<primitives::BlockNumber> override;

        auto getHashByNumber( const primitives::BlockNumber &number ) const -> outcome::result<base::Hash256> override;

        auto getBlockHeader( const primitives::BlockId &id ) const -> outcome::result<primitives::BlockHeader> override;

        auto putBlockHeader( const primitives::BlockHeader &header ) -> outcome::result<primitives::BlockHash> override;

        auto removeBlockHeader( const primitives::BlockId &id ) -> outcome::result<void> override;

        auto getBlockStatus( const primitives::BlockId &id ) const -> outcome::result<blockchain::BlockStatus> override;

        std::string GetName() override
        {
            return "KeyValueBlockHeaderRepository";
        }

        std::string             GetHeaderPath() const;
        std::vector<uint8_t>    GetHeaderSerializedData( const primitives::BlockHeader &header ) const;
        primitives::BlockHeader GetBlockHeaderFromSerialized( const std::vector<uint8_t> &serialized_data ) const;

    private:
        static constexpr std::string_view BLOCKCHAIN_PATH = "blockchain/";

        std::shared_ptr<crdt::GlobalDB> db_;
        std::shared_ptr<crypto::Hasher> hasher_;

        std::string block_header_key_prefix;
    };

} // namespace sgns::blockchain

#endif // SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_KEY_VALUE_BLOCK_HEADER_REPOSITORY_HPP
