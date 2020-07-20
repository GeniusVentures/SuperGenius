

#ifndef SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP
#define SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP

#include "base/buffer.hpp"

namespace sgns::storage {

  inline const base::Buffer kAuthoritySetKey =
      base::Buffer().put("finality_voters");
  inline const base::Buffer kSetStateKey =
      base::Buffer().put("finality_completed_round");
  ;

}  // namespace sgns::storage

#endif  // SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP
