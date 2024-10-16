

#include "storage/changes_trie/impl/changes_trie.hpp"

#include "scale/scale.hpp"

namespace sgns::storage::changes_trie {

  outcome::result<std::unique_ptr<ChangesTrie>> ChangesTrie::buildFromChanges(
      const primitives::BlockNumber &parent_number,
      const std::shared_ptr<storage::trie::SuperGeniusTrieFactory> &trie_factory,
      std::shared_ptr<storage::trie::Codec> codec,
      const ExtrinsicsChanges &extinsics_changes,
      const ChangesTrieConfig &config) {
    BOOST_ASSERT(trie_factory != nullptr);
    auto changes_storage = trie_factory->createEmpty();

    for (auto &change : extinsics_changes) {
      auto &key = change.first;
      auto &changers = change.second;
      auto current_number = parent_number + 1;
      KeyIndexVariant keyIndex{ExtrinsicsChangesKey{{current_number, key}}};
      OUTCOME_TRY((auto &&, key_enc), scale::encode(keyIndex));
      OUTCOME_TRY((auto &&, value), scale::encode(changers));
      base::Buffer value_buf {std::move(value)};
      BOOST_OUTCOME_TRYV2(auto &&, changes_storage->put(base::Buffer{std::move(key_enc)},
                                       std::move(value_buf)));
    }

    return std::unique_ptr<ChangesTrie>{
        new ChangesTrie{std::move(changes_storage), std::move(codec)}};
  }

  ChangesTrie::ChangesTrie(std::unique_ptr<storage::trie::SuperGeniusTrie> trie,
                           std::shared_ptr<storage::trie::Codec> codec)
      : changes_trie_{std::move(trie)},
        codec_{std::move(codec)},
        logger_(base::createLogger("ChangesTrie")) {
    BOOST_ASSERT(changes_trie_ != nullptr);
    BOOST_ASSERT(codec_ != nullptr);
  }

  base::Hash256 ChangesTrie::getHash() const {
    if (changes_trie_ == nullptr) {
      return {};
    }

    auto root = changes_trie_->getRoot();
    if (root == nullptr) {
      logger_->warn("Get root of empty changes trie");
      return codec_->hash256({0});
    }
    auto enc_res = codec_->encodeNode(*root);
    if (enc_res.has_error()) {
      logger_->error("Encoding Changes trie failed"
                     + enc_res.error().message());
      return {};
    }
    auto hash_bytes = codec_->hash256(enc_res.value());
    return hash_bytes;
  }

}  // namespace sgns::storage::changes_trie
