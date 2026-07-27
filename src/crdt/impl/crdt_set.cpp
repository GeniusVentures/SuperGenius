#include "crdt/crdt_set.hpp"
#include "base/logger.hpp"
#include <storage/database_error.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <utility>
#include <fstream>

namespace sgns::crdt
{
    namespace
    {
        std::string LogicalKeyFromDatastoreKey( const std::string &keysNamespacePrefix,
                                                const std::string &datastoreKey )
        {
            const std::string keysPrefix = keysNamespacePrefix + "/";
            if ( !boost::algorithm::starts_with( datastoreKey, keysPrefix ) )
            {
                return datastoreKey;
            }

            std::string logicalKey = datastoreKey.substr( keysNamespacePrefix.size() );
            const auto  valueSuffix = "/" + CrdtSet::GetValueSuffix();
            const auto  prioritySuffix = "/" + CrdtSet::GetPrioritySuffix();

            if ( boost::algorithm::ends_with( logicalKey, valueSuffix ) )
            {
                logicalKey.erase( logicalKey.size() - valueSuffix.size() );
            }
            else if ( boost::algorithm::ends_with( logicalKey, prioritySuffix ) )
            {
                logicalKey.erase( logicalKey.size() - prioritySuffix.size() );
            }

            return logicalKey;
        }
    } // namespace

    base::Logger CRDTSet()
    {
        // Always call base::createLogger to get the current logger.
        return base::createLogger( "CRDTSet" );
    }

    CrdtSet::CrdtSet( std::shared_ptr<DataStore> aDatastore,
                      const HierarchicalKey     &aNamespace,
                      PutHookPtr                 aPutHookPtr,
                      DeleteHookPtr              aDeleteHookPtr ) :
        dataStore_( std::move( aDatastore ) ),
        namespaceKey_( aNamespace ),
        putHookFunc_( std::move( aPutHookPtr ) ),
        deleteHookFunc_( std::move( aDeleteHookPtr ) )
    {
        CRDTSet()->debug( "CrdtSet created for namespace '{}'", namespaceKey_.GetKey() );
    }

    CrdtSet::CrdtSet( const CrdtSet &aSet )
    {
        *this = aSet;
    }

    bool CrdtSet::operator==( const CrdtSet &aSet ) const
    {
        bool returnEqual  = true;
        returnEqual      &= this->dataStore_ == aSet.dataStore_;
        returnEqual      &= this->namespaceKey_ == aSet.namespaceKey_;
        return returnEqual;
    }

    bool CrdtSet::operator!=( const CrdtSet &aSet ) const
    {
        return !( *this == aSet );
    }

    CrdtSet &CrdtSet::operator=( const CrdtSet &aSet )
    {
        if ( this != &aSet )
        {
            this->dataStore_      = aSet.dataStore_;
            this->namespaceKey_   = aSet.namespaceKey_;
            this->putHookFunc_    = aSet.putHookFunc_;
            this->deleteHookFunc_ = aSet.deleteHookFunc_;
        }
        return *this;
    }

    outcome::result<std::string> CrdtSet::GetValueFromDatastore( const HierarchicalKey &aKey ) const
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        Buffer bufferKey;
        bufferKey.put( aKey.GetKey() );

        auto bufferValueResult = dataStore_->get( bufferKey );
        if ( bufferValueResult.has_failure() )
        {
            return outcome::failure( bufferValueResult.error() );
        }

        std::string strValue( bufferValueResult.value().toString() );
        return strValue;
    }

    outcome::result<std::shared_ptr<CrdtSet::Delta>> CrdtSet::CreateDeltaToAdd( const std::string &aKey,
                                                                                const std::string &aValue )
    {
        auto delta   = std::make_shared<Delta>();
        auto element = delta->add_elements();
        element->set_key( aKey );
        element->set_value( aValue );

        return delta;
    }

    outcome::result<std::shared_ptr<CrdtSet::Delta>> CrdtSet::CreateDeltaToRemove( const std::string &aKey ) const
    {
        CRDTSet()->debug( "CreateDeltaToRemove called for key '{}'", aKey );
        auto delta = std::make_shared<Delta>();
        // /namespace/s/<key>
        auto prefix         = this->ElemsPrefix( aKey );
        auto strElemsPrefix = prefix.GetKey();

        Buffer keyPrefixBuffer;
        keyPrefixBuffer.put( strElemsPrefix );
        BOOST_OUTCOME_TRY( auto queryResult, this->dataStore_->query( keyPrefixBuffer ) );
        CRDTSet()->debug( "CreateDeltaToRemove key '{}' found {} element entries under '{}'",
                          aKey,
                          queryResult.size(),
                          strElemsPrefix );

        for ( const auto &[key, _] : queryResult )
        {
            std::string keyWithPrefix( key.toString() );
            std::string id = keyWithPrefix.erase( 0, strElemsPrefix.size() );

            auto hId = HierarchicalKey( id );
            CRDTSet()->debug( "CreateDeltaToRemove candidate datastore key '{}', extracted id '{}'",
                              key.toString(),
                              hId.GetKey() );

            if ( !hId.IsTopLevel() )
            {
                CRDTSet()->debug( "CreateDeltaToRemove skipping non-top-level id '{}' for key '{}'",
                                  hId.GetKey(),
                                  aKey );
                continue;
            }

            // check if its already tombed, which case don't add it to the
            // Remove delta set.
            auto isDeletedResult = this->InTombsKeyID( aKey, hId.GetKey() );
            CRDTSet()->debug( "CreateDeltaToRemove tomb check for key '{}' id '{}': has_value={}, tombed={}",
                              aKey,
                              hId.GetKey(),
                              isDeletedResult.has_value(),
                              isDeletedResult.has_value() ? isDeletedResult.value() : false );
            if ( isDeletedResult.has_value() && !isDeletedResult.value() )
            {
                auto tombstone = delta->add_tombstones();
                tombstone->set_key( aKey );
                tombstone->set_id( hId.GetKey() );
                CRDTSet()->debug( "CreateDeltaToRemove adding tombstone for key '{}' id '{}'", aKey, hId.GetKey() );
            }
        }

        CRDTSet()->debug( "CreateDeltaToRemove finished for key '{}' with {} tombstones",
                          aKey,
                          delta->tombstones_size() );

        return delta;
    }

    outcome::result<CrdtSet::Buffer> CrdtSet::GetElement( const std::string &aKey ) const
    {
        // We can only GET an element if it's part of the Set (in
        // "elements" and not in "tombstones").

        // As an optimization:
        // * If the key has a value in the store it means:
        //   -> It occurs at least once in "elems"
        //   -> It may or not be tombstoned
        // * If the key does not have a value in the store:
        //   -> It was either never added

        auto valueK      = this->ValueKey( aKey );
        auto valueResult = this->GetValueFromDatastore( valueK );

        if ( valueResult.has_failure() )
        {
            // not found is fine, we just return it
            return outcome::failure( valueResult.error() );
        }

        // We have an existing element. Check if tombstoned.
        auto inSetResult = this->InElemsNotTombstoned( aKey );
        if ( inSetResult.has_failure() )
        {
            return outcome::failure( inSetResult.error() );
        }

        if ( !inSetResult.value() )
        {
            // attempt to remove so next time we do not have to do this lookup.
            // In concurrency, this may delete a key that was just written
            // and should not be deleted.
            return outcome::failure( boost::system::error_code{} );
        }

        // otherwise return the value
        Buffer bufferValue;
        bufferValue.put( valueResult.value() );

        return bufferValue;
    }

    outcome::result<CrdtSet::QueryResult> CrdtSet::QueryElements(
        std::string_view   aPrefix,
        const QuerySuffix &aSuffix /*=QuerySuffix::QUERY_ALL*/ ) const
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( storage::DatabaseError::UNITIALIZED );
        }

        // We can only GET an element if it's part of the Set (in
        // "elements" and not in "tombstones").

        // As an optimization:
        // * If the key has a value in the store it means:
        //   -> It occurs at least once in "elems"
        //   -> It may or not be tombstoned
        // * If the key does not have a value in the store:
        //   -> It was either never added

        // /namespace/k/<prefix>
        auto prefixKeysKey = this->KeysKey( aPrefix );
        const auto keysNamespacePrefix = this->KeysKey( "" ).GetKey();

        Buffer keyPrefixBuffer;
        keyPrefixBuffer.put( prefixKeysKey.GetKey() );
        auto queryResult = this->dataStore_->query( keyPrefixBuffer );
        if ( queryResult.has_failure() )
        {
            return outcome::failure( queryResult.error() );
        }

        QueryResult elements;
        // Check if elements tombstoned.
        for ( const auto &element : queryResult.value() )
        {
            const auto datastoreKey = std::string( element.first.toString() );
            const auto logicalKey = LogicalKeyFromDatastoreKey( keysNamespacePrefix, datastoreKey );
            auto       inSetResult = this->InElemsNotTombstoned( logicalKey );
            CRDTSet()->debug( "QueryElements evaluating datastore key '{}' mapped to logical key '{}': inSet.has_value={}, inSet={}",
                              datastoreKey,
                              logicalKey,
                              inSetResult.has_value(),
                              inSetResult.has_value() ? inSetResult.value() : false );
            if ( inSetResult.has_failure() || !inSetResult.value() )
            {
                continue;
            }

            std::string key( datastoreKey );
            switch ( aSuffix )
            {
                case QuerySuffix::QUERY_ALL:
                    elements.insert( element );
                    break;
                case QuerySuffix::QUERY_PRIORITYSUFFIX:
                    if ( boost::algorithm::ends_with( key, "/" + GetPrioritySuffix() ) )
                    {
                        elements.insert( element );
                    }
                    break;
                case QuerySuffix::QUERY_VALUESUFFIX:
                    if ( boost::algorithm::ends_with( key, "/" + GetValueSuffix() ) )
                    {
                        elements.insert( element );
                    }
                    break;
                default:
                    CRDTSet()->error( "QueryElements got invalid suffix enum for key '{}'", key );
                    return outcome::failure( queryResult.error() );
            }
        }

        CRDTSet()->debug( "QueryElements prefix '{}' returned {} visible entries after tomb check",
                          aPrefix,
                          elements.size() );

        return elements;
    }

    outcome::result<CrdtSet::QueryResult> CrdtSet::QueryElements( const std::string &prefix_base,
                                                                  const std::string &middle_part,
                                                                  const std::string &remainder_prefix,
                                                                  const QuerySuffix &aSuffix ) const
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( storage::DatabaseError::UNITIALIZED );
        }

        // We can only GET an element if it's part of the Set (in
        // "elements" and not in "tombstones").

        // As an optimization:
        // * If the key has a value in the store it means:
        //   -> It occurs at least once in "elems"
        //   -> It may or not be tombstoned
        // * If the key does not have a value in the store:
        //   -> It was either never added

        // /namespace/k/<prefix>
        auto prefixKeysKey = this->KeysKey( prefix_base );
        const auto keysNamespacePrefix = this->KeysKey( "" ).GetKey();

        auto queryResult = this->dataStore_->query( prefixKeysKey.GetKey() + "/", middle_part, remainder_prefix );
        if ( queryResult.has_failure() )
        {
            return outcome::failure( queryResult.error() );
        }

        QueryResult elements;
        // Check if elements tombstoned.
        for ( const auto &element : queryResult.value() )
        {
            const auto datastoreKey = std::string( element.first.toString() );
            const auto logicalKey = LogicalKeyFromDatastoreKey( keysNamespacePrefix, datastoreKey );
            auto       inSetResult = this->InElemsNotTombstoned( logicalKey );
            if ( inSetResult.has_failure() || !inSetResult.value() )
            {
                continue;
            }

            std::string key( datastoreKey );
            switch ( aSuffix )
            {
                case QuerySuffix::QUERY_ALL:
                    elements.insert( element );
                    break;
                case QuerySuffix::QUERY_PRIORITYSUFFIX:
                    if ( boost::algorithm::ends_with( key, "/" + GetPrioritySuffix() ) )
                    {
                        elements.insert( element );
                    }
                    break;
                case QuerySuffix::QUERY_VALUESUFFIX:
                    if ( boost::algorithm::ends_with( key, "/" + GetValueSuffix() ) )
                    {
                        elements.insert( element );
                    }
                    break;
                default:
                    return outcome::failure( queryResult.error() );
            }
        }

        return elements;
    }

    outcome::result<bool> CrdtSet::IsValueInSet( const std::string &aKey ) const
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // Optimization: if we do not have a value
        // this key was never added.
        auto valueK = this->ValueKey( aKey );

        Buffer bufferKey;
        bufferKey.put( valueK.GetKey() );

        if ( !this->dataStore_->contains( bufferKey ) )
        {
            return false;
        }

        // Otherwise, do the long check.
        auto inElemsNotTombstonedResult = this->InElemsNotTombstoned( aKey );
        if ( inElemsNotTombstonedResult.has_error() )
        {
            return outcome::failure( inElemsNotTombstonedResult.error() );
        }

        return inElemsNotTombstonedResult.value();
    }

    outcome::result<bool> CrdtSet::InElemsNotTombstoned( const std::string &aKey ) const
    {
        CRDTSet()->debug( "InElemsNotTombstoned called with input key '{}'", aKey );
        // /namespace/elems/<key>
        auto prefix         = this->ElemsPrefix( aKey );
        auto strElemsPrefix = prefix.GetKey();
        CRDTSet()->debug( "InElemsNotTombstoned querying elems prefix '{}'", strElemsPrefix );

        Buffer keyPrefixBuffer;
        keyPrefixBuffer.put( strElemsPrefix );
        auto queryResult = this->dataStore_->query( keyPrefixBuffer );
        if ( queryResult.has_failure() )
        {
            CRDTSet()->error( "InElemsNotTombstoned query failed for key '{}'", aKey );
            return outcome::failure( queryResult.error() );
        }

        if ( queryResult.value().empty() )
        {
            CRDTSet()->debug( "InElemsNotTombstoned key '{}' found no elem entries, returning true", aKey );
            return true;
        }

        CRDTSet()->debug( "InElemsNotTombstoned key '{}' found {} elem entries",
                          aKey,
                          queryResult.value().size() );

        for ( const auto &[key, _] : queryResult.value() )
        {
            std::string keyWithPrefix( key.toString() );
            std::string id  = keyWithPrefix.erase( 0, strElemsPrefix.size() );
            auto        hId = HierarchicalKey( id );
            CRDTSet()->debug( "InElemsNotTombstoned checking datastore key '{}', extracted id '{}'",
                              key.toString(),
                              hId.GetKey() );
            if ( !hId.IsTopLevel() )
            {
                // our prefix matches blocks from other keys i.e. our
                // prefix is "hello" and we have a different key like
                // "hello/bye" so we have a block id like
                // "bye/<block>". If we got the right key, then the id
                // should be the block id only.
                CRDTSet()->debug( "InElemsNotTombstoned skipping non-top-level id '{}'", hId.GetKey() );
                continue;
            }
            // if not tombstoned, we have it
            auto inTombResult = this->InTombsKeyID( aKey, hId.GetKey() );
            CRDTSet()->debug( "InElemsNotTombstoned tomb check for key '{}' id '{}': has_value={}, tombed={}",
                              aKey,
                              hId.GetKey(),
                              inTombResult.has_value(),
                              inTombResult.has_value() ? inTombResult.value() : false );
            if ( inTombResult.has_value() && !inTombResult.value() )
            {
                CRDTSet()->debug( "InElemsNotTombstoned key '{}' has live id '{}'", aKey, hId.GetKey() );
                return true;
            }
        }

        CRDTSet()->debug( "InElemsNotTombstoned key '{}' all ids tombstoned", aKey );
        return false;
    }

    HierarchicalKey CrdtSet::KeyPrefix( const std::string &aKey ) const
    {
        // /namespace/<key>
        return this->namespaceKey_.ChildString( aKey );
    }

    HierarchicalKey CrdtSet::ElemsPrefix( const std::string &aKey ) const
    {
        // /namespace/s/<key>
        return this->KeyPrefix( std::string( elemsNamespace_ ) ).ChildString( aKey );
    }

    HierarchicalKey CrdtSet::TombsPrefix( const std::string &aKey ) const
    {
        // /namespace/t/<key>
        return this->KeyPrefix( std::string( tombsNamespace_ ) ).ChildString( aKey );
    }

    HierarchicalKey CrdtSet::KeysKey( std::string_view aKey ) const
    {
        // /namespace/k/<key>
        return this->KeyPrefix( std::string( keysNamespace_ ) ).ChildString( aKey );
    }

    HierarchicalKey CrdtSet::ValueKey( const std::string &aKey ) const
    {
        // /namespace/k/<key>/v
        return this->KeysKey( aKey ).ChildString( GetValueSuffix() );
    }

    HierarchicalKey CrdtSet::PriorityKey( const std::string &aKey ) const
    {
        // /namespace/k/<key>/p
        return this->KeysKey( aKey ).ChildString( GetPrioritySuffix() );
    }

    outcome::result<uint64_t> CrdtSet::GetPriority( const std::string &aKey ) const
    {
        uint64_t priority     = 0;
        auto     priority_key = this->PriorityKey( aKey );
        if ( auto valueResult = this->GetValueFromDatastore( priority_key ); !valueResult.has_failure() )
        {
            try
            {
                priority = boost::lexical_cast<uint64_t>( valueResult.value() ) - 1;
            }
            catch ( boost::bad_lexical_cast & )
            {
                return outcome::failure( boost::system::error_code{} );
            }
        }
        else if ( valueResult.has_failure() && valueResult.error() != storage::DatabaseError::NOT_FOUND )
        {
            // Return failure only we have other than NOT_FOUND error
            return outcome::failure( valueResult.error() );
        }
        return priority;
    }

    outcome::result<void> CrdtSet::SetPriority( const std::string &aKey, uint64_t aPriority )
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( std::errc::owner_dead );
        }

        auto priority_key = this->PriorityKey( aKey );

        std::string strPriority = std::to_string( aPriority + 1 );

        Buffer keyBuffer;
        keyBuffer.put( priority_key.GetKey() );

        Buffer valueBuffer;
        valueBuffer.put( strPriority );

        return this->dataStore_->put( keyBuffer, valueBuffer );
    }

    outcome::result<void> CrdtSet::SetPriority( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                                const std::string                           &aKey,
                                                uint64_t                                     aPriority )
    {
        if ( aDataStore == nullptr )
        {
            return outcome::failure( std::errc::owner_dead );
        }

        auto priority_key = this->PriorityKey( aKey );

        std::string strPriority = std::to_string( aPriority + 1 );

        Buffer keyBuffer;
        keyBuffer.put( priority_key.GetKey() );

        Buffer valueBuffer;
        valueBuffer.put( strPriority );

        return aDataStore->put( keyBuffer, valueBuffer );
    }

    outcome::result<void> CrdtSet::SetValue( const std::string &aKey,
                                             const std::string &aID,
                                             const Buffer      &aValue,
                                             uint64_t           aPriority )
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto batchDatastore = this->dataStore_->batch();
        auto setValueResult = this->SetValue( batchDatastore, aKey, aID, aValue, aPriority );
        if ( setValueResult.has_failure() )
        {
            return outcome::failure( setValueResult.error() );
        }

        auto commitResult = batchDatastore->commit();
        if ( commitResult.has_failure() )
        {
            return outcome::failure( commitResult.error() );
        }

        // Fire the hook only now that the batch is actually committed -- see the
        // batch overload's doc comment for why this must happen post-commit.
        if ( setValueResult.value() && putHookFunc_ != nullptr )
        {
            putHookFunc_( aKey, aValue, aID );
        }

        return outcome::success();
    }

    outcome::result<bool> CrdtSet::SetValue( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                             const std::string                           &aKey,
                                             const std::string                           &aID,
                                             const Buffer                                &aValue,
                                             uint64_t                                     aPriority )
    {
        if ( aDataStore == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // If this key was tombstoned already, do not store/update the value at all.
        auto isDeletedResult = this->InTombsKeyID( aKey, aID );
        if ( isDeletedResult.has_failure() )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        if ( isDeletedResult.value() )
        {
            //if it's tombstone we just don't add it
            return false;
        }

        auto priorityResult = this->GetPriority( aKey );
        if ( priorityResult.has_failure() )
        {
            return outcome::failure( priorityResult.error() );
        }

        if ( aPriority < priorityResult.value() )
        {
            return false;
        }

        auto valueK = this->ValueKey( aKey );

        if ( aPriority == priorityResult.value() )
        {
            auto valueResult = this->GetValueFromDatastore( valueK );
            if ( valueResult.has_failure() )
            {
                return outcome::failure( valueResult.error() );
            }

            if ( valueResult.value() == std::string( aValue.toString() ) )
            {
                return false;
            }
        }

        // store value
        Buffer valueKeyBuffer;
        valueKeyBuffer.put( valueK.GetKey() );

        BOOST_OUTCOME_TRY( aDataStore->put( valueKeyBuffer, aValue ) );

        // store priority
        BOOST_OUTCOME_TRY( this->SetPriority( aDataStore, aKey, aPriority ) );

        // NOTE: the hook is deliberately NOT invoked here anymore -- this batch
        // has not been committed yet. Callers (this class's single-key SetValue
        // overload, and PutElems) must invoke putHookFunc_ themselves, once their
        // batch commit succeeds, only for keys where this function returned true.
        return true;
    }

    outcome::result<void> CrdtSet::PutElems( std::vector<Element> &aElems, const std::string &aID, uint64_t aPriority )
    {
        if ( aElems.empty() )
        {
            return outcome::success();
        }

        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        // Elements actually written to the batch (SetValue returned true) --
        // hooks for these fire only after the batch below successfully commits
        // (see SetValue's doc comment for why pre-commit hook firing is wrong)
        // AND only after this->mutex_ is released (see below -- a hook that
        // re-enters CrdtSet/CrdtDatastore/GlobalDB synchronously, as
        // SecureCrdt::ReadIfQuorum's callback-driven re-derivation does, must
        // never run while this instance's own mutex_ is still held).
        std::vector<std::pair<std::string, Buffer>> hookPending;

        {
            std::lock_guard lg( this->mutex_ );
            CRDTSet()->debug( "PutElems called with {} elements, id '{}', priority {}",
                              aElems.size(),
                              aID,
                              aPriority );

            auto batchDatastore = this->dataStore_->batch();

            for ( auto &elem : aElems )
            {
                // overwrite the identifier as it would come unset
                elem.set_id( aID );
                auto key = elem.key();
                CRDTSet()->debug( "PutElems writing key '{}' with id '{}'", key, aID );

                // /namespace/s/<key>/<id>
                auto kNamespace = this->ElemsPrefix( key ).ChildString( aID );

                Buffer keyBuffer;
                keyBuffer.put( kNamespace.GetKey() );

                BOOST_OUTCOME_TRY( batchDatastore->put( std::move( keyBuffer ), Buffer() ) );
                // update the value if applicable:
                // * higher priority than we currently have.
                // * not tombstoned before.
                Buffer valueBuffer;
                valueBuffer.put( elem.value() );
                auto setValueResult = this->SetValue( batchDatastore, key, aID, valueBuffer, aPriority );
                if ( setValueResult.has_failure() )
                {
                    return outcome::failure( setValueResult.error() );
                }
                if ( setValueResult.value() )
                {
                    hookPending.emplace_back( key, std::move( valueBuffer ) );
                }
            }
            auto commitResult = batchDatastore->commit();
            if ( commitResult.has_failure() )
            {
                CRDTSet()->error( "PutElems batch commit failed for id '{}'", aID );
                return outcome::failure( commitResult.error() );
            }

            CRDTSet()->debug( "PutElems committed {} elements for id '{}'", aElems.size(), aID );
        } // lg released here -- hooks below run with this->mutex_ NOT held.

        if ( putHookFunc_ != nullptr )
        {
            for ( const auto &[key, value] : hookPending )
            {
                putHookFunc_( key, value, aID );
            }
        }

        return outcome::success();
    }

    outcome::result<void> CrdtSet::PutTombs( const std::vector<Element> &aTombs, const std::string &aID ) const
    {
        if ( aTombs.empty() )
        {
            return outcome::success();
        }

        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto batchDatastore = this->dataStore_->batch();
        CRDTSet()->debug( "PutTombs called with {} tombstones, default id '{}'", aTombs.size(), aID );

        std::vector<std::string> deletedKeys;
        for ( auto tomb : aTombs )
        {
            // /namespace/tombs/<key>/<id>
            if ( tomb.id().empty() )
            {
                tomb.set_id( aID );
            }
            const auto &key        = tomb.key();
            auto        kNamespace = this->TombsPrefix( key ).ChildString( tomb.id() );
            CRDTSet()->debug( "PutTombs writing tombstone for key '{}' id '{}' at '{}'",
                              key,
                              tomb.id(),
                              kNamespace.GetKey() );

            Buffer keyBuffer;
            keyBuffer.put( kNamespace.GetKey() );

            BOOST_OUTCOME_TRY( batchDatastore->put( std::move( keyBuffer ), Buffer() ) );

            // run delete hook only once for all
            // versions of the same element tombstoned
            // in this delta
            deletedKeys.push_back( key );
        }

        BOOST_OUTCOME_TRY( batchDatastore->commit() );
        CRDTSet()->debug( "PutTombs committed {} tombstones", aTombs.size() );

        if ( deleteHookFunc_ )
        {
            for ( const auto &key : deletedKeys )
            {
                deleteHookFunc_( key, aID );
            }
        }

        return outcome::success();
    }

    outcome::result<void> CrdtSet::Merge( const Delta &aDelta, const std::string &aID )
    {
        CRDTSet()->debug( "Merge called with id '{}': {} tombstones, {} elements, priority {}",
                          aID,
                          aDelta.tombstones_size(),
                          aDelta.elements_size(),
                          aDelta.priority() );
        BOOST_OUTCOME_TRY( this->PutTombs( std::vector( aDelta.tombstones().cbegin(), aDelta.tombstones().cend() ), aID ) );

        std::vector elements( aDelta.elements().cbegin(), aDelta.elements().cend() );
        return this->PutElems( elements, aID, aDelta.priority() );
    }

    outcome::result<bool> CrdtSet::InTombsKeyID( const std::string &aKey, const std::string &aID ) const
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto   kNamespace = this->TombsPrefix( aKey ).ChildString( aID );
        Buffer keyBuffer;
        keyBuffer.put( kNamespace.GetKey() );
        return this->dataStore_->contains( keyBuffer );
    }

    void CrdtSet::SetPutHook( const PutHookPtr &putHookPtr )
    {
        this->putHookFunc_ = putHookPtr;
    }

    void CrdtSet::SetDeleteHook( const DeleteHookPtr &deleteHookPtr )
    {
        this->deleteHookFunc_ = deleteHookPtr;
    }

    outcome::result<void> CrdtSet::DataStoreSync( const std::vector<HierarchicalKey> &aKeyList )
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        if ( aKeyList.size() != 4 )
        {
            // Vector hierarchicalkey need enough element.
            return outcome::failure( std::errc::invalid_argument );
        }

        // Put all Hierarchical key to database.
        std::string aKey = aKeyList.at( 0 ).GetKey();
        std::string aID  = aKeyList.at( 1 ).GetKey();
        Buffer      aValue;
        aValue.put( aKeyList.at( 2 ).GetKey() );
        auto aPriority = GetPriority( aKeyList.at( 3 ).GetKey() );
        return SetValue( aKey, aID, aValue, aPriority.value() );
    }

    void CrdtSet::PrintDataStore() const
    {
        if ( dataStore_ )
        {
            std::ofstream logFile( "crdt_data.log", std::ios::out | std::ios::trunc ); // Overwrites the file each time

            if ( !logFile )
            {
                std::cerr << "Failed to open log file for writing!" << std::endl;
                return;
            }

            auto key_values = dataStore_->GetAll();
            for ( const auto &[key, value] : key_values )
            {
                logFile << "[" << key.toString() << "] " << value << std::endl;
            }

            logFile.close();
            std::cout << "Data successfully written to crdt_data.log" << std::endl;
        }
    }

    void CrdtSet::PrintTombs( const std::vector<Element> &aTombs )
    {
        std::cout << "Tombs" << std::endl;
        for ( const auto &tomb : aTombs )
        {
            // /namespace/tombs/<key>/<id>
            std::cout << tomb.key() << ", " << tomb.id() << std::endl;
        }
    }
}
