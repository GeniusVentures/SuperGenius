#include "crdt/crdt_heads.hpp"
#include <storage/database_error.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <utility>

namespace sgns::crdt
{
    CrdtHeads::CrdtHeads( std::shared_ptr<DataStore> aDatastore, const HierarchicalKey &aNamespace ) :
        dataStore_( std::move( aDatastore ) ), namespaceKey_( aNamespace )
    {
        logger_->debug( "Creating heads" );
        auto result = this->PrimeCache();
    }

    CrdtHeads::CrdtHeads( const CrdtHeads &aHeads )
    {
        *this = aHeads;
    }

    CrdtHeads &CrdtHeads::operator=( const CrdtHeads &aHeads )
    {
        if ( this != &aHeads )
        {
            this->dataStore_    = aHeads.dataStore_;
            this->namespaceKey_ = aHeads.namespaceKey_;
            this->cache_        = aHeads.cache_;
        }
        return *this;
    }

    bool CrdtHeads::operator==( const CrdtHeads &aHeads )
    {
        bool returnEqual  = true;
        returnEqual      &= this->dataStore_ == aHeads.dataStore_;
        returnEqual      &= this->namespaceKey_ == aHeads.namespaceKey_;
        returnEqual      &= this->cache_ == aHeads.cache_;
        return returnEqual;
    }

    bool CrdtHeads::operator!=( const CrdtHeads &aHeads )
    {
        return !( *this == aHeads );
    }

    HierarchicalKey CrdtHeads::GetNamespaceKey() const
    {
        return this->namespaceKey_;
    }

    outcome::result<HierarchicalKey> CrdtHeads::GetKey( const CID &aCid )
    {
        // /<namespace>/<cid>
        auto cidToStringResult = aCid.toString();
        if ( cidToStringResult.has_failure() )
        {
            return outcome::failure( cidToStringResult.error() );
        }

        return this->namespaceKey_.ChildString( cidToStringResult.value() );
    }

    outcome::result<HierarchicalKey> CrdtHeads::GetKeyForTopic( const std::string &topic, const CID &aCid )
    {
        auto topicNs = namespaceKey_.ChildString( std::string( topic ) );

        auto cidStr = aCid.toString();
        if ( cidStr.has_failure() )
        {
            return outcome::failure( cidStr.error() );
        }

        return topicNs.ChildString( cidStr.value() );
    }

    outcome::result<void> CrdtHeads::Write( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                            const CID                                   &aCid,
                                            uint64_t                                     aHeight,
                                            const std::string                           &topic )
    {
        auto getKeyResult = GetKeyForTopic( topic, aCid );
        if ( getKeyResult.has_failure() )
        {
            return outcome::failure( getKeyResult.error() );
        }

        auto strHeight = std::to_string( aHeight );

        Buffer keyBuffer;
        keyBuffer.put( getKeyResult.value().GetKey() );

        Buffer valueBuffer;
        valueBuffer.put( strHeight );

        return aDataStore->put( keyBuffer, valueBuffer );
    }

    outcome::result<void> CrdtHeads::Delete( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                             const CID                                   &aCid,
                                             const std::string                           &topic )
    {
        if ( aDataStore == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto getKeyResult = this->GetKeyForTopic( topic, aCid );
        if ( getKeyResult.has_failure() )
        {
            return outcome::failure( getKeyResult.error() );
        }

        Buffer keyBuffer;
        keyBuffer.put( getKeyResult.value().GetKey() );

        return aDataStore->remove( keyBuffer );
    }

    bool CrdtHeads::IsHead( const CID &cid, const std::string &topic /* = "" */ )
    {
        std::lock_guard lock( mutex_ );

        if ( topic.empty() )
        {
            for ( const auto &tMap : cache_ )
            {
                if ( tMap.second.find( cid ) != tMap.second.end() )
                {
                    return true;
                }
            }
            return false;
        }

        const auto topicIt = cache_.find( topic );
        if ( topicIt == cache_.end() )
        {
            return false;
        }

        return topicIt->second.find( cid ) != topicIt->second.end();
    }

    outcome::result<uint64_t> CrdtHeads::GetHeadHeight( const CID &aCid, const std::string &topic /* = "" */ )
    {
        std::lock_guard lg( this->mutex_ );

        if ( !this->IsHead( aCid, topic ) )
        {
            return 0;
        }
        if ( topic.empty() )
        {
            for ( auto &t : cache_ )
            {
                auto it = t.second.find( aCid );
                if ( it != t.second.end() )
                {
                    return it->second;
                }
            }
            return 0u;
        }
        auto tit = cache_.find( topic );
        if ( tit == cache_.end() )
        {
            return 0u;
        }
        auto it = tit->second.find( aCid );
        return it == tit->second.end() ? 0u : it->second;
    }

    outcome::result<int> CrdtHeads::GetLength( const std::string &topic /* = "" */ )
    {
        std::lock_guard lock( mutex_ );

        if ( topic.empty() )
        {
            size_t total = 0;
            for ( const auto &kv : cache_ )
            {
                total += kv.second.size();
            }
            return static_cast<int>( total );
        }
        return static_cast<int>( cache_[topic].size() );
    }

    outcome::result<void> CrdtHeads::Add( const CID &aCid, uint64_t aHeight, const std::string &topic )
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto batchDatastore = this->dataStore_->batch();
        auto writeResult    = this->Write( batchDatastore, aCid, aHeight, topic );
        if ( writeResult.has_failure() )
        {
            return outcome::failure( writeResult.error() );
        }

        auto commitResult = batchDatastore->commit();
        if ( commitResult.has_failure() )
        {
            return outcome::failure( commitResult.error() );
        }

        {
            logger_->debug( "Add: Inserting {} with topic {} as head", aCid.toString().value(), topic );

            std::lock_guard lg( this->mutex_ );
            this->cache_[topic][aCid] = aHeight;
        }
        return outcome::success();
    }

    outcome::result<void> CrdtHeads::Replace( const CID         &aCidHead,
                                              const CID         &aNewHeadCid,
                                              uint64_t           aHeight,
                                              const std::string &topic )
    {
        if ( this->dataStore_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto batchDatastore = this->dataStore_->batch();
        auto writeResult    = this->Write( batchDatastore, aNewHeadCid, aHeight, topic );
        if ( writeResult.has_failure() )
        {
            return outcome::failure( writeResult.error() );
        }

        auto deleteResult = Delete( batchDatastore, aCidHead, topic );
        if ( deleteResult.has_failure() )
        {
            return outcome::failure( deleteResult.error() );
        }

        auto commitResult = batchDatastore->commit();
        if ( commitResult.has_failure() )
        {
            return outcome::failure( commitResult.error() );
        }

        {
            logger_->debug( "Replace: Replacing {} with {} as head for topic {}",
                            aCidHead.toString().value(),
                            aNewHeadCid.toString().value(),
                            topic );

            std::lock_guard lg( this->mutex_ );
            cache_[topic].erase( aCidHead );
            cache_[topic][aNewHeadCid] = aHeight;
        }
        return outcome::success();
    }

    outcome::result<CrdtHeads::CRDTListResult> CrdtHeads::GetList( const std::set<std::string> &topics )
    {
        CRDTHeadList result_heads;
        uint64_t     max_value = 0;
        logger_->debug( "GetList: Getting list of CIDs" );
        for ( const auto &[current_topic, cid_map] : cache_ )
        {
            if ( !topics.empty() && topics.find( current_topic ) == topics.end() )
            {
                continue;
            }

            for ( const auto &[cid, value] : cid_map )
            {
                result_heads[current_topic].insert( cid );
                max_value = std::max( max_value, value );
            }
        }

        //if ( result_heads.empty() )
        //{
        //    return outcome::failure( boost::system::error_code{} );
        //}

        return outcome::success( CRDTListResult{ result_heads, max_value } );
    }

    outcome::result<void> CrdtHeads::PrimeCache()
    {
        // builds the heads cache based on what's in storage
        const auto strNamespace = this->namespaceKey_.GetKey();
        logger_->debug( "PrimeCache: starting for namespace '{}'", strNamespace );

        Buffer keyPrefixBuffer;
        keyPrefixBuffer.put( strNamespace );

        auto queryResult = this->dataStore_->query( keyPrefixBuffer );
        if ( queryResult.has_failure() )
        {
            logger_->error( "PrimeCache: query failed: {}", queryResult.error().message() );
            return outcome::failure( queryResult.error() );
        }
        logger_->debug( "PrimeCache: retrieved {} entries from datastore", queryResult.value().size() );

        size_t loadedCount = 0;
        for ( const auto &bufferKeyAndValue : queryResult.value() )
        {
            // full key is "/<namespace>/<topic>/<cid>"
            auto        keyView = bufferKeyAndValue.first.toString();
            std::string full( keyView.data(), keyView.size() );

            if ( full.size() <= strNamespace.size() + 1 )
            {
                logger_->debug( "PrimeCache: skipping too-short key '{}'", full );
                continue;
            }

            std::string rel = full.substr( strNamespace.size() + 1 );

            auto sepPos = rel.find( '/' );
            if ( sepPos == std::string::npos )
            {
                logger_->warn( "PrimeCache: malformed key '{}'", rel );
                continue;
            }

            std::string topic  = rel.substr( 0, sepPos );
            std::string strCid = rel.substr( sepPos + 1 );

            if ( topic.empty() || strCid.empty() )
            {
                logger_->warn( "PrimeCache: empty topic or CID in '{}'", rel );
                continue;
            }

            auto cidResult = CID::fromString( strCid );
            if ( cidResult.has_failure() )
            {
                logger_->warn( "PrimeCache: invalid CID '{}' in key '{}'", strCid, full );
                continue;
            }
            CID cid( cidResult.value() );

            uint64_t height = 0;
            try
            {
                height = boost::lexical_cast<uint64_t>( bufferKeyAndValue.second.toString() );
            }
            catch ( const boost::bad_lexical_cast & )
            {
                logger_->warn( "PrimeCache: could not parse height '{}' for CID '{}'",
                               bufferKeyAndValue.second.toString(),
                               strCid );
                continue;
            }

            std::lock_guard lg( this->mutex_ );
            this->cache_[topic][cid] = height;
            loadedCount++;
            logger_->trace( "PrimeCache: loaded head [topic='{}', cid='{}', height={}]",
                            topic,
                            cid.toString().value(),
                            height );
        }

        logger_->debug( "PrimeCache: completed, loaded {} entries into cache", loadedCount );
        return outcome::success();
    }

}
