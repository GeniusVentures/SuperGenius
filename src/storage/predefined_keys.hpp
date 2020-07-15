

#ifndef SUPERGENIUS_CORE_STORAGE_PREDEFINED_KEYS_HPP
#define SUPERGENIUS_CORE_STORAGE_PREDEFINED_KEYS_HPP

#include "base/buffer.hpp"

namespace sgns::storage {

  inline const base::Buffer kAuthoritySetKey =
      base::Buffer().put("grandpa_voters");
  inline const base::Buffer kSetStateKey =
      base::Buffer().put("grandpa_completed_round");
  ;

}  // namespace sgns::storage

#endif  // SUPERGENIUS_CORE_STORAGE_PREDEFINED_KEYS_HPP
