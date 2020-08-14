
#include "runtime/dummy/finality_dummy.hpp"

#include <boost/system/error_code.hpp>

namespace sgns::runtime::dummy {

  FinalityDummy::FinalityDummy(
      std::shared_ptr<application::KeyStorage> key_storage)
      : key_storage_{std::move(key_storage)} {
    BOOST_ASSERT(key_storage_ != nullptr);
  }

  outcome::result<boost::optional<primitives::ScheduledChange>>
  FinalityDummy::pending_change(const Finality::Digest &digest) {
    BOOST_ASSERT_MSG(false, "not implemented");  // NOLINT
    return outcome::failure(boost::system::error_code{});
  }
  outcome::result<boost::optional<primitives::ForcedChange>>
  FinalityDummy::forced_change(const Finality::Digest &digest) {
    BOOST_ASSERT_MSG(false, "not implemented");  // NOLINT
    return outcome::failure(boost::system::error_code{});
  }
  outcome::result<std::vector<primitives::Authority>> FinalityDummy::authorities(
      const primitives::BlockId &block_id) {
    return std::vector<primitives::Authority>{
        {{key_storage_->getLocalEd25519Keypair().public_key}, 1}};
  }
}  // namespace sgns::runtime::dummy
