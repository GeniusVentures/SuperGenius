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

    outcome::result<void> AtomicTransaction::Commit()
    {
        if ( is_committed_ )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // Create a combined delta for all operations
        auto     combined_delta = std::make_shared<Delta>();
        uint64_t max_priority   = 0;

        for ( const auto &[type, key, value] : operations_ )
        {
            std::shared_ptr<Delta> delta;
            if ( type == Operation::PUT )
            {
                OUTCOME_TRY( ( auto &&, result ),
                             datastore_->CreateDeltaToAdd( key.GetKey(), std::string( value.toString() ) ) );
                delta = result;
            }
            else // REMOVE
            {
                OUTCOME_TRY( ( auto &&, result ), datastore_->CreateDeltaToRemove( key.GetKey() ) );
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
    }

} // namespace sgns::crdt
