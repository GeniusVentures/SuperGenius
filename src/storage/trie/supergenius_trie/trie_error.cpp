

#include "storage/trie/supergenius_trie/trie_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::storage::trie, TrieError, e) {
  using sgns::storage::trie::TrieError;
  switch (e) {
    case TrieError::NO_VALUE:
      return "no stored value found by the given key";
  }
  return "unknown";
}
