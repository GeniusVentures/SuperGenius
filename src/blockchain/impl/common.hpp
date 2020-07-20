

#ifndef SUPERGENIUS_BLOCKCHAIN_COMMON_HPP
#define SUPERGENIUS_BLOCKCHAIN_COMMON_HPP

#include <outcome/outcome.hpp>

#include "base/buffer.hpp"
#include "primitives/block_id.hpp"
#include "storage/buffer_map_types.hpp"

namespace sgns::blockchain {
  using ReadableBufferMap =
      storage::face::Readable<base::Buffer, base::Buffer>;

  enum class Error {
    // it's important to convert storage errors of this type to this one to
    // enable a user to discern between cases when a header with provided id
    // is not found and when an internal storage error occurs
    BLOCK_NOT_FOUND = 1
  };

  /**
   * Convert a block ID into a key, which is a first part of a key, by which the
   * columns are stored in the database
   */
  outcome::result<base::Buffer> idToLookupKey(const ReadableBufferMap &map,
                                                const primitives::BlockId &id);

  /**
   * Instantiate empty merkle trie, insert \param key_vals pairs and \return
   * Buffer containing merkle root of resulting trie
   */
  base::Buffer trieRoot(
      const std::vector<std::pair<base::Buffer, base::Buffer>> &key_vals);
}  // namespace sgns::blockchain

OUTCOME_HPP_DECLARE_ERROR_2(sgns::blockchain, Error)

#endif  // SUPERGENIUS_BLOCKCHAIN_COMMON_HPP
