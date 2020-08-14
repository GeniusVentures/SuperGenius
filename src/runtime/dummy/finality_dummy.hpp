#ifndef SUPERGENIUS_SRC_RUNTIME_FAKE_FINALITY_DUMMY_HPP
#define SUPERGENIUS_SRC_RUNTIME_FAKE_FINALITY_DUMMY_HPP

#include "application/key_storage.hpp"
#include "runtime/finality.hpp"

namespace sgns::runtime::dummy {

  /**
   * Dummy implementation of the finality api. Should not be used in production.
   * Instead of using runtime to get authorities, just returns current authority
   */
  class FinalityDummy : public Finality {
   public:
    ~FinalityDummy() override = default;

    explicit FinalityDummy(std::shared_ptr<application::KeyStorage> key_storage);

    outcome::result<boost::optional<ScheduledChange>> pending_change(
        const Digest &digest) override;

    outcome::result<boost::optional<ForcedChange>> forced_change(
        const Digest &digest) override;

    outcome::result<std::vector<WeightedAuthority>> authorities(
        const primitives::BlockId &block_id) override;

   private:
    std::shared_ptr<application::KeyStorage> key_storage_;
  };
}  // namespace sgns::runtime::dummy

#endif  // SUPERGENIUS_SRC_RUNTIME_FAKE_FINALITY_DUMMY_HPP
