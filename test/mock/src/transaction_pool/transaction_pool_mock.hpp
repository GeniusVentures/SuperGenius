
#ifndef SUPERGENIUS_TEST_MOCK_SRC_TRANSACTION_POOL_TRANSACTION_POOL_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_TRANSACTION_POOL_TRANSACTION_POOL_MOCK_HPP

#include "transaction_pool/transaction_pool.hpp"

#include <gmock/gmock.h>

namespace sgns::transaction_pool {

  class TransactionPoolMock : public TransactionPool {
   public:
    outcome::result<void> submitOne(Transaction &&tx) {
      return submitOne(tx);
    }
    MOCK_METHOD1(submitOne, outcome::result<void>(Transaction));
    MOCK_METHOD1(submit, outcome::result<void>(std::vector<Transaction>));

    MOCK_METHOD1(removeOne, outcome::result<void>(const Transaction::Hash &));
    MOCK_METHOD1(remove,
                 outcome::result<void>(const std::vector<Transaction::Hash> &));

    MOCK_CONST_METHOD0(
        getReadyTransactions,
        std::map<Transaction::Hash, std::shared_ptr<Transaction>>());

    MOCK_METHOD1(
        removeStale,
        outcome::result<std::vector<Transaction>>(const primitives::BlockId &));

    MOCK_CONST_METHOD0(getStatus, Status());

    MOCK_METHOD0(GetName,std::string());
  };

}  // namespace sgns::transaction_pool

#endif  // SUPERGENIUS_TEST_MOCK_SRC_TRANSACTION_POOL_TRANSACTION_POOL_MOCK_HPP
