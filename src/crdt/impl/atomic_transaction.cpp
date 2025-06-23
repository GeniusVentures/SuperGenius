#include <algorithm>
#include <utility>
#include "crdt/atomic_transaction.hpp"
#include "crdt/crdt_datastore.hpp"

namespace sgns::crdt
{
    AtomicTransaction::AtomicTransaction( std::shared_ptr<CrdtDatastore> datastore ) :
        datastore_( std::move( datastore ) ), is_committed_( false )
    {
    }

    AtomicTransaction::~AtomicTransaction()
    {
        if ( !is_committed_ )
        {
            Rollback();
        }
    }

    outcome::result<void> AtomicTransaction::Put( const HierarchicalKey &key, const Buffer &value )
    {
        if ( is_committed_ )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        operations_.push_back( { Operation::PUT, key, value } );
        modified_keys_.insert( key.GetKey() ); // Track the key
        return outcome::success();
    }

    outcome::result<void> AtomicTransaction::Remove( const HierarchicalKey &key )
    {
        if ( is_committed_ )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        operations_.push_back( { Operation::REMOVE, key, Buffer() } );
        return outcome::success();
    }

    outcome::result<AtomicTransaction::Buffer> AtomicTransaction::Get( const HierarchicalKey &key ) const
    {
        // First, check pending operations in reverse order (most recent first)
        auto latest_op = FindLatestOperation( key );
        if ( latest_op.has_value() )
        {
            if ( latest_op->type == Operation::REMOVE )
            {
                // Key has been removed in this transaction
                return outcome::failure( boost::system::error_code{} );
            }
            else if ( latest_op->type == Operation::PUT )
            {
                // Return the value from the pending put operation
                return latest_op->value;
            }
        }

        // Key not found
        return outcome::failure( boost::system::error_code{} );
    }

    outcome::result<void> AtomicTransaction::Erase( const HierarchicalKey &key )
    {
        if ( is_committed_ )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // Remove all operations for this key from the operations vector
        auto new_end = std::remove_if( operations_.begin(),
                                       operations_.end(),
                                       [&key]( const PendingOperation &op )
                                       { return op.key.GetKey() == key.GetKey(); } );

        // If we removed any operations, erase them and remove from the set
        if ( new_end != operations_.end() )
        {
            operations_.erase( new_end, operations_.end() );
            modified_keys_.erase( key.GetKey() );
        }

        return outcome::success();
    }

    bool AtomicTransaction::HasKey( const HierarchicalKey &key ) const
    {
        return modified_keys_.find( key.GetKey() ) != modified_keys_.end();
    }

    outcome::result<void> AtomicTransaction::Commit()
    {
        if ( is_committed_ )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // Create a combined delta for all operations
        auto     combined_delta = std::make_shared<Delta>();
        uint64_t max_priority   = 0;

        for ( const auto &op : operations_ )
        {
            std::shared_ptr<Delta> delta;
            if ( op.type == Operation::PUT )
            {
                OUTCOME_TRY( ( auto &&, result ),
                             datastore_->CreateDeltaToAdd( op.key.GetKey(), std::string( op.value.toString() ) ) );
                delta = result;
            }
            else // REMOVE
            {
                OUTCOME_TRY( ( auto &&, result ), datastore_->CreateDeltaToRemove( op.key.GetKey() ) );
                delta = result;
            }

            for ( const auto &elem : delta->elements() )
            {
                auto new_elem = combined_delta->add_elements();
                new_elem->CopyFrom( elem );
            }
            for ( const auto &tomb : delta->tombstones() )
            {
                auto new_tomb = combined_delta->add_tombstones();
                new_tomb->CopyFrom( tomb );
            }
            max_priority = std::max( max_priority, delta->priority() );
        }
        combined_delta->set_priority( max_priority );

        auto result = datastore_->Publish( combined_delta );
        if ( result.has_failure() )
        {
            return result.error();
        }

        is_committed_ = true;
        return outcome::success();
    }

    void AtomicTransaction::Rollback()
    {
        operations_.clear();
        modified_keys_.clear();
    }

    std::optional<AtomicTransaction::PendingOperation> AtomicTransaction::FindLatestOperation(
        const HierarchicalKey &key ) const
    {
        // Search from the end (most recent operations first)
        for ( auto it = operations_.rbegin(); it != operations_.rend(); ++it )
        {
            if ( it->key.GetKey() == key.GetKey() )
            {
                return *it;
            }
        }
        return std::nullopt;
    }

} // namespace sgns::crdt
