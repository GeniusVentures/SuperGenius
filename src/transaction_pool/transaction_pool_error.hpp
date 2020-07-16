

#ifndef SUPERGENIUS_SRC_TRANSACTION_POOL_TRANSACTION_POOL_ERROR_HPP
#define SUPERGENIUS_SRC_TRANSACTION_POOL_TRANSACTION_POOL_ERROR_HPP

#include <outcome/outcome.hpp>

namespace sgns::transaction_pool {
  enum class TransactionPoolError {
    TX_ALREADY_IMPORTED = 1,
    TX_NOT_FOUND,
    POOL_IS_FULL,
  };
}

OUTCOME_HPP_DECLARE_ERROR_2(sgns::transaction_pool, TransactionPoolError);

#endif  // SUPERGENIUS_SRC_TRANSACTION_POOL_TRANSACTION_POOL_ERROR_HPP
