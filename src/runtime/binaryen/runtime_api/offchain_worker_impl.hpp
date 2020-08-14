#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_OFFCHAIN_WORKER_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_OFFCHAIN_WORKER_IMPL_HPP

#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "runtime/offchain_worker.hpp"

namespace sgns::runtime::binaryen {

  class OffchainWorkerImpl : public RuntimeApi, public OffchainWorker {
   public:
    explicit OffchainWorkerImpl(
        const std::shared_ptr<RuntimeManager> &runtime_manager);

    ~OffchainWorkerImpl() override = default;

    outcome::result<void> offchain_worker(BlockNumber bn) override;
  };
}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_IMPL_OFFCHAIN_WORKER_IMPL_HPP
