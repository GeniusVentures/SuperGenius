#include "runtime/binaryen/runtime_api/tagged_transaction_queue_impl.hpp"

namespace sgns::runtime::binaryen {
  using primitives::TransactionValidity;

  TaggedTransactionQueueImpl::TaggedTransactionQueueImpl(
      const std::shared_ptr<WasmProvider> &wasm_provider,
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(wasm_provider, runtime_manager) {}

  outcome::result<primitives::TransactionValidity>
  TaggedTransactionQueueImpl::validate_transaction(
      const primitives::Extrinsic &ext) {
    return execute<TransactionValidity>(
        "TaggedTransactionQueue_validate_transaction",
        CallPersistency::EPHEMERAL,
        ext);
  }
}  // namespace sgns::runtime::binaryen
