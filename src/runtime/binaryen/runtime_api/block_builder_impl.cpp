
#include "runtime/binaryen/runtime_api/block_builder_impl.hpp"

namespace sgns::runtime::binaryen {
  using base::Buffer;
  using extensions::Extension;
  using primitives::Block;
  using primitives::BlockHeader;
  using primitives::CheckInherentsResult;
  using primitives::Extrinsic;
  using primitives::InherentData;

  BlockBuilderImpl::BlockBuilderImpl(
      const std::shared_ptr<WasmProvider> &wasm_provider,
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(wasm_provider, runtime_manager),
       logger_{base::createLogger("BlockBuilderImpl")} {}

  outcome::result<primitives::ApplyResult> BlockBuilderImpl::apply_extrinsic(
      const Extrinsic &extrinsic) {
    logger_->debug("BlockBuilder_apply_extrinsic {}", extrinsic.data.toHex());
    return execute<primitives::ApplyResult>(
        "BlockBuilder_apply_extrinsic", CallPersistency::PERSISTENT, extrinsic);
  }

  outcome::result<BlockHeader> BlockBuilderImpl::finalise_block() {
	logger_->debug("BlockBuilder_finalize_block {}");
    return execute<BlockHeader>("BlockBuilder_finalize_block",
                                CallPersistency::PERSISTENT);
  }

  outcome::result<std::vector<Extrinsic>> BlockBuilderImpl::inherent_extrinsics(
      const InherentData &data) {
	logger_->debug("BlockBuilder_inherent_extrinsics {}");
    return execute<std::vector<Extrinsic>>(
        "BlockBuilder_inherent_extrinsics", CallPersistency::EPHEMERAL, data);
  }

  outcome::result<CheckInherentsResult> BlockBuilderImpl::check_inherents(
      const Block &block, const InherentData &data) {
	logger_->debug("BlockBuilder_check_inherents {}");
    return execute<CheckInherentsResult>("BlockBuilder_check_inherents",
                                         CallPersistency::EPHEMERAL,
                                         block,
                                         data);
  }

  outcome::result<base::Hash256> BlockBuilderImpl::random_seed() {
	  logger_->debug("BlockBuilder_random_seed {}");
	  // TODO(Harrm) PRE-154 Figure out what it requires
    return execute<base::Hash256>("BlockBuilder_random_seed",
                                    CallPersistency::EPHEMERAL);
  }
}  // namespace sgns::runtime::binaryen
