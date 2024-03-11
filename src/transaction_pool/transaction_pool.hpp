

#ifndef SUPERGENIUS_TRANSACTION_POOL_HPP
#define SUPERGENIUS_TRANSACTION_POOL_HPP

#include <outcome/outcome.hpp>

#include "primitives/block_id.hpp"
#include "primitives/transaction.hpp"
#include <map>
#include "integration/IComponent.hpp"
namespace sgns::transaction_pool {

  using primitives::Transaction;

  class TransactionPool :public IComponent {
   public:
    struct Status;
    struct Limits;

    virtual ~TransactionPool() = default;

    /**
     * Import one verified transaction to the pool. If it has unresolved
     * dependencies (requires tags of transactions that are not in the pool
     * yet), it will wait in the pool until its dependencies are solved, in
     * which case it becomes ready and may be pruned, or it is banned from the
     * pool for some amount of time as its longevity is reached or the pool is
     * overflown
     */
    virtual outcome::result<void> submitOne(Transaction &&tx) = 0;

    /**
     * Import several transactions to the pool
     * @see submitOne()
     */
    virtual outcome::result<void> submit(std::vector<Transaction> txs) = 0;

    /**
     * Remove transaction from the pool
     * @param txHash - hash of the removed transaction
     */
    virtual outcome::result<void> removeOne(
        const Transaction::Hash &txHash) = 0;

    /**
     * Remove several transactions from the pool
     * @see removeOne()
     */
    virtual outcome::result<void> remove(
        const std::vector<Transaction::Hash> &txHashes) = 0;

    /**
     * @return transactions ready to included in the next block, sorted by their
     * priority
     */
    virtual std::map<Transaction::Hash, std::shared_ptr<Transaction>>
    getReadyTransactions() const = 0;

    /**
     * Remove from the pool and temporarily ban transactions which longevity is
     * expired
     * @param at a block that is considered current for removal (transaction t
     * is banned if
     * 'block number when t got to pool' + 't.longevity' <= block number of at)
     * @return removed transactions
     */
    virtual outcome::result<std::vector<Transaction>> removeStale(
        const primitives::BlockId &at) = 0;

    virtual Status getStatus() const = 0;
  };

  struct TransactionPool::Status {
    size_t ready_num;
    size_t waiting_num;
    //added to fix link error
    friend std::ostream &operator<<(std::ostream &out, const TransactionPool::Status &test_struct)
    {
      return out << test_struct.ready_num << test_struct.waiting_num; 
    }
    //end
  };

  struct TransactionPool::Limits {
    static constexpr size_t kDefaultMaxReadyNum = 128;
    static constexpr size_t kDefaultCapacity = 512;

    size_t max_ready_num = kDefaultMaxReadyNum;
    size_t capacity = kDefaultCapacity;
    //added to fix link error
    friend std::ostream &operator<<(std::ostream &out, const TransactionPool::Limits &test_struct)
    {
      return out << test_struct.max_ready_num << test_struct.capacity; 
    }
    //end
  };

}  // namespace sgns::transaction_pool

#endif  // SUPERGENIUS_TRANSACTION_POOL_HPP
