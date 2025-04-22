#include "crdt/crdt_heads.hpp"
#include <storage/database_error.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <sstream>
#include <utility>

namespace sgns::crdt
{
    CrdtHeads::CrdtHeads( std::shared_ptr<DataStore> aDatastore, const HierarchicalKey &aNamespace ) :
        dataStore_( std::move( aDatastore ) ), namespaceKey_( aNamespace )
    {
        auto result = this->PrimeCache();
    }

  CrdtHeads::CrdtHeads(const CrdtHeads& aHeads)
  {
    *this = aHeads;
  }

  CrdtHeads& CrdtHeads::operator=(const CrdtHeads& aHeads)
  {
    if (this != &aHeads)
    {
      this->dataStore_ = aHeads.dataStore_;
      this->namespaceKey_ = aHeads.namespaceKey_;
      this->cache_ = aHeads.cache_;
    }
    return *this;
  }

  bool CrdtHeads::operator==(const CrdtHeads& aHeads)
  {
    bool returnEqual = true;
    returnEqual &= this->dataStore_ == aHeads.dataStore_;
    returnEqual &= this->namespaceKey_ == aHeads.namespaceKey_;
    returnEqual &= this->cache_ == aHeads.cache_;
    return returnEqual;
  }

  bool CrdtHeads::operator!=(const CrdtHeads& aHeads)
  {
    return !(*this == aHeads);
  }

  HierarchicalKey CrdtHeads::GetNamespaceKey() const
  {
    return this->namespaceKey_;
  }

  outcome::result<HierarchicalKey> CrdtHeads::GetKey(const CID& aCid)
  {
    // /<namespace>/<cid>
    auto cidToStringResult = aCid.toString();
    if (cidToStringResult.has_failure())
    {
      return outcome::failure(cidToStringResult.error());
    }

    return this->namespaceKey_.ChildString(cidToStringResult.value());
  }

outcome::result<void> CrdtHeads::Write(
    const std::unique_ptr<storage::BufferBatch> &aDataStore,
    const CID &aCid,
    uint64_t aHeight,
    std::optional<std::string> topic)
{
    if (aDataStore == nullptr)
    {
        return outcome::failure(boost::system::error_code{});
    }

    auto getKeyResult = this->GetKey(aCid);
    if (getKeyResult.has_failure())
    {
        return outcome::failure(getKeyResult.error());
    }

    std::ostringstream oss;
    oss << aHeight;
    if (topic.has_value() && !topic->empty())
    {
        oss << ":" << topic.value();
    }
    std::string strValue = oss.str();

    Buffer keyBuffer;
    keyBuffer.put(getKeyResult.value().GetKey());

    Buffer valueBuffer;
    valueBuffer.put(strValue);

    return aDataStore->put(keyBuffer, valueBuffer);
}


  outcome::result<void> CrdtHeads::Delete(const std::unique_ptr<storage::BufferBatch>& aDataStore, const CID& aCid)
  {
    if (aDataStore == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto getKeyResult = this->GetKey(aCid);
    if (getKeyResult.has_failure())
    {
      return outcome::failure(getKeyResult.error());
    }

    Buffer keyBuffer;
    keyBuffer.put(getKeyResult.value().GetKey());

    return aDataStore->remove(keyBuffer);
  }

  bool CrdtHeads::IsHead(const CID& aCid)
  {
    // IsHead returns if a given cid is among the current heads.
    std::lock_guard lg(this->mutex_);
    return cache_.find(aCid) != cache_.end();
  }

  outcome::result<uint64_t> CrdtHeads::GetHeadHeight(const CID& aCid)
  {
    std::lock_guard lg(this->mutex_);

    if (!this->IsHead(aCid))
    {
        return 0;
    }
    return this->cache_[aCid].first;
  }

outcome::result<std::string> CrdtHeads::GetHeadTopic( const CID &aCid )
{
    std::lock_guard<std::recursive_mutex> lg( this->mutex_ );
    auto                                  it = this->cache_.find( aCid );
    if ( it == this->cache_.end() )
    {
        return outcome::failure( boost::system::error_code{} );
    }
    return it->second.second;
}

  outcome::result<int> CrdtHeads::GetLength()
  {
    std::lock_guard lg(this->mutex_);
    return this->cache_.size();
  }

outcome::result<void> CrdtHeads::Add( const CID &aCid, uint64_t aHeight, std::optional<std::string> topic )
{
    if (this->dataStore_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto batchDatastore = this->dataStore_->batch();
    auto writeResult = this->Write(batchDatastore, aCid, aHeight, topic);
    if (writeResult.has_failure())
    {
      return outcome::failure(writeResult.error());
    }
    auto commitResult = batchDatastore->commit();
    if (commitResult.has_failure())
    {
      return outcome::failure(commitResult.error());
    }

    std::lock_guard lg(this->mutex_);
    this->cache_[aCid] = std::make_pair( aHeight, topic.value_or( "" ) );
    return outcome::success();
}

outcome::result<void> CrdtHeads::Replace( const CID                 &aCidHead,
                                            const CID                 &aNewHeadCid,
                                            uint64_t                   aHeight,
                                            std::optional<std::string> topic )
{
    if (this->dataStore_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }
    auto batchDatastore = this->dataStore_->batch();
    auto writeResult    = this->Write( batchDatastore, aNewHeadCid, aHeight, topic );
    if (writeResult.has_failure())
    {
      return outcome::failure(writeResult.error());
    }

    auto deleteResult = this->Delete(batchDatastore, aCidHead);
    if (deleteResult.has_failure())
    {
      return outcome::failure(deleteResult.error());
    }

    auto commitResult = batchDatastore->commit();
    if (commitResult.has_failure())
    {
      return outcome::failure(commitResult.error());
    }

    std::lock_guard lg(this->mutex_);
    this->cache_.erase(aCidHead);
    cache_[aNewHeadCid] = std::make_pair( aHeight, topic.value_or( "" ) );
    return outcome::success();
  }

  outcome::result<void> CrdtHeads::GetList(std::vector<CID>& aHeads, uint64_t& aMaxHeight)
  {
    std::lock_guard lg(this->mutex_);
    aMaxHeight = 0;
    aHeads.clear();
    for ( const auto &kv : cache_ )
    {
        aHeads.push_back( kv.first );
        aMaxHeight = std::max( aMaxHeight, kv.second.first );
    }
    return outcome::success();
}

outcome::result<void> CrdtHeads::PrimeCache()
{
    const auto strNamespace = this->namespaceKey_.GetKey();
    Buffer keyPrefixBuffer;
    keyPrefixBuffer.put(strNamespace);

    auto queryResult = this->dataStore_->query(keyPrefixBuffer);
    if (queryResult.has_failure())
    {
        return outcome::failure(queryResult.error());
    }

    for (const auto &bufferKeyAndValue : queryResult.value())
    {
        std::string keyWithNamespace = std::string(bufferKeyAndValue.first.toString());
        std::string strCid = keyWithNamespace.erase(0, strNamespace.size() + 1);

        auto cidResult = CID::fromString(strCid);
        if (cidResult.has_failure())
        {
            continue;
        }
        CID cid(cidResult.value());
        std::string valStr = std::string(bufferKeyAndValue.second.toString());
        std::string topic = "";
        uint64_t height = 0;

        size_t sepPos = valStr.find(':');
        if (sepPos != std::string::npos)
        {
            try
            {
                height = boost::lexical_cast<uint64_t>(valStr.substr(0, sepPos));
                topic = valStr.substr(sepPos + 1);
            }
            catch (boost::bad_lexical_cast&)
            {
                return outcome::failure(boost::system::error_code{});
            }
        }
        else
        {
            try
            {
                height = boost::lexical_cast<uint64_t>(valStr);
            }
            catch (boost::bad_lexical_cast&)
            {
                return outcome::failure(boost::system::error_code{});
            }
        }

        this->cache_[cid] = std::make_pair(height, topic);
    }
    return outcome::success();
}

}
