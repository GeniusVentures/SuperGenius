#ifndef SUPERGENIUS_SRC_AUTHORSHIP_IMPL_BLOCK_BUILDER_IMPL_HPP
#define SUPERGENIUS_SRC_AUTHORSHIP_IMPL_BLOCK_BUILDER_IMPL_HPP

#include "authorship/block_builder.hpp"

#include "base/logger.hpp"

namespace sgns::authorship {

  class BlockBuilderImpl : public BlockBuilder {
   public:
    ~BlockBuilderImpl() override = default;

    BlockBuilderImpl(primitives::BlockHeader block_header/*,
                     std::shared_ptr<runtime::BlockBuilder> r_block_builder*/);

    outcome::result<void> pushExtrinsic(
        const primitives::Extrinsic &extrinsic) override;

    [[nodiscard]] outcome::result<primitives::Block> bake() const override;

private:
    primitives::BlockHeader block_header_;
    //std::shared_ptr<runtime::BlockBuilder> r_block_builder_;
    base::Logger logger_;

    std::vector<primitives::Extrinsic> extrinsics_;
  };

}  // namespace sgns::authorship

#endif  // SUPERGENIUS_SRC_AUTHORSHIP_IMPL_BLOCK_BUILDER_IMPL_HPP
