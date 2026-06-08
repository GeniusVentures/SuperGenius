#include <algorithm>
#include <chrono>
#include <utility>
#include "crdt/atomic_transaction.hpp"
#include "crdt/crdt_datastore.hpp"
#include "base/logger.hpp"

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

    outcome::result<void> AtomicTransaction::Put( HierarchicalKey key, Buffer value )
    {
        if ( is_committed_ )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        modified_keys_.insert( key.GetKey() ); // Track the key
        operations_.push_back( { Operation::PUT,  std::move(key), std::move( value ) } );
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
            if ( latest_op->type == Operation::PUT )
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

    outcome::result<CID> AtomicTransaction::Commit( const std::set<std::string> &topics )
    {
        static auto logger = base::createLogger( "AtomicTransaction" );

        if ( is_committed_ )
        {
            logger->error( "Commit called after transaction already committed" );
            return outcome::failure( boost::system::error_code{} );
        }

        auto commit_start = std::chrono::steady_clock::now();

        // Create a combined delta for all operations
        auto     combined_delta = std::make_shared<Delta>();
        uint64_t max_priority   = 0;
        size_t   total_value_size = 0;

        std::string topic_list;
        for ( const auto &topic : topics )
        {
            if ( !topic_list.empty() )
            {
                topic_list += ",";
            }
            topic_list += topic;
        }

        logger->debug( "Commit started (operations={}, topics='{}')", operations_.size(), topic_list );

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

        auto publish_start = std::chrono::steady_clock::now();
        auto result = datastore_->Publish( combined_delta, topics );
        auto publish_ms = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                                   publish_start )
                              .count();

        if ( !result.has_failure() )
        {
            is_committed_ = true;
            auto commit_ms = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                                      commit_start )
                                 .count();
            logger->debug( "Commit succeeded (operations={}, total_value_bytes={}, elements={}, tombstones={}, "
                           "priority={}, publish_ms={}, total_ms={}, cid={})",
                           operations_.size(),
                           total_value_size,
                           combined_delta->elements_size(),
                           combined_delta->tombstones_size(),
                           combined_delta->priority(),
                           publish_ms,
                           commit_ms,
                           result.value().toString().value() );
        }
        else
        {
            auto commit_ms = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                                      commit_start )
                                 .count();
            logger->error( "Commit failed (operations={}, total_value_bytes={}, elements={}, tombstones={}, "
                           "priority={}, publish_ms={}, total_ms={}, error='{}')",
                           operations_.size(),
                           total_value_size,
                           combined_delta->elements_size(),
                           combined_delta->tombstones_size(),
                           combined_delta->priority(),
                           publish_ms,
                           commit_ms,
                           result.error().message() );
        }

        return result;
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
}
