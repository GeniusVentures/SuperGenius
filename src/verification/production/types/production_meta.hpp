

#ifndef SUPERGENIUS_PRODUCTION_META_HPP
#define SUPERGENIUS_PRODUCTION_META_HPP

#include "clock/clock.hpp"
#include "verification/production/production_lottery.hpp"
#include "verification/production/common.hpp"
#include "verification/production/types/epoch.hpp"

namespace sgns::verification {
  /**
   * Information about running PRODUCTION production
   */
  struct ProductionMeta {
    const Epoch &current_epoch_;
    ProductionSlotNumber current_slot_{};
    const ProductionLottery::SlotsLeadership &slots_leadership_;
    const ProductionTimePoint &last_slot_finish_time_;
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_PRODUCTION_META_HPP
