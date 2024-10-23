#ifndef SUPERGENIUS_BLOCK_TREE_ERROR_HPP
#define SUPERGENIUS_BLOCK_TREE_ERROR_HPP

#include "outcome/outcome.hpp"

namespace sgns::blockchain {
  /**
   * Errors of the block tree are here, so that other modules can use them, for
   * example, to compare a received error with those
   */
  enum class BlockTreeError {
    INVALID_DB = 1,
    NO_PARENT,
    BLOCK_EXISTS,
    HASH_FAILED,
    NO_SUCH_BLOCK,
    INCORRECT_ARGS,
    INTERNAL_ERROR
  };
}  // namespace sgns::blockchain

OUTCOME_HPP_DECLARE_ERROR_2(sgns::blockchain, BlockTreeError)

#endif  // SUPERGENIUS_BLOCK_TREE_ERROR_HPP
