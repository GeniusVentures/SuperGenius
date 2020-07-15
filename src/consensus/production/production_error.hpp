

#ifndef SUPERGENIUS_CONSENSUS_PRODUCTION_ERROR_HPP
#define SUPERGENIUS_CONSENSUS_PRODUCTION_ERROR_HPP

#include <outcome/outcome.hpp>

namespace sgns::consensus {
  enum class ProductionError { TIMER_ERROR = 1, NODE_FALL_BEHIND };
}

OUTCOME_HPP_DECLARE_ERROR_2(sgns::consensus, ProductionError)

#endif  // SUPERGENIUS_CONSENSUS_PRODUCTION_ERROR_HPP
