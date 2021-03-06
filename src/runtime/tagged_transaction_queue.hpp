

#ifndef SUPERGENIUS_TAGGED_TRANSACTION_QUEUE_HPP
#define SUPERGENIUS_TAGGED_TRANSACTION_QUEUE_HPP

#include "primitives/common.hpp"
#include "primitives/extrinsic.hpp"
#include "primitives/transaction_validity.hpp"

namespace sgns::runtime {

  /**
   * Part of runtime API responsible for transaction validation
   */
  class TaggedTransactionQueue {
   public:
    virtual ~TaggedTransactionQueue() = default;

    /**
     * Calls the TaggedTransactionQueue_validate_transaction function from wasm
     * code
     * @param ext extrinsic containing transaction to be validated
     * @return structure with information about transaction validity
     */
    virtual outcome::result<primitives::TransactionValidity>
    validate_transaction(const primitives::Extrinsic &ext) = 0;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_TAGGED_TRANSACTION_QUEUE_HPP
