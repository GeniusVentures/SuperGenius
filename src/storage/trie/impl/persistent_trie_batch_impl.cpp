

#include "storage/trie/impl/persistent_trie_batch_impl.hpp"

#include "scale/scale.hpp"
#include "storage/trie/supergenius_trie/trie_error.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_cursor.hpp"
#include "storage/trie/impl/topper_trie_batch_impl.hpp"

namespace sgns::storage::trie {

  const base::Buffer EXTRINSIC_INDEX_KEY =
      base::Buffer{}.put(":extrinsic_index");

  // sometimes there is no extrinsic index for a runtime call
  const base::Buffer NO_EXTRINSIC_INDEX_VALUE{
      scale::encode(0xffffffff).value()};

  PersistentTrieBatchImpl::PersistentTrieBatchImpl(
      std::shared_ptr<Codec> codec,
      std::shared_ptr<TrieSerializer> serializer,
      boost::optional<std::shared_ptr<changes_trie::ChangesTracker>> changes,
      std::unique_ptr<SuperGeniusTrie> trie,
      RootChangedEventHandler &&handler)
      : codec_{std::move(codec)},
        serializer_{std::move(serializer)},
        changes_{std::move(changes)},
        trie_{std::move(trie)},
        root_changed_handler_{std::move(handler)} {
    BOOST_ASSERT(codec_ != nullptr);
    BOOST_ASSERT(serializer_ != nullptr);
    BOOST_ASSERT((changes_.has_value() && changes_.value() != nullptr)
                 || !changes_.has_value());
    BOOST_ASSERT(trie_ != nullptr);
    if (changes_) {
      changes_.value()->setExtrinsicIdxGetter(
          [this]() -> outcome::result<Buffer> {
            auto res = trie_->get(EXTRINSIC_INDEX_KEY);
            if (res.has_error() && res.error() == TrieError::NO_VALUE) {
              return NO_EXTRINSIC_INDEX_VALUE;
            }
            return res;
          });
    }
  }

  outcome::result<Buffer> PersistentTrieBatchImpl::commit() {
    OUTCOME_TRY((auto &&, root), serializer_->storeTrie(*trie_));
    root_changed_handler_(root);
    return std::move(root);
  }

  std::unique_ptr<TopperTrieBatch> PersistentTrieBatchImpl::batchOnTop() {
    return std::make_unique<TopperTrieBatchImpl>(shared_from_this());
  }

  outcome::result<Buffer> PersistentTrieBatchImpl::get(
      const Buffer &key) const {
    return trie_->get(key);
  }

  std::unique_ptr<BufferMapCursor> PersistentTrieBatchImpl::cursor() {
    return std::make_unique<SuperGeniusTrieCursor>(*trie_);
  }

  bool PersistentTrieBatchImpl::contains(const Buffer &key) const {
    return trie_->contains(key);
  }

  bool PersistentTrieBatchImpl::empty() const {
    return trie_->empty();
  }

  outcome::result<void> PersistentTrieBatchImpl::clearPrefix(
      const Buffer &prefix) {
    // TODO(Harrm): notify changes tracker
    return trie_->clearPrefix(prefix);
  }

  outcome::result<void> PersistentTrieBatchImpl::put(const Buffer &key,
                                                     const Buffer &value) {
    return put(key, Buffer {value}); // would have to copy anyway
  }

  outcome::result<void> PersistentTrieBatchImpl::put(const Buffer &key,
                                                     Buffer &&value) {
    bool is_new_entry = !trie_->contains(key);
    auto res = trie_->put(key, value);
    if (res && changes_.has_value()) {
      BOOST_OUTCOME_TRYV2(auto &&, changes_.value()->onPut(key, value, is_new_entry));
    }
    return res;
  }

  outcome::result<void> PersistentTrieBatchImpl::remove(const Buffer &key) {
    auto res = trie_->remove(key);
    if (res /*and*/ &&  changes_.has_value()) {
      BOOST_OUTCOME_TRYV2(auto &&, changes_.value()->onRemove(key));
    }
    return res;
  }

}  // namespace sgns::storage::trie
