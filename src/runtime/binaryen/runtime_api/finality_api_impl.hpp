
#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_FINALITY_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_FINALITY_IMPL_HPP

#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "runtime/finality_api.hpp"
#include "base/logger.hpp"

namespace sgns::runtime::binaryen {

  class FinalityApiImpl : public RuntimeApi, public FinalityApi {
   public:
    explicit FinalityApiImpl(
        const std::shared_ptr<WasmProvider> &wasm_provider,
        const std::shared_ptr<RuntimeManager> &runtime_manager);

    ~FinalityApiImpl() override = default;

    outcome::result<boost::optional<ScheduledChange>> pending_change(
        const Digest &digest) override;

    outcome::result<boost::optional<ForcedChange>> forced_change(
        const Digest &digest) override;

    outcome::result<primitives::AuthorityList> authorities(
        const primitives::BlockId &block_id) override;
	base::Logger logger_;
  };
}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_FINALITY_IMPL_HPP
