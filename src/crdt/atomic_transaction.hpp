#ifndef SUPERGENIUS_ATOMIC_TRANSACTION_HPP
#define SUPERGENIUS_ATOMIC_TRANSACTION_HPP

#include "base/buffer.hpp"
#include "crdt/hierarchical_key.hpp"
#include "crdt/proto/delta.pb.h"
#include "outcome/outcome.hpp"

#include <memory>
#include <vector>

namespace sgns::crdt 
{
    class CrdtDatastore;
    class AtomicTransaction;

    /**
     * @brief AtomicTransaction provides atomic multi-key operations for CRDT datastore
     * All operations within a transaction are combined into a single delta and published
     * atomically to ensure consistency.
     */
    class AtomicTransaction 
    {
    public:
        using Buffer = base::Buffer;
        using Delta = pb::Delta;

        /**
         * @brief Constructor for AtomicTransaction
         * @param datastore pointer to CRDT datastore
         */
        explicit AtomicTransaction(std::shared_ptr<CrdtDatastore> datastore);

        /**
         * @brief Destructor ensures rollback if not committed
         */
        ~AtomicTransaction();

        /**
         * @brief Add a key-value pair to the transaction
         * @param key hierarchical key to add
         * @param value buffer value to store
         * @return outcome::success or failure if already committed
         */
        outcome::result<void> Put(const HierarchicalKey& key, const Buffer& value);

        /**
         * @brief Delete a key in the transaction
         * @param key hierarchical key to remove
         * @return outcome::success or failure if already committed
         */
        outcome::result<void> Remove(const HierarchicalKey& key);

        /**
         * @brief Commit all operations atomically
         * @param topic Optional topic name for targeted publishing.
         * @return outcome::success if committed successfully, failure otherwise
         */
        outcome::result<void> Commit(const std::optional<std::string> &topic = std::nullopt);
        /**
         * @brief Commits all operations atomically to a single topic, rather than all topics.
         * @param topicName The single topic name to which we broadcast the combined delta
         * @return outcome::success if committed successfully, failure otherwise
         */
        outcome::result<void> CommitToTopic( const std::string &topicName );

    private:
        enum class Operation 
        {
            PUT,
            REMOVE
        };

        struct PendingOperation 
        {
            Operation type;
            HierarchicalKey key;
            Buffer value;
        };

        /**
         * @brief Rollback all pending operations
         */
        void Rollback();

        std::shared_ptr<CrdtDatastore> datastore_;
        std::vector<PendingOperation> operations_;
        bool is_committed_;
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_ATOMIC_TRANSACTION_HPP