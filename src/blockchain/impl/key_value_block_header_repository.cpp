

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
      std::shared_ptr<storage::BufferStorage> map,
      std::shared_ptr<crypto::Hasher> hasher)
      : map_{std::move(map)}, hasher_{std::move(hasher)} {
    BOOST_ASSERT(hasher_);
  }

  outcome::result<BlockNumber> KeyValueBlockHeaderRepository::getNumberByHash(
      const Hash256 &hash) const {
    OUTCOME_TRY(key, idToLookupKey(*map_, hash));

    auto maybe_number = lookupKeyToNumber(key);

    return maybe_number;
  }

  outcome::result<base::Hash256>
  KeyValueBlockHeaderRepository::getHashByNumber(
      const primitives::BlockNumber &number) const {
    OUTCOME_TRY(header, getBlockHeader(number));
    OUTCOME_TRY(enc_header, scale::encode(header));
    return hasher_->blake2b_256(enc_header);
  }

  outcome::result<primitives::BlockHeader>
  KeyValueBlockHeaderRepository::getBlockHeader(const BlockId &id) const {
    auto header_res = getWithPrefix(*map_, Prefix::HEADER, id);
    if (!header_res) {
      return (isNotFoundError(header_res.error())) ? Error::BLOCK_NOT_FOUND
                                                   : header_res.error();
    }

    return scale::decode<primitives::BlockHeader>(header_res.value());
  }

  outcome::result<BlockStatus> KeyValueBlockHeaderRepository::getBlockStatus(
      const primitives::BlockId &id) const {
    return getBlockHeader(id).has_value() ? BlockStatus::InChain
                                          : BlockStatus::Unknown;
  }

}  // namespace sgns::blockchain
