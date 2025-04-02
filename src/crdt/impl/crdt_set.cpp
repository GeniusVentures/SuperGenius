#include "crdt/crdt_set.hpp"
#include <storage/database_error.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <utility>
#include <fstream>

namespace sgns::crdt
{

    CrdtSet::CrdtSet( std::shared_ptr<DataStore> aDatastore,
                      const HierarchicalKey     &aNamespace,
                      PutHookPtr                 aPutHookPtr,
                      DeleteHookPtr              aDeleteHookPtr ) :
        dataStore_( std::move( aDatastore ) ),
        namespaceKey_( aNamespace ),
        putHookFunc_( std::move( aPutHookPtr ) ),
        deleteHookFunc_( std::move( aDeleteHookPtr ) )
    {
    }

    CrdtSet::CrdtSet( const CrdtSet &aSet )
    {
        *this = aSet;
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

    bool CrdtSet::operator==( const CrdtSet &aSet )
    {
        bool returnEqual  = true;
        returnEqual      &= this->dataStore_ == aSet.dataStore_;
        returnEqual      &= this->namespaceKey_ == aSet.namespaceKey_;
        return returnEqual;
    }

    bool CrdtSet::operator!=( const CrdtSet &aSet )
    {
        return !( *this == aSet );
    }

    outcome::result<std::string> CrdtSet::GetValueFromDatastore( const HierarchicalKey &aKey )
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

        std::string strValue = std::string( bufferValueResult.value().toString() );
        return strValue;
    }

    outcome::result<std::shared_ptr<CrdtSet::Delta>> CrdtSet::CreateDeltaToAdd( const std::string &aKey,
                                                                                const std::string &aValue )
    {
        auto delta   = std::make_shared<CrdtSet::Delta>();
        auto element = delta->add_elements();
        element->set_key( aKey );
        element->set_value( aValue );

        return delta;
    }

    outcome::result<std::shared_ptr<CrdtSet::Delta>> CrdtSet::CreateDeltaToRemove( const std::string &aKey )
    {
        auto delta = std::make_shared<CrdtSet::Delta>();
        // /namespace/s/<key>
        auto prefix         = this->ElemsPrefix( aKey );
        auto strElemsPrefix = prefix.GetKey();

        Buffer keyPrefixBuffer;
        keyPrefixBuffer.put( strElemsPrefix );
        auto queryResult = this->dataStore_->query( keyPrefixBuffer );
        if ( queryResult.has_failure() )
        {
            return outcome::failure( queryResult.error() );
        }

        for ( const auto &bufferKeyAndValue : queryResult.value() )
        {
            std::string keyWithPrefix = std::string( bufferKeyAndValue.first.toString() );
            std::string id            = keyWithPrefix.erase( 0, strElemsPrefix.size() );

            auto hId = HierarchicalKey( id );

            if ( !hId.IsTopLevel() )
            {
                continue;
            }

            // check if its already tombed, which case don't add it to the
            // Remove delta set.
            auto isDeletedResult = this->InTombsKeyID( aKey, hId.GetKey() );
            if ( isDeletedResult.has_value() && !isDeletedResult.value() )
            {
                auto tombstone = delta->add_tombstones();
                tombstone->set_key( aKey );
                tombstone->set_id( hId.GetKey() );
            }
        }

        return delta;
    }

    outcome::result<CrdtSet::Buffer> CrdtSet::GetElement( const std::string &aKey )
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
        const std::string &aPrefix,
        const QuerySuffix &aSuffix /*=QuerySuffix::QUERY_ALL*/ )
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
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
            auto inSetResult = this->InElemsNotTombstoned( std::string( element.first.toString() ) );
            if ( inSetResult.has_failure() || !inSetResult.value() )
            {
                continue;
            }

            std::string key = std::string( element.first.toString() );
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

    outcome::result<bool> CrdtSet::IsValueInSet( const std::string &aKey )
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

    outcome::result<bool> CrdtSet::InElemsNotTombstoned( const std::string &aKey )
    {
        // /namespace/elems/<key>
        auto prefix         = this->ElemsPrefix( aKey );
        auto strElemsPrefix = prefix.GetKey();

        Buffer keyPrefixBuffer;
        keyPrefixBuffer.put( strElemsPrefix );
        auto queryResult = this->dataStore_->query( keyPrefixBuffer );
        if ( queryResult.has_failure() )
        {
            return outcome::failure( queryResult.error() );
        }

        if ( queryResult.value().empty() )
        {
            return true;
        }

        for ( const auto &bufferKeyAndValue : queryResult.value() )
        {
            std::string keyWithPrefix = std::string( bufferKeyAndValue.first.toString() );
            std::string id            = keyWithPrefix.erase( 0, strElemsPrefix.size() );
            auto        hId           = HierarchicalKey( id );
            if ( !hId.IsTopLevel() )
            {
                // our prefix matches blocks from other keys i.e. our
                // prefix is "hello" and we have a different key like
                // "hello/bye" so we have a block id like
                // "bye/<block>". If we got the right key, then the id
                // should be the block id only.
                continue;
            }
            // if not tombstoned, we have it
            auto inTombResult = this->InTombsKeyID( aKey, hId.GetKey() );
            if ( inTombResult.has_value() && !inTombResult.value() )
            {
                return true;
            }
        }

        return false;
    }

    HierarchicalKey CrdtSet::KeyPrefix( const std::string &aKey )
    {
        // /namespace/<key>
        return this->namespaceKey_.ChildString( aKey );
    }

    HierarchicalKey CrdtSet::ElemsPrefix( const std::string &aKey )
    {
        // /namespace/s/<key>
        return this->KeyPrefix( std::string( elemsNamespace_ ) ).ChildString( aKey );
    }

    HierarchicalKey CrdtSet::TombsPrefix( const std::string &aKey )
    {
        // /namespace/t/<key>
        return this->KeyPrefix( std::string( tombsNamespace_ ) ).ChildString( aKey );
    }

    HierarchicalKey CrdtSet::KeysKey( const std::string &aKey )
    {
        // /namespace/k/<key>
        return this->KeyPrefix( std::string( keysNamespace_ ) ).ChildString( aKey );
    }

    HierarchicalKey CrdtSet::ValueKey( const std::string &aKey )
    {
        // /namespace/k/<key>/v
        return this->KeysKey( aKey ).ChildString( GetValueSuffix() );
    }

    HierarchicalKey CrdtSet::PriorityKey( const std::string &aKey )
    {
        // /namespace/k/<key>/p
        return this->KeysKey( aKey ).ChildString( GetPrioritySuffix() );
    }

    outcome::result<uint64_t> CrdtSet::GetPriority( const std::string &aKey )
    {
        uint64_t priority    = 0;
        auto     prioK       = this->PriorityKey( aKey );
        auto     valueResult = this->GetValueFromDatastore( prioK );
        if ( !valueResult.has_failure() )
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
            return outcome::failure( boost::system::error_code{} );
        }

        auto prioK = this->PriorityKey( aKey );

        std::string strPriority = std::to_string( aPriority + 1 );

        Buffer keyBuffer;
        keyBuffer.put( prioK.GetKey() );

        Buffer valueBuffer;
        valueBuffer.put( strPriority );

        return this->dataStore_->put( keyBuffer, valueBuffer );
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

        return outcome::success();
    }

    outcome::result<void> CrdtSet::SetValue( const std::unique_ptr<storage::BufferBatch> &aDataStore,
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
        if ( isDeletedResult.has_failure() || isDeletedResult.value() )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto priorityResult = this->GetPriority( aKey );
        if ( priorityResult.has_failure() )
        {
            return outcome::failure( priorityResult.error() );
        }

        if ( aPriority < priorityResult.value() )
        {
            return outcome::success();
        }

        auto valueK = this->ValueKey( aKey );

        if ( aPriority == priorityResult.value() )
        {
            auto valueResult = this->GetValueFromDatastore( valueK );
            if ( valueResult.has_failure() )
            {
                return outcome::failure( valueResult.error() );
            }

            // if bytes.Compare(valueResult.value(), aValue) >= 0 {
            // comparing two data lexicographically,  valueResult >= aValue, no need to store value
            if ( !boost::lexicographical_compare<std::string, std::string>( valueResult.value(),
                                                                            std::string( aValue.toString() ) ) )
            {
                return outcome::success();
            }
        }

        // store value
        Buffer valueKeyBuffer;
        valueKeyBuffer.put( valueK.GetKey() );

        auto putResult = aDataStore->put( valueKeyBuffer, aValue );
        if ( putResult.has_failure() )
        {
            return outcome::failure( putResult.error() );
        }

        // store priority
        auto setPriorityResult = this->SetPriority( aKey, aPriority );
        if ( setPriorityResult.has_failure() )
        {
            return outcome::failure( setPriorityResult.error() );
        }

        // trigger add hook
        if ( this->putHookFunc_ != nullptr )
        {
            putHookFunc_( aKey, aValue );
        }

        return outcome::success();
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

        std::lock_guard lg( this->mutex_ );

        auto batchDatastore = this->dataStore_->batch();

        for ( auto &elem : aElems )
        {
            // overwrite the identifier as it would come unset
            elem.set_id( aID );
            auto key = elem.key();

            // /namespace/s/<key>/<id>
            auto kNamespace = this->ElemsPrefix( key ).ChildString( aID );

            Buffer keyBuffer;
            keyBuffer.put( kNamespace.GetKey() );

            auto putResult = batchDatastore->put( std::move( keyBuffer ), Buffer() );
            if ( putResult.has_error() )
            {
                return outcome::failure( putResult.error() );
            }
            // update the value if applicable:
            // * higher priority than we currently have.
            // * not tombstoned before.
            Buffer valueBuffer;
            valueBuffer.put( elem.value() );
            auto setValueResult = this->SetValue( batchDatastore, key, aID, std::move( valueBuffer ), aPriority );
            if ( setValueResult.has_failure() )
            {
                return outcome::failure( setValueResult.error() );
            }
        }
        auto commitResult = batchDatastore->commit();
        if ( commitResult.has_failure() )
        {
            return outcome::failure( commitResult.error() );
        }

        return outcome::success();
    }

    outcome::result<void> CrdtSet::PutTombs( const std::vector<Element> &aTombs )
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

        std::vector<std::string> deletedKeys;
        for ( const auto &tomb : aTombs )
        {
            // /namespace/tombs/<key>/<id>
            const auto &key        = tomb.key();
            auto        kNamespace = this->TombsPrefix( key ).ChildString( tomb.id() );

            Buffer keyBuffer;
            keyBuffer.put( kNamespace.GetKey() );

            auto putResult = batchDatastore->put( std::move( keyBuffer ), Buffer() );
            if ( putResult.has_error() )
            {
                return outcome::failure( putResult.error() );
            }

            // run delete hook only once for all
            // versions of the same element tombstoned
            // in this delta
            deletedKeys.push_back( key );
        }

        auto commitResult = batchDatastore->commit();
        if ( commitResult.has_failure() )
        {
            return outcome::failure( commitResult.error() );
        }

        if ( deleteHookFunc_ )
        {
            for ( const auto &key : deletedKeys )
            {
                deleteHookFunc_( key );
            }
        }

        return outcome::success();
    }

    outcome::result<void> CrdtSet::Merge( const std::shared_ptr<Delta> &aDelta, const std::string &aID )
    {
        if ( aDelta == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto putTombsResult = this->PutTombs( std::vector( aDelta->tombstones().begin(), aDelta->tombstones().end() ) );
        if ( putTombsResult.has_failure() )
        {
            return outcome::failure( putTombsResult.error() );
        }

        std::vector<Element> elements( aDelta->elements().begin(), aDelta->elements().end() );
        return this->PutElems( elements, aID, aDelta->priority() );
    }

    outcome::result<bool> CrdtSet::InTombsKeyID( const std::string &aKey, const std::string &aID )
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
        // Put all Hierarchical key to database.
        if ( aKeyList.size() != 4 )
        {
            // Vector hierarchicalkey need enough element.
            return outcome::failure( boost::system::error_code{} );
        }
        std::string aKey = aKeyList.at( 0 ).GetKey();
        std::string aID  = aKeyList.at( 1 ).GetKey();
        Buffer      aValue;
        aValue.put( aKeyList.at( 2 ).GetKey() );
        auto aPriority = GetPriority( aKeyList.at( 3 ).GetKey() );
        return SetValue( aKey, aID, aValue, aPriority.value() );
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

    void CrdtSet::PrintElements( const std::vector<Element> &aElems )
    {
        std::cout << "Elems" << std::endl;
        for ( const auto &tomb : aElems )
        {
            // /namespace/tombs/<key>/<id>
            std::cout << tomb.key() << ", " << tomb.id() << std::endl;
        }
    }

    void CrdtSet::PrintDataStore()
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
}
