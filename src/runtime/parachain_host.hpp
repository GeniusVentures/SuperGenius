
#ifndef SUPERGENIUS_SRC_RUNTIME_PARACHAIN_HOST_HPP
#define SUPERGENIUS_SRC_RUNTIME_PARACHAIN_HOST_HPP

#include "base/buffer.hpp"
#include "primitives/parachain_host.hpp"

namespace sgns::runtime {

  class ParachainHost {
   protected:
    using Buffer = base::Buffer;
    using ValidatorId = primitives::parachain::ValidatorId;
    using DutyRoster = primitives::parachain::DutyRoster;
    using ParachainId = primitives::parachain::ParaId;

   public:
    virtual ~ParachainHost() = default;

    /**
     * @brief Calls the ParachainHost_duty_roster function from wasm code
     * @return DutyRoster structure or error if fails
     */
    virtual outcome::result<DutyRoster> duty_roster() = 0;

    /**
     * @brief Calls the ParachainHost_active_parachains function from wasm code
     * @return vector of ParachainId items or error if fails
     */
    virtual outcome::result<std::vector<ParachainId>> active_parachains() = 0;

    /**
     * @brief Calls the ParachainHost_parachain_head function from wasm code
     * @param id parachain id
     * @return parachain head or error if fails
     */
    virtual outcome::result<boost::optional<Buffer>> parachain_head(
        ParachainId id) = 0;

    /**
     * @brief Calls the ParachainHost_parachain_code function from wasm code
     * @param id parachain id
     * @return parachain code or error if fails
     */
    virtual outcome::result<boost::optional<sgns::base::Buffer>>
    parachain_code(ParachainId id) = 0;

    /**
     * @brief reports validators list for given block_id
     * @return validators list
     */
    virtual outcome::result<std::vector<ValidatorId>> validators() = 0;
  };

}  // namespace sgns::runtime
#endif  // SUPERGENIUS_SRC_RUNTIME_PARACHAIN_HOST_HPP
