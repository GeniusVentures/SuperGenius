

#ifndef SUPERGENIUS_STORAGE_TRIE_TRIE_ERROR_HPP
#define SUPERGENIUS_STORAGE_TRIE_TRIE_ERROR_HPP

#include "outcome/outcome.hpp"

namespace sgns::storage::trie {
  /**
   * @brief TrieDbError enum provides error codes for TrieDb methods
   */
  enum class TrieError {
    NO_VALUE = 1,  // no stored value found by the given key
  };
}  // namespace sgns::storage::trie

OUTCOME_HPP_DECLARE_ERROR_2(sgns::storage::trie, TrieError)

#endif  // SUPERGENIUS_STORAGE_TRIE_TRIE_ERROR_HPP
