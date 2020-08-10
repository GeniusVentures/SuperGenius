#include "runtime/binaryen/runtime_api/parachain_host_impl.hpp"

namespace sgns::runtime::binaryen {
  using base::Buffer;
  using primitives::parachain::DutyRoster;
  using primitives::parachain::ParaId;
  using primitives::parachain::ValidatorId;

  ParachainHostImpl::ParachainHostImpl(
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(runtime_manager) {}

  outcome::result<DutyRoster> ParachainHostImpl::duty_roster() {
    return execute<DutyRoster>("ParachainHost_duty_roster",
                               CallPersistency::EPHEMERAL);
  }

  outcome::result<std::vector<ParaId>> ParachainHostImpl::active_parachains() {
    return execute<std::vector<ParaId>>("ParachainHost_active_parachains",
                                        CallPersistency::EPHEMERAL);
  }

  outcome::result<boost::optional<Buffer>> ParachainHostImpl::parachain_head(
      ParachainId id) {
    return execute<boost::optional<Buffer>>(
        "ParachainHost_parachain_head", CallPersistency::EPHEMERAL, id);
  }

  outcome::result<boost::optional<Buffer>> ParachainHostImpl::parachain_code(
      ParachainId id) {
    return execute<boost::optional<Buffer>>(
        "ParachainHost_parachain_code", CallPersistency::EPHEMERAL, id);
  }

  outcome::result<std::vector<ValidatorId>> ParachainHostImpl::validators() {
    return execute<std::vector<ValidatorId>>("ParachainHost_validators",
                                             CallPersistency::EPHEMERAL);
  }

}  // namespace sgns::runtime::binaryen
