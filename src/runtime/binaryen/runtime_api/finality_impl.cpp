
#include "runtime/binaryen/runtime_api/finality_impl.hpp"

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
      : RuntimeApi(runtime_manager),
	  logger_{ base::createLogger("FinalityImpl") }
  {
	  BOOST_ASSERT(runtime_manager);
  }

  outcome::result<boost::optional<ScheduledChange>> FinalityImpl::pending_change(
      const Digest &digest) {
	  logger_->debug("FinalityApi_finality_pending_change {}", digest.size());
    return execute<boost::optional<ScheduledChange>>(
        "FinalityApi_finality_pending_change",
        CallPersistency::EPHEMERAL,
        digest);
  }

  outcome::result<boost::optional<ForcedChange>> FinalityImpl::forced_change(
      const Digest &digest) {
	  logger_->debug("FinalityApi_finality_forced_change {}", digest.size());
    return execute<boost::optional<ForcedChange>>(
        "FinalityApi_finality_forced_change", CallPersistency::EPHEMERAL, digest);
  }

  outcome::result<std::vector<Authority>> FinalityImpl::authorities(
      const primitives::BlockId &block_id) {
	  logger_->debug("FinalityApi_finality_authorities ");
      std::vector<Authority> result ;
      Authority authority_;
	  authority_.id.id = SessionKey::fromHex("88dc3417d5058ec4b4503e0c12ea1a0a89be200fe98922423d4334014fa6b0ee").value();
	  authority_.weight = 100u;
	  result.push_back(authority_);
    
	  return result;

//     return execute<std::vector<Authority>>(
// //         //"FinalityApi_finality_authorities", CallPersistency::EPHEMERAL, block_id);
// 		"FinalityApi_finality_authorities", CallPersistency::EPHEMERAL, block_id);
  }
}  // namespace sgns::runtime::binaryen
