

#include "blockchain/impl/common.hpp"
#include "blockchain/impl/storage_util.hpp"
#include "base/visitor.hpp"
#include "storage/in_memory/in_memory_storage.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_impl.hpp"
#include "storage/trie/serialization/trie_serializer_impl.hpp"
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::blockchain, Error, e) {
  switch (e) {
    case sgns::blockchain::Error::BLOCK_NOT_FOUND:
      return "Block with such ID is not found";
  }
  return "Unknown error";
}

namespace sgns::blockchain {

  outcome::result<base::Buffer> idToBufferKey(crdt::GlobalDB &db,
                                                const primitives::BlockId &id) {
    auto key = visit_in_place(
        id,
        [](const primitives::BlockNumber &n) {
          //auto key = prependPrefix(NumberToBuffer(n),
          //                         prefix::Prefix::ID_TO_LOOKUP_KEY);
          return base::Buffer{}.put(std::to_string( n ));
        },
        [&db](const base::Hash256 &hash) {
          return db.Get({hash.toString()});
        });
    if (!key && isNotFoundError(key.error())) {
      return Error::BLOCK_NOT_FOUND;
    }
    return key;
  }
  outcome::result<std::string> idToStringKey(crdt::GlobalDB &db,
                                                const primitives::BlockId &id) {
    auto key = visit_in_place(
        id,
        [](const primitives::BlockNumber &n) {
          return std::to_string( n );
        },
        [&db](const base::Hash256 &hash) {
          auto key = db.Get({hash.toString()});
          if (key)
          {
            return std::string(key.value().toString());
          }
          else
          {
            return std::string{};
          }
        });
    if (key.empty())
    {
        return outcome::failure(blockchain::Error::BLOCK_NOT_FOUND);
    }
    return key;
  }

  base::Buffer trieRoot(
      const std::vector<std::pair<base::Buffer, base::Buffer>> &key_vals) {
    auto trie = storage::trie::SuperGeniusTrieImpl();
    auto codec = storage::trie::SuperGeniusCodec();

    for (const auto &[key, val] : key_vals) {
      auto res = trie.put(key, val);
      BOOST_ASSERT_MSG(res.has_value(), "Insertion into trie failed");
    }
    auto root = trie.getRoot();
    if (root == nullptr) {
      return base::Buffer{codec.hash256({0})};
    }
    auto encode_res = codec.encodeNode(*root);
    BOOST_ASSERT_MSG(encode_res.has_value(), "Trie encoding failed");
    return base::Buffer{codec.hash256(encode_res.value())};
  }
}  // namespace sgns::blockchain
