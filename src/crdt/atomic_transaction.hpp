#ifndef SUPERGENIUS_ATOMIC_TRANSACTION_HPP
#define SUPERGENIUS_ATOMIC_TRANSACTION_HPP

#include "base/buffer.hpp"
#include "crdt/hierarchical_key.hpp"
#include "crdt/proto/delta.pb.h"
#include "outcome/outcome.hpp"

#include <memory>
#include <vector>
#include <optional>
#include <unordered_set>

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
        using Delta  = pb::Delta;

        /**
         * @brief Constructor for AtomicTransaction
         * @param datastore pointer to CRDT datastore
         */
        explicit AtomicTransaction( std::shared_ptr<CrdtDatastore> datastore );

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
        outcome::result<void> Put( const HierarchicalKey &key, const Buffer &value );

        /**
         * @brief Delete a key in the transaction
         * @param key hierarchical key to remove
         * @return outcome::success or failure if already committed
         */
        outcome::result<void> Remove( const HierarchicalKey &key );

        /**
         * @brief Get a value for a key
         * @param key hierarchical key to retrieve
         * @return Buffer containing the value if found, or error if not found/committed
         * @note This method checks pending operations first, then falls back to datastore
         */
        outcome::result<Buffer> Get( const HierarchicalKey &key ) const;

        /**
         * @brief Erase a key from the transaction (alias for Remove)
         * @param key hierarchical key to erase
         * @return outcome::success or failure if already committed
         */
        outcome::result<void> Erase( const HierarchicalKey &key );

        /**
         * @brief Check if a key has been modified in this transaction
         * @param key hierarchical key to check
         * @return true if key has pending operations in this transaction
         */
        bool HasKey( const HierarchicalKey &key ) const;

        /**
         * @brief    Commits all pending operations atomically.
         *            Combines all pending operations into a single Delta and publishes it.
         * @param[in] topic Optional topic name for targeted publishing. If not provided, the default broadcast behavior is used.
         * @return outcome::success on successful commit, or outcome::failure if an error occurs.
         */
        outcome::result<void> Commit();
        outcome::result<void> Commit(const std::vector<std::string>& topics);

    private:
        enum class Operation
        {
            PUT,
            REMOVE
        };

        struct PendingOperation
        {
            Operation       type;
            HierarchicalKey key;
            Buffer          value;
        };

        /**
         * @brief Rollback all pending operations
         */
        void Rollback();

        /**
         * @brief Find the most recent operation for a given key
         * @param key hierarchical key to search for
         * @return optional containing the operation if found
         */
        std::optional<PendingOperation> FindLatestOperation( const HierarchicalKey &key ) const;

        std::shared_ptr<CrdtDatastore>  datastore_;
        std::vector<PendingOperation>   operations_;
        std::unordered_set<std::string> modified_keys_; // Track which keys have been modified
        bool                            is_committed_;
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_ATOMIC_TRANSACTION_HPP
