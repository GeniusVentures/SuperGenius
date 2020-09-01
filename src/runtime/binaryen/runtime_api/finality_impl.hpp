
#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_FINALITY_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_FINALITY_IMPL_HPP

#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "runtime/finality.hpp"
#include "base/logger.hpp"

namespace sgns::runtime::binaryen {

  class FinalityImpl : public RuntimeApi, public Finality {
   public:
    explicit FinalityImpl(
        const std::shared_ptr<RuntimeManager> &runtime_manager);

    ~FinalityImpl() override = default;

    outcome::result<boost::optional<ScheduledChange>> pending_change(
        const Digest &digest) override;

    outcome::result<boost::optional<ForcedChange>> forced_change(
        const Digest &digest) override;

    outcome::result<std::vector<WeightedAuthority>> authorities(
        const primitives::BlockId &block_id) override;
	base::Logger logger_;
  };
}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_FINALITY_IMPL_HPP
