

#ifndef SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP
#define SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP

#include "base/buffer.hpp"

namespace sgns::storage {

  inline const base::Buffer kAuthoritySetKey =
      base::Buffer().put("finality_voters");
  inline const base::Buffer kSetStateKey =
      base::Buffer().put("finality_completed_round");
  ;
  inline const base::Buffer kGenesisBlockHashLookupKey =
      base::Buffer().put(":sgns:genesis_block_hash");
  inline const base::Buffer kLastFinalizedBlockHashLookupKey =
      base::Buffer().put(":sgns:last_finalized_block_hash");

}  // namespace sgns::storage

#endif  // SUPERGENIUS_SRC_STORAGE_PREDEFINED_KEYS_HPP
