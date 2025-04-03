
#ifndef SUPERGENIUS_SRC_AUTHORSHIP_IMPL_BLOCK_BUILDER_FACTORY_IMPL_HPP
#define SUPERGENIUS_SRC_AUTHORSHIP_IMPL_BLOCK_BUILDER_FACTORY_IMPL_HPP

#include "authorship/block_builder_factory.hpp"

#include "blockchain/block_header_repository.hpp"
#include "base/logger.hpp"

namespace sgns::authorship {

  class BlockBuilderFactoryImpl : public BlockBuilderFactory {
   public:
    ~BlockBuilderFactoryImpl() override = default;

    BlockBuilderFactoryImpl(
        std::shared_ptr<runtime::Core> r_core,
        std::shared_ptr<runtime::BlockBuilder> r_block_builder,
        std::shared_ptr<blockchain::BlockHeaderRepository> header_backend);

    outcome::result<std::unique_ptr<BlockBuilder>> create(
        const sgns::primitives::BlockId &parent_id,
        primitives::Digest inherent_digest) const override;

   private:
    std::shared_ptr<runtime::Core> r_core_;
    std::shared_ptr<runtime::BlockBuilder> r_block_builder_;
    std::shared_ptr<blockchain::BlockHeaderRepository> header_backend_;
    base::Logger logger_;
  };

}  // namespace sgns::authorship

#endif  // SUPERGENIUS_SRC_AUTHORSHIP_IMPL_BLOCK_BUILDER_FACTORY_IMPL_HPP
