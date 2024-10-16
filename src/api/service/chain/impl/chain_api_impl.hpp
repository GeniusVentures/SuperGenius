
#ifndef SUPERGENIUS_SRC_CHAIN_API_IMPL_HPP
#define SUPERGENIUS_SRC_CHAIN_API_IMPL_HPP

#include <memory>

#include "api/service/chain/chain_api.hpp"
#include "blockchain/block_header_repository.hpp"
#include "blockchain/block_tree.hpp"

namespace sgns::api {

  class ChainApiImpl : public ChainApi {
   public:
    ~ChainApiImpl() override = default;

    ChainApiImpl(std::shared_ptr<blockchain::BlockHeaderRepository> block_repo,
                 std::shared_ptr<blockchain::BlockTree> block_tree);

    outcome::result<BlockHash> getBlockHash() const override;

    outcome::result<BlockHash> getBlockHash(BlockNumber value) const override;

    outcome::result<BlockHash> getBlockHash(
        std::string_view value) const override;

    outcome::result<std::vector<BlockHash>> getBlockHash(
        gsl::span<const ValueType> values) const override;

    std::string GetName() override
    {
      return "ChainApiImpl";
    }

   private:
    std::shared_ptr<blockchain::BlockHeaderRepository> block_repo_;
    std::shared_ptr<blockchain::BlockTree> block_tree_;
  };
}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_CHAIN_API_IMPL_HPP
