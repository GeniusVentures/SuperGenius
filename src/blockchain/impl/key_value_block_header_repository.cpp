

#include "blockchain/impl/key_value_block_header_repository.hpp"

#include <string_view>

#include <boost/optional.hpp>

#include "blockchain/impl/storage_util.hpp"
#include "base/hexutil.hpp"
#include "scale/scale.hpp"

using sgns::blockchain::prefix::Prefix;
using sgns::base::Hash256;
using sgns::primitives::BlockId;
using sgns::primitives::BlockNumber;

namespace sgns::blockchain {

  KeyValueBlockHeaderRepository::KeyValueBlockHeaderRepository(
      std::shared_ptr<crdt::GlobalDB> db,
      std::shared_ptr<crypto::Hasher> hasher,
      std::string &net_id)
      : db_{std::move(db)}, hasher_{std::move(hasher)} {
    BOOST_ASSERT(hasher_);
    block_header_key_prefix = net_id + std::string(BLOCKCHAIN_PATH);
  }

  outcome::result<BlockNumber> KeyValueBlockHeaderRepository::getNumberByHash(
      const Hash256 &hash) const {
    OUTCOME_TRY((auto &&, key), idToBufferKey(*db_, hash));

    auto maybe_number = BufferToNumber(key);

    return maybe_number;
  }

  outcome::result<base::Hash256>
  KeyValueBlockHeaderRepository::getHashByNumber(
      const primitives::BlockNumber &number) const {
    OUTCOME_TRY((auto &&, header), getBlockHeader(number));
    OUTCOME_TRY((auto &&, enc_header), scale::encode(header));
    return hasher_->blake2b_256(enc_header);
  }

  outcome::result<primitives::BlockHeader>
  KeyValueBlockHeaderRepository::getBlockHeader(const BlockId &id) const {
    OUTCOME_TRY((auto &&, header_string_val), idToStringKey(*db_,id));

    auto header_res = db_->Get({block_header_key_prefix + header_string_val});
    if (!header_res) {
      return (isNotFoundError(header_res.error())) ? Error::BLOCK_NOT_FOUND
                                                   : header_res.error();
    }

    return scale::decode<primitives::BlockHeader>(header_res.value());
  }
  
  outcome::result<primitives::BlockHash> KeyValueBlockHeaderRepository::putBlockHeader(
      const primitives::BlockHeader &header) {
    OUTCOME_TRY((auto &&, encoded_header), scale::encode(header));
    auto header_hash = hasher_->blake2b_256(encoded_header);
   // BOOST_OUTCOME_TRYV2(auto &&, putWithPrefix(*db_,
   //                           Prefix::HEADER,
   //                           header.number,
   //                           header_hash,
   //                           Buffer{std::move(encoded_header)}));

    //Store block humber with hash as its key
    //OUTCOME_TRY((auto &&, hash_key_string), idToStringKey(*db_, header_hash));
    OUTCOME_TRY((auto &&, id_string), idToStringKey(*db_, header.number));
    BOOST_OUTCOME_TRYV2( auto &&, db_->Put({header_hash.toReadableString() }, NumberToBuffer(header.number)));
    BOOST_OUTCOME_TRYV2(auto &&, db_->Put({block_header_key_prefix + id_string}, base::Buffer{std::move(encoded_header)}));

    return header_hash;
  }

  outcome::result<void>
  KeyValueBlockHeaderRepository::removeBlockHeader(const BlockId &id) {
    OUTCOME_TRY((auto &&, header_string_val), idToStringKey(*db_,id));

    return db_->Remove({block_header_key_prefix + header_string_val});
  }

  outcome::result<BlockStatus> KeyValueBlockHeaderRepository::getBlockStatus(
      const primitives::BlockId &id) const {
    return getBlockHeader(id).has_value() ? BlockStatus::InChain
                                          : BlockStatus::Unknown;
  }

  std::string KeyValueBlockHeaderRepository::GetHeaderPath() const
  {
    return block_header_key_prefix;
  }

}  // namespace sgns::blockchain
