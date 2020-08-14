#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_PARACHAIN_HOST_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_PARACHAIN_HOST_IMPL_HPP

#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "runtime/parachain_host.hpp"

namespace sgns::runtime::binaryen {

  class ParachainHostImpl : public RuntimeApi, public ParachainHost {
   public:
    /**
     * @brief constructor
     * @param state_code error or result code
     * @param extension extension instance
     * @param codec scale codec instance
     */
    explicit ParachainHostImpl(const std::shared_ptr<RuntimeManager> &runtime_manager);

    ~ParachainHostImpl() override = default;

    outcome::result<DutyRoster> duty_roster() override;

    outcome::result<std::vector<ParachainId>> active_parachains() override;

    outcome::result<boost::optional<Buffer>> parachain_head(
        ParachainId id) override;

    outcome::result<boost::optional<Buffer>> parachain_code(
        ParachainId id) override;

    outcome::result<std::vector<ValidatorId>> validators() override;
  };
}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_PARACHAIN_HOST_IMPL_HPP
