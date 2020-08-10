
#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_TAGGED_TRANSACTION_QUEUE_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_TAGGED_TRANSACTION_QUEUE_IMPL_HPP

#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "runtime/tagged_transaction_queue.hpp"

namespace sgns::runtime::binaryen {

  class TaggedTransactionQueueImpl : public RuntimeApi,
                                     public TaggedTransactionQueue {
   public:
    explicit TaggedTransactionQueueImpl(
        const std::shared_ptr<RuntimeManager> &runtime_manager);

    ~TaggedTransactionQueueImpl() override = default;

    outcome::result<primitives::TransactionValidity> validate_transaction(
        const primitives::Extrinsic &ext) override;
  };
}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_TAGGED_TRANSACTION_QUEUE_IMPL_HPP
