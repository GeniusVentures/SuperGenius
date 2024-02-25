

#ifndef SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_KEY_VALUE_BLOCK_HEADER_REPOSITORY_HPP
#define SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_KEY_VALUE_BLOCK_HEADER_REPOSITORY_HPP

#include "blockchain/block_header_repository.hpp"

#include "blockchain/impl/common.hpp"
#include "crypto/hasher.hpp"

namespace sgns::blockchain {

  class KeyValueBlockHeaderRepository : public BlockHeaderRepository {
   public:
    KeyValueBlockHeaderRepository(std::shared_ptr<storage::BufferStorage> map,
                                  std::shared_ptr<crypto::Hasher> hasher);

    ~KeyValueBlockHeaderRepository() override = default;

    auto getNumberByHash(const base::Hash256 &hash) const
        -> outcome::result<primitives::BlockNumber> override;

    auto getHashByNumber(const primitives::BlockNumber &number) const
        -> outcome::result<base::Hash256> override;

    auto getBlockHeader(const primitives::BlockId &id) const
        -> outcome::result<primitives::BlockHeader> override;

    auto getBlockStatus(const primitives::BlockId &id) const
        -> outcome::result<blockchain::BlockStatus> override;
        
    std::string GetName() override
    {
        return "KeyValueBlockHeaderRepository";
    }
   private:
    std::shared_ptr<storage::BufferStorage> map_;
    std::shared_ptr<crypto::Hasher> hasher_;
  };

}  // namespace sgns::blockchain

#endif  // SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_KEY_VALUE_BLOCK_HEADER_REPOSITORY_HPP
