
#ifndef KAGOME_TAGGED_TRANSACTION_QUEUE_MOCK_HPP
#define KAGOME_TAGGED_TRANSACTION_QUEUE_MOCK_HPP

#include "runtime/tagged_transaction_queue.hpp"

#include <gmock/gmock.h>

namespace sgns::runtime {
  struct TaggedTransactionQueueMock : public TaggedTransactionQueue {
    MOCK_METHOD1(validate_transaction,
                 outcome::result<primitives::TransactionValidity>(
                     const primitives::Extrinsic &));
  };
}  // namespace sgns::runtime

#endif  // KAGOME_TAGGED_TRANSACTION_QUEUE_MOCK_HPP
