#include "runtime/binaryen/runtime_api/offchain_worker_impl.hpp"

namespace sgns::runtime::binaryen {
  OffchainWorkerImpl::OffchainWorkerImpl(
      const std::shared_ptr<WasmProvider> &wasm_provider,
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(wasm_provider, runtime_manager) {}

  outcome::result<void> OffchainWorkerImpl::offchain_worker(BlockNumber bn) {
    return execute<void>(
        "OffchainWorkerApi_offchain_worker", CallPersistency::EPHEMERAL, bn);
  }
}  // namespace sgns::runtime::binaryen
