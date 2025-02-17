

#ifndef SUPERGENIUS_RUNTIME_CORE_HPP
#define SUPERGENIUS_RUNTIME_CORE_HPP

#include "outcome/outcome.hpp"
#include <vector>

#include "primitives/authority.hpp"
#include "primitives/block.hpp"
#include "primitives/block_id.hpp"
#include "primitives/common.hpp"
#include "primitives/transaction_validity.hpp"
#include "primitives/version.hpp"
#include <boost/optional.hpp>
namespace sgns::runtime {
  class WasmProvider;
  /**
   * Core represents mandatory part of runtime api
   */
  class Core {
   public:
    virtual ~Core() = default;

    /**
     * @brief Returns the version of the runtime
     * @return runtime version
     */
    virtual outcome::result<primitives::Version> version(
        const boost::optional<primitives::BlockHash> &block_hash) = 0;

    /**
     * @brief Executes the given block
     * @param block block to execute
     */
    virtual outcome::result<void> execute_block(
        const primitives::Block &block) = 0;

    /**
     * @brief Initialize a block with the given header.
     * @param header header used for block initialization
     */
    virtual outcome::result<void> initialise_block(
        const primitives::BlockHeader &header) = 0;

    /**
     * Get current authorities
     * @return collection of authorities
     */
    virtual outcome::result<std::vector<primitives::AuthorityId>> authorities(
        const primitives::BlockId &block_id) = 0;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_RUNTIME_CORE_HPP
