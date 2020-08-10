
#include "runtime/binaryen/runtime_api/grandpa_impl.hpp"

#include "primitives/authority.hpp"

namespace sgns::runtime::binaryen {
  using base::Buffer;
  using primitives::Authority;
  using primitives::Digest;
  using primitives::ForcedChange;
  using primitives::ScheduledChange;
  using primitives::SessionKey;

  FinalityImpl::FinalityImpl(
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(runtime_manager) {}

  outcome::result<boost::optional<ScheduledChange>> FinalityImpl::pending_change(
      const Digest &digest) {
    return execute<boost::optional<ScheduledChange>>(
        "FinalityApi_grandpa_pending_change",
        CallPersistency::EPHEMERAL,
        digest);
  }

  outcome::result<boost::optional<ForcedChange>> FinalityImpl::forced_change(
      const Digest &digest) {
    return execute<boost::optional<ForcedChange>>(
        "FinalityApi_grandpa_forced_change", CallPersistency::EPHEMERAL, digest);
  }

  outcome::result<std::vector<Authority>> FinalityImpl::authorities(
      const primitives::BlockId &block_id) {
    return execute<std::vector<Authority>>(
        "FinalityApi_grandpa_authorities", CallPersistency::EPHEMERAL, block_id);
  }
}  // namespace sgns::runtime::binaryen
