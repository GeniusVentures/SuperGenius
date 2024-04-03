

#include "blockchain/impl/storage_util.hpp"

#include "blockchain/impl/common.hpp"
#include "storage/database_error.hpp"
#include <crdt/globaldb/globaldb.hpp>

using sgns::blockchain::prefix::Prefix;
using sgns::base::Buffer;
using sgns::base::Hash256;
using sgns::primitives::BlockId;
using sgns::primitives::BlockNumber;

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::blockchain, KeyValueRepositoryError, e) {
  using E = sgns::blockchain::KeyValueRepositoryError;
  switch (e) {
    case E::INVALID_KEY:
      return "Invalid storage key";
  }
  return "Unknown error";
}

namespace sgns::blockchain {

  outcome::result<void> putWithPrefix(crdt::GlobalDB &db,
                                      prefix::Prefix prefix,
                                      BlockNumber num,
                                      Hash256 block_hash,
                                      const base::Buffer &value) {
    auto block_lookup_key = numberAndHashToLookupKey(num, block_hash);
    auto value_lookup_key = prependPrefix(block_lookup_key, prefix);
    auto num_to_idx_key =
        prependPrefix(numberToIndexKey(num), Prefix::ID_TO_LOOKUP_KEY);
    auto hash_to_idx_key =
        prependPrefix(Buffer{block_hash}, Prefix::ID_TO_LOOKUP_KEY);
    BOOST_OUTCOME_TRYV2(auto &&, db.Put({"num_to_idx_key"}, block_lookup_key));
    BOOST_OUTCOME_TRYV2(auto &&, db.Put({"hash_to_idx_key"}, block_lookup_key));
    return db.Put({"value_lookup_key"}, value);
  }

  outcome::result<base::Buffer> getWithPrefix(
      crdt::GlobalDB &db,
      prefix::Prefix prefix,
      const primitives::BlockId &block_id) {
    OUTCOME_TRY((auto &&, key), idToLookupKey(db, block_id));
    return db.Get({"prependPrefix(key, prefix)"});
  }

  base::Buffer numberToIndexKey(primitives::BlockNumber n) {
    // TODO(Harrm) Figure out why exactly it is this way in substrate
    BOOST_ASSERT((n & 0xffffffff00000000) == 0);

    return {uint8_t(n >> 24u),
            uint8_t((n >> 16u) & 0xffu),
            uint8_t((n >> 8u) & 0xffu),
            uint8_t(n & 0xffu)};
  }

  base::Buffer numberAndHashToLookupKey(primitives::BlockNumber number,
                                          const base::Hash256 &hash) {
    auto lookup_key = numberToIndexKey(number);
    lookup_key.put(hash);
    return lookup_key;
  }

  outcome::result<primitives::BlockNumber> lookupKeyToNumber(
      const base::Buffer &key) {
    if (key.size() < 4) {
      return outcome::failure(KeyValueRepositoryError::INVALID_KEY);
    }
    return (uint64_t(key[0]) << 24u) | (uint64_t(key[1]) << 16u)
           | (uint64_t(key[2]) << 8u) | uint64_t(key[3]);
  }

  base::Buffer prependPrefix(const base::Buffer &key,
                               prefix::Prefix key_column) {
    return base::Buffer{}
        .reserve(key.size() + 1)
        .putUint8(key_column)
        .put(key);
  }

  bool isNotFoundError(outcome::result<void> result) {
    if (result) {
      return false;
    }

    auto &&error = result.error();
    return (error == storage::DatabaseError::NOT_FOUND);
  }

}  // namespace sgns::blockchain
