
#include "runtime/binaryen/runtime_api/finality_api_impl.hpp"

#include "primitives/authority.hpp"

namespace sgns::runtime::binaryen {
  using base::Buffer;
  using primitives::Authority;
  using primitives::Digest;
  using primitives::ForcedChange;
  using primitives::ScheduledChange;
  using primitives::SessionKey;

  FinalityApiImpl::FinalityApiImpl(
      const std::shared_ptr<WasmProvider> &wasm_provider,
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(wasm_provider, runtime_manager),
	  logger_{ base::createLogger("FinalityApiImpl") }
  {
	  BOOST_ASSERT(runtime_manager);
  }

  outcome::result<boost::optional<ScheduledChange>> FinalityApiImpl::pending_change(
      const Digest &digest) {
	  logger_->debug("FinalityApi_finality_pending_change {}", digest.size());
    return execute<boost::optional<ScheduledChange>>(
        "FinalityApi_finality_pending_change",
        CallPersistency::EPHEMERAL,
        digest);
  }

  outcome::result<boost::optional<ForcedChange>> FinalityApiImpl::forced_change(
      const Digest &digest) {
	  logger_->debug("FinalityApi_finality_forced_change {}", digest.size());
    return execute<boost::optional<ForcedChange>>(
        "FinalityApi_finality_forced_change", CallPersistency::EPHEMERAL, digest);
  }

  outcome::result<primitives::AuthorityList> FinalityApiImpl::authorities(
      const primitives::BlockId &block_id) {
	  logger_->debug("FinalityApi_finality_authorities ");
      /*
	  primitives::AuthorityList result ;
      Authority authority_;
	  authority_.id.id = SessionKey::fromHex("88dc3417d5058ec4b4503e0c12ea1a0a89be200fe98922423d4334014fa6b0ee").value();
	  authority_.weight = 100u;
	  result.push_back(authority_);
    
	  return result;
	  */
      return execute<primitives::AuthorityList>(
      //"FinalityApi_finality_authorities", CallPersistency::EPHEMERAL, block_id);
		"GrandpaApi_grandpa_authorities", CallPersistency::EPHEMERAL, block_id);
  }
}  // namespace sgns::runtime::binaryen
