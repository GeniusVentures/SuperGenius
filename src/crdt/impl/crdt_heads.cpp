#include "crdt/crdt_heads.hpp"
#include <storage/database_error.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <utility>
#include <boost/outcome/utils.hpp>

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

  bool CrdtHeads::operator==(const CrdtHeads& aHeads ) const
  {
    bool returnEqual = true;
    returnEqual &= this->dataStore_ == aHeads.dataStore_;
    returnEqual &= this->namespaceKey_ == aHeads.namespaceKey_;
    returnEqual &= this->cache_ == aHeads.cache_;
    return returnEqual;
  }

  bool CrdtHeads::operator!=(const CrdtHeads& aHeads) const
  {
    return !(*this == aHeads);
  }

  HierarchicalKey CrdtHeads::GetNamespaceKey() const
  {
    return this->namespaceKey_;
  }

  outcome::result<HierarchicalKey> CrdtHeads::GetKey(const CID& aCid ) const
  {
    // /<namespace>/<cid>
    BOOST_OUTCOME_TRY(auto&& cidToString, aCid.toString());
    return this->namespaceKey_.ChildString(cidToString);
  }

  outcome::result<void> CrdtHeads::Write( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                          const CID                                   &aCid,
                                          uint64_t                                     aHeight )
  {
    if (aDataStore == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    BOOST_OUTCOME_TRY(auto&& key, this->GetKey(aCid));

    std::string strHeight = std::to_string( aHeight );

    Buffer keyBuffer;
    keyBuffer.put(key.GetKey());

    Buffer valueBuffer;
    valueBuffer.put(strHeight);

    return aDataStore->put(keyBuffer, valueBuffer);
  }

  outcome::result<void> CrdtHeads::Delete(const std::unique_ptr<storage::BufferBatch>& aDataStore, const CID& aCid)
  {
    if (aDataStore == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    BOOST_OUTCOME_TRY(auto&& key, this->GetKey(aCid));

    Buffer keyBuffer;
    keyBuffer.put(key.GetKey());

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

    return this->cache_[aCid];
  }

  outcome::result<int> CrdtHeads::GetLength()
  {
    std::lock_guard lg(this->mutex_);
    return this->cache_.size();
  }

  outcome::result<void> CrdtHeads::Add( const CID &aCid, uint64_t aHeight )
  {
    if (this->dataStore_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto batchDatastore = this->dataStore_->batch();
    auto writeResult = this->Write(batchDatastore, aCid, aHeight);
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
    this->cache_[aCid] = aHeight;
    return outcome::success();
  }

  outcome::result<void> CrdtHeads::Replace( const CID &aCidHead, const CID &aNewHeadCid, uint64_t aHeight )
  {
    if (this->dataStore_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto batchDatastore = this->dataStore_->batch();
    auto writeResult = this->Write(batchDatastore, aNewHeadCid, aHeight);
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
    this->cache_[aNewHeadCid] = aHeight;
    return outcome::success();
  }

  std::pair<std::vector<CID>, uint64_t> CrdtHeads::GetList()
  {
    std::vector<CID> heads;
    uint64_t max_height = 0;

    const std::lock_guard lg(this->mutex_);
    for (const auto &[head, height] : this->cache_)
    {
      heads.push_back(head);
      max_height = std::max(max_height, height);
    }

    return {heads, max_height};
  }

  outcome::result<void> CrdtHeads::PrimeCache()
  {
    // builds the heads cache based on what's in storage
    const auto strNamespace = this->namespaceKey_.GetKey();
    Buffer keyPrefixBuffer;
    keyPrefixBuffer.put(strNamespace);
    BOOST_OUTCOME_TRY(auto&& query, this->dataStore_->query(keyPrefixBuffer));

    for ( const auto &[key, height_str] : query)
    {
      std::string keyWithNamespace = std::string(key.toString());
      std::string strCid = keyWithNamespace.erase(0, strNamespace.size() + 1);

      auto cidResult = CID::fromString(strCid);
      if (cidResult.has_failure())
      {
        continue;
      }
      CID cid(cidResult.value());

      uint64_t height = 0;
      try
      {
        height = boost::lexical_cast<uint64_t>(height_str.toString());
      }
      catch (boost::bad_lexical_cast&)
      {
        return outcome::failure(boost::system::error_code{});
      }

      this->cache_[cid] = height;
    }

    return outcome::success();
  }

}