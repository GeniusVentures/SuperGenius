#include "blockchain/impl/storage_util.hpp"

#include "blockchain/impl/common.hpp"
#include "storage/database_error.hpp"
#include <crdt/globaldb/globaldb.hpp>
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
    BOOST_OUTCOME_TRYV2(auto &&, db.Put({"num_to_idx_key"}, block_lookup_key));
    BOOST_OUTCOME_TRYV2(auto &&, db.Put({"hash_to_idx_key"}, block_lookup_key));
    return db.Put({"value_lookup_key"}, value);
  }


  outcome::result<base::Buffer> getWithPrefix(
      crdt::GlobalDB &db,
      prefix::Prefix prefix,
      const primitives::BlockId &block_id) {
    OUTCOME_TRY((auto &&, key), idToBufferKey(db, block_id));
    return db.Get({"prependPrefix(key, prefix)"});
  }

  base::Buffer NumberToBuffer(primitives::BlockNumber n) {
    // TODO(Harrm) Figure out why exactly it is this way in substrate
    //BOOST_ASSERT((n & 0xffffffff00000000) == 0);
    SGBlocks::BlockID blockID;

    blockID.set_block_number(n);
    size_t               size = blockID.ByteSizeLong();
    std::vector<uint8_t> serialized_proto( size );

    blockID.SerializeToArray( serialized_proto.data(), static_cast<int>(serialized_proto.size()) );
    return base::Buffer{serialized_proto};
    /**base::Buffer retval;

    //Little endian
    for ( std::size_t i = 0; i < sizeof(primitives::BlockNumber); ++i )
    {
        retval.putUint8(static_cast<uint8_t>((n >> (i * 8)) & 0xffu))  ;
    }
    return retval;*/

    //return {uint8_t(n >> 24u),
    //        uint8_t((n >> 16u) & 0xffu),
    //        uint8_t((n >> 8u) & 0xffu),
    //        uint8_t(n & 0xffu)};
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
        std::cerr << "Failed to parse BlockID from array." << std::endl;
    }

    return blockID.block_number();
    /*primitives::BlockNumber retval = 0;

    //Little endian
    for ( std::size_t i = 0; i < key.size(); ++i )
    {
        retval += (key[i] << (i * 8));
    }
    return retval;*/
    //return (uint64_t(key[0]) << 24u) | (uint64_t(key[1]) << 16u)
    //       | (uint64_t(key[2]) << 8u) | uint64_t(key[3]);
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
