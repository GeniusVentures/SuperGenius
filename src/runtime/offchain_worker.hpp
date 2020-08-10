
#ifndef SUPERGENIUS_SRC_RUNTIME_OFFCHAIN_WORKER_HPP
#define SUPERGENIUS_SRC_RUNTIME_OFFCHAIN_WORKER_HPP

#include <outcome/outcome.hpp>
#include "base/buffer.hpp"
#include "primitives/common.hpp"

namespace sgns::runtime {

  class OffchainWorker {
   protected:
    using BlockNumber = sgns::primitives::BlockNumber;

   public:
    virtual ~OffchainWorker() = default;

    /**
     * @brief calls offchain_worker method of OffchainWorker runtime api
     * @param bn block number
     * @return success or error
     */
    virtual outcome::result<void> offchain_worker(BlockNumber bn) = 0;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_OFFCHAIN_WORKER_HPP
