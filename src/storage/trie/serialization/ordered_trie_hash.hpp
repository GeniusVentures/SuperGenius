

#ifndef SUPERGENIUS_ORDERED_TRIE_HASH_HPP
#define SUPERGENIUS_ORDERED_TRIE_HASH_HPP

#include "base/buffer.hpp"
#include "scale/scale.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_impl.hpp"
#include "storage/trie/serialization/supergenius_codec.hpp"

namespace sgns::storage::trie {

  /**
   * Calculates the hash of a Merkle tree containing the items from the provided
   * range [begin; end) as values and compact-encoded indices of those
   * values(starting from 0) as keys
   * @tparam It an iterator type of a container of base::Buffers
   * @return the Merkle tree root hash of the tree containing provided values
   */
  template <typename It>
  outcome::result<base::Buffer> calculateOrderedTrieHash(const It &begin,
                                                           const It &end) {
    SuperGeniusTrieImpl trie;
    SuperGeniusCodec codec;
    // empty root
    if (begin == end) {
      static const auto empty_root = base::Buffer{}.put(codec.hash256({0}));
      return empty_root;
    }
    // clang-format off
    static_assert(
        std::is_same_v<std::decay_t<decltype(*begin)>, base::Buffer>);
    // clang-format on
    It it = begin;
    scale::CompactInteger key = 0;
    while (it != end) {
      OUTCOME_TRY((auto &&, enc), scale::encode(key++));
      BOOST_OUTCOME_TRYV2(auto &&, trie.put(base::Buffer{enc}, *it));
      it++;
    }
    OUTCOME_TRY((auto &&, enc), codec.encodeNode(*trie.getRoot()));
    return base::Buffer{codec.hash256(enc)};
  }

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_ORDERED_TRIE_HASH_HPP
