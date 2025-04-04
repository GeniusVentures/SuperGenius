#ifndef SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_PERSISTENT_MAP_UTIL_HPP
#define SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_PERSISTENT_MAP_UTIL_HPP

#include "base/buffer.hpp"
#include "primitives/block_id.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"

/**
 * Auxiliary functions to simplify usage of persistant map based storage
 * as a Blockchain storage
 */

namespace sgns::blockchain {

  /**
   * Storage has only one key space, prefixes are used to divide it
   */
  namespace prefix {
    enum Prefix : uint8_t {
      // mapping of block id to a storage lookup key
      ID_TO_LOOKUP_KEY = 3,

      // block headers
      HEADER = 4,

      // body of the block (extrinsics)
      BLOCK_DATA = 5,

      // justification of the finalized block
      JUSTIFICATION = 6,

      // node of a trie db
      TRIE_NODE = 7
    };
  }

  /**
   * Errors that might occur during work with storage
   */
  enum class KeyValueRepositoryError { INVALID_KEY = 1 };

  /**
   * Concatenate \param key_column with \param key
   * @return key_column|key
   */
  base::Buffer prependPrefix(const base::Buffer &key,
                               prefix::Prefix key_column);

  /**
   * Put an entry to key space \param prefix and corresponding lookup keys to
   * ID_TO_LOOKUP_KEY space
   * @param map to put the entry to
   * @param prefix keyspace for the entry value
   * @param num block number that could be used to retrieve the value
   * @param block_hash block hash that could be used to retrieve the value
   * @param value data to be put to the storage
   * @return storage error if any
   */
  outcome::result<void> putWithPrefix(crdt::GlobalDB &db,
                                      prefix::Prefix prefix,
                                      primitives::BlockNumber num,
                                      base::Hash256 block_hash,
                                      const base::Buffer &value);

  /**
   * Get an entry from the database
   * @param map to get the entry from
   * @param prefix, with which the entry was put into
   * @param block_id - id of the block to get entry for
   * @return encoded entry or error
   */
  outcome::result<base::Buffer> getWithPrefix(
      crdt::GlobalDB &db,
      prefix::Prefix prefix,
      const primitives::BlockId &block_id);

  /**
   * Convert block number into short lookup key (LE representation) for
   * blocks that are in the canonical chain.
   *
   * In the current database schema, this kind of key is only used for
   * lookups into an index, NOT for storing header data or others.
   */
  base::Buffer NumberToBuffer(primitives::BlockNumber n);

  /**
   * Convert number and hash into long lookup key for blocks that are
   * not in the canonical chain.
   */
  base::Buffer numberAndHashToLookupKey(primitives::BlockNumber number,
                                          const base::Hash256 &hash);

  /**
   * Converts buffer data to a block number
   */
  outcome::result<primitives::BlockNumber> BufferToNumber(
      const base::Buffer &key);

  /**
   * For a persistant map based storage checks
   * whether result should be considered as `NOT FOUND` error
   */
  bool isNotFoundError(outcome::result<void> result);

}  // namespace sgns::blockchain

OUTCOME_HPP_DECLARE_ERROR_2(sgns::blockchain, KeyValueRepositoryError);

#endif  // SUPERGENIUS_CORE_BLOCKCHAIN_IMPL_PERSISTENT_MAP_UTIL_HPP
