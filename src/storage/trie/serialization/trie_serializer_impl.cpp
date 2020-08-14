

#include "storage/trie/serialization/trie_serializer_impl.hpp"

namespace sgns::storage::trie {

  TrieSerializerImpl::TrieSerializerImpl(
      std::shared_ptr<SuperGeniusTrieFactory> factory,
      std::shared_ptr<Codec> codec,
      std::shared_ptr<TrieStorageBackend> backend)
      : trie_factory_{std::move(factory)},
        codec_{std::move(codec)},
        backend_{std::move(backend)} {
    BOOST_ASSERT(trie_factory_ != nullptr);
    BOOST_ASSERT(codec_ != nullptr);
    BOOST_ASSERT(backend_ != nullptr);
  }

  Buffer TrieSerializerImpl::getEmptyRootHash() const {
    return Buffer(codec_->hash256({0}));
  }

  outcome::result<Buffer> TrieSerializerImpl::storeTrie(SuperGeniusTrie &trie) {
    if (trie.getRoot() == nullptr) {
      return getEmptyRootHash();
    }
    return storeRootNode(*trie.getRoot());
  }

  outcome::result<std::unique_ptr<SuperGeniusTrie>>
  TrieSerializerImpl::retrieveTrie(const base::Buffer &db_key) const {
    SuperGeniusTrieFactory::ChildRetrieveFunctor f =
        [this](const SuperGeniusTrie::BranchPtr& parent, uint8_t idx) {
          return retrieveChild(parent, idx);
        };
    if (db_key == getEmptyRootHash()) {
      return trie_factory_->createEmpty(std::move(f));
    }
    OUTCOME_TRY(root, retrieveNode(db_key));
    return trie_factory_->createFromRoot(std::move(root), std::move(f));
  }

  outcome::result<Buffer> TrieSerializerImpl::storeRootNode(
      SuperGeniusNode &node) {
    auto batch = backend_->batch();
    using T = SuperGeniusNode::Type;

    // if node is a branch node, its children must be stored to the storage
    // before it, as their hashes, which are used as database keys, are a part
    // of its encoded representation required to save it to the storage
    if (node.getTrieType() == T::BranchEmptyValue
        || node.getTrieType() == T::BranchWithValue) {
      auto &branch = dynamic_cast<BranchNode &>(node);
      OUTCOME_TRY(storeChildren(branch, *batch));
    }

    OUTCOME_TRY(enc, codec_->encodeNode(node));
    auto key = Buffer{codec_->hash256(enc)};
    OUTCOME_TRY(batch->put(key, enc));
    OUTCOME_TRY(batch->commit());

    return key;
  }

  outcome::result<base::Buffer> TrieSerializerImpl::storeNode(
      SuperGeniusNode &node, BufferBatch &batch) {
    using T = SuperGeniusNode::Type;

    // if node is a branch node, its children must be stored to the storage
    // before it, as their hashes, which are used as database keys, are a part
    // of its encoded representation required to save it to the storage
    if (node.getTrieType() == T::BranchEmptyValue
        || node.getTrieType() == T::BranchWithValue) {
      auto &branch = dynamic_cast<BranchNode &>(node);
      OUTCOME_TRY(storeChildren(branch, batch));
    }
    OUTCOME_TRY(enc, codec_->encodeNode(node));
    auto key = Buffer{codec_->merkleValue(enc)};
    OUTCOME_TRY(batch.put(key, enc));
    return key;
  }

  outcome::result<void> TrieSerializerImpl::storeChildren(BranchNode &branch,
                                                          BufferBatch &batch) {
    for (auto &child : branch.children) {
      if (child && !child->isDummy()) {
        OUTCOME_TRY(hash, storeNode(*child, batch));
        // when a node is written to the storage, it is replaced with a dummy
        // node to avoid memory waste
        child = std::make_shared<DummyNode>(hash);
      }
    }
    return outcome::success();
  }

  outcome::result<SuperGeniusTrie::NodePtr> TrieSerializerImpl::retrieveChild(
      const SuperGeniusTrie::BranchPtr &parent, uint8_t idx) const {
    if (parent->children.at(idx) == nullptr) {
      return nullptr;
    }
    if (parent->children.at(idx)->isDummy()) {
      auto dummy =
          std::dynamic_pointer_cast<DummyNode>(parent->children.at(idx));
      OUTCOME_TRY(n, retrieveNode(dummy->db_key));
      parent->children.at(idx) = n;
    }
    return parent->children.at(idx);
  }

  outcome::result<SuperGeniusTrie::NodePtr> TrieSerializerImpl::retrieveNode(
      const base::Buffer &db_key) const {
    if (db_key.empty() || db_key == getEmptyRootHash()) {
      return nullptr;
    }
    OUTCOME_TRY(enc, backend_->get(db_key));
    OUTCOME_TRY(n, codec_->decodeNode(enc));
    return std::dynamic_pointer_cast<SuperGeniusNode>(n);
  }

}  // namespace sgns::storage::trie
