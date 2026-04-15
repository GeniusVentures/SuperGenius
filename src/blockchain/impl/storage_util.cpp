#include "blockchain/impl/storage_util.hpp"

#include "blockchain/impl/common.hpp"
#include "storage/database_error.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "blockchain/impl/proto/SGBlocks.pb.h"

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
        prependPrefix(NumberToBuffer(num), Prefix::ID_TO_LOOKUP_KEY);
    auto hash_to_idx_key =
        prependPrefix(Buffer{block_hash}, Prefix::ID_TO_LOOKUP_KEY);
    BOOST_OUTCOME_TRYV2(auto &&, db.Put( { "num_to_idx_key" }, block_lookup_key, { "topic" } ) );
    BOOST_OUTCOME_TRYV2(auto &&, db.Put( { "hash_to_idx_key" }, block_lookup_key, { "topic" } ) );
    BOOST_OUTCOME_TRY(db.Put( { "value_lookup_key" }, value, { "topic" } ) );
    return outcome::success();
  }


  outcome::result<base::Buffer> getWithPrefix(
      crdt::GlobalDB &db,
      prefix::Prefix prefix,
      const primitives::BlockId &block_id) {
    BOOST_OUTCOME_TRY( auto key, idToBufferKey(db, block_id));
    return db.Get({"prependPrefix(key, prefix)"});
  }

  base::Buffer NumberToBuffer(primitives::BlockNumber n) {
    SGBlocks::BlockID blockID;

    blockID.set_block_number(n);
    size_t               size = blockID.ByteSizeLong();
    std::vector<uint8_t> serialized_proto( size );

    if (!blockID.SerializeToArray( serialized_proto.data(), static_cast<int>(serialized_proto.size()))) {
        std::cerr << "Failed to serialize blockID into array.\n";
    }

    return base::Buffer{serialized_proto};
  }

  base::Buffer numberAndHashToLookupKey(primitives::BlockNumber number,
                                          const base::Hash256 &hash) {
    auto lookup_key = NumberToBuffer(number);
    lookup_key.put(hash);
    return lookup_key;
  }

  outcome::result<primitives::BlockNumber> BufferToNumber(
      const base::Buffer &key) {
    if (key.size() > sizeof(primitives::BlockNumber)) {
      return outcome::failure(KeyValueRepositoryError::INVALID_KEY);
    }

    SGBlocks::BlockID blockID;

    if ( !blockID.ParseFromArray( key.toVector().data(), static_cast<int>(key.toVector().size()) ) )
    {
        std::cerr << "Failed to parse BlockID from array.\n";
    }

    return blockID.block_number();
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

    return (result.error() == storage::DatabaseError::NOT_FOUND);
  }

}  // namespace sgns::blockchain
