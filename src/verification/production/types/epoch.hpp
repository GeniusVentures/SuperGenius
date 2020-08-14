

#ifndef SUPERGENIUS_EPOCH_HPP
#define SUPERGENIUS_EPOCH_HPP

#include <chrono>
#include <vector>

#include "verification/production/common.hpp"
#include "primitives/authority.hpp"

namespace sgns::verification {
  /**
   * Metainformation about the Production epoch
   */
  struct Epoch {
    /// the epoch index
    EpochIndex epoch_index{};

    /// starting slot of the epoch; can be non-zero, as the node can join in the
    /// middle of the running epoch
    ProductionSlotNumber start_slot{};

    /// duration of the epoch (number of slots it takes)
    ProductionSlotNumber epoch_duration{};

    /// authorities of the epoch with their weights
    std::vector<primitives::Authority> authorities;

    /// randomness of the epoch
    Randomness randomness{};

    bool operator==(const Epoch &other) const {
      return epoch_index == other.epoch_index && start_slot == other.start_slot
             && epoch_duration == other.epoch_duration
             && authorities == other.authorities
             && randomness == other.randomness;
    }
    bool operator!=(const Epoch &other) const {
      return !(*this == other);
    }
    //added to fix link error
    friend std::ostream &operator<<(std::ostream &out, const Epoch &test_struct)
    {
      out << test_struct.epoch_index << test_struct.start_slot << test_struct.epoch_duration ;
      out << test_struct.authorities.size();
      for(auto it = test_struct.authorities.begin(); it != test_struct.authorities.end() ; it++)
      {
        out << *it;
      }
      out << test_struct.randomness ;
      return out;

    }
    //end
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_EPOCH_HPP
