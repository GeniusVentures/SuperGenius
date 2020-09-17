#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_BLOCK_BUILDER_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_BLOCK_BUILDER_IMPL_HPP

#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "runtime/block_builder.hpp"
#include "base/logger.hpp"
namespace sgns::runtime::binaryen {

  class BlockBuilderImpl : public RuntimeApi, public BlockBuilder {
   public:
    explicit BlockBuilderImpl(
        const std::shared_ptr<WasmProvider> &wasm_provider,
        const std::shared_ptr<RuntimeManager> &runtime_manager);

    ~BlockBuilderImpl() override = default;

    outcome::result<primitives::ApplyResult> apply_extrinsic(
        const primitives::Extrinsic &extrinsic) override;

    outcome::result<primitives::BlockHeader> finalise_block() override;

    outcome::result<std::vector<primitives::Extrinsic>> inherent_extrinsics(
        const primitives::InherentData &data) override;

    outcome::result<primitives::CheckInherentsResult> check_inherents(
        const primitives::Block &block,
        const primitives::InherentData &data) override;

    outcome::result<base::Hash256> random_seed() override;
    base::Logger logger_;
  };
}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_BLOCK_BUILDER_IMPL_HPP
