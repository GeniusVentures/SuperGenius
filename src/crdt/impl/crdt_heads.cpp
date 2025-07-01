#include "crdt/crdt_heads.hpp"
#include "crdt/proto/heads.pb.h"
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

    outcome::result<void> CrdtHeads::Write( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                            const CID                                   &aCid,
                                            uint64_t                                     aHeight,
                                            const std::string                           &topic )
    {
        if ( aDataStore == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto getKeyResult = this->GetKey( aCid );
        if ( getKeyResult.has_failure() )
        {
            return outcome::failure( getKeyResult.error() );
        }

        // serialize HeadInfo proto
        pb::HeadInfo info;
        info.set_height( aHeight );
        info.set_topic( topic );
        std::string payload;
        info.SerializeToString( &payload );

        Buffer keyBuffer;
        keyBuffer.put( getKeyResult.value().GetKey() );

        Buffer               valueBuffer;
        std::vector<uint8_t> dataVec( payload.begin(), payload.end() );
        valueBuffer.put( dataVec );

        return aDataStore->put( keyBuffer, valueBuffer );
    }

    outcome::result<void> CrdtHeads::Delete( const std::unique_ptr<storage::BufferBatch> &aDataStore, const CID &aCid )
    {
        if ( aDataStore == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto getKeyResult = this->GetKey( aCid );
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
            std::lock_guard lg( this->mutex_ );
            this->cache_[topic][aCid] = height;
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

        auto deleteResult = Delete( batchDatastore, aCidHead );
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
            std::lock_guard lg( this->mutex_ );
            cache_[topic].erase( aCidHead );
            cache_[topic][aNewHeadCid] = aHeight;
        }
        return outcome::success();
    }

    outcome::result<void> CrdtHeads::GetList( std::vector<CID>  &aHeads,
                                              uint64_t          &aMaxHeight,
                                              const std::string &topic /* = "" */ )
    {
        aHeads.clear();
        aMaxHeight = 0;

        const auto addFrom = [&]( const auto &submap )
        {
            for ( const auto &kv : submap )
            {
                aHeads.push_back( kv.first );
                aMaxHeight = std::max( aMaxHeight, kv.second );
            }
        };

        if ( topic.empty() )
        {
            for ( const auto &kv : cache_ )
            {
                addFrom( kv.second );
            }
        }
        else if ( const auto it = cache_.find( topic ); it != cache_.end() )
        {
            addFrom( it->second );
        }

        return outcome::success();
    }

    outcome::result<void> CrdtHeads::PrimeCache()
    {
        const auto strNamespace = this->namespaceKey_.GetKey();
        Buffer     keyPrefixBuffer;
        keyPrefixBuffer.put( strNamespace );

        auto queryResult = this->dataStore_->query( keyPrefixBuffer );
        if ( queryResult.has_failure() )
        {
            return outcome::failure( queryResult.error() );
        }

        for ( const auto &bufferKeyAndValue : queryResult.value() )
        {
            std::string keyWithNamespace = bufferKeyAndValue.first.toString();
            std::string strCid           = keyWithNamespace.erase( 0, strNamespace.size() + 1 );

            auto cidResult = CID::fromString( strCid );
            if ( cidResult.has_failure() )
            {
                continue;
            }
            CID cid( cidResult.value() );

            pb::HeadInfo info;
            info.ParseFromArray( bufferKeyAndValue.second.data(), bufferKeyAndValue.second.size() );
            uint64_t height = info.height();

            std::lock_guard lg( this->mutex_ );
            this->cache_[info.topic()][cid] = height;
        }

        return outcome::success();
    }

}
