

#ifndef SUPERGENIUS_SRC_STORAGE_TRIE_IMPL_TOPPER_TRIE_BATCH_IMPL
#define SUPERGENIUS_SRC_STORAGE_TRIE_IMPL_TOPPER_TRIE_BATCH_IMPL

#include <map>
#include <list>

#include <boost/optional.hpp>

#include "storage/trie/trie_batches.hpp"

namespace sgns::storage::trie {

  class TopperTrieBatchImpl : public TopperTrieBatch {
   public:
    enum class Error { PARENT_EXPIRED = 1 };

    explicit TopperTrieBatchImpl(const std::shared_ptr<TrieBatch> &parent);

    outcome::result<Buffer> get(const Buffer &key) const override;

    /**
     * Won't consider changes not written back to the parent batch
     */
    std::unique_ptr<BufferMapCursor> cursor() override;
    bool contains(const Buffer &key) const override;
    bool empty() const override;

    outcome::result<void> put(const Buffer &key, const Buffer &value) override;
    outcome::result<void> put(const Buffer &key, Buffer &&value) override;
    outcome::result<void> remove(const Buffer &key) override;
    outcome::result<void> clearPrefix(const Buffer &prefix) override;

    outcome::result<void> writeBack() override;

   private:
    bool wasClearedByPrefix(const Buffer& key) const;

    std::map<Buffer, boost::optional<Buffer>> cache_;
    std::list<Buffer> cleared_prefixes_;
    std::weak_ptr<TrieBatch> parent_;
  };

}  // namespace sgns::storage::trie

OUTCOME_HPP_DECLARE_ERROR_2(sgns::storage::trie, TopperTrieBatchImpl::Error);

#endif  // SUPERGENIUS_SRC_STORAGE_TRIE_IMPL_TOPPER_TRIE_BATCH_IMPL
