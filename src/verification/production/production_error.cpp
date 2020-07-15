

#include "consensus/production/production_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::consensus, ProductionError, e) {
  using E = sgns::consensus::ProductionError;
  switch (e) {
    case E::TIMER_ERROR:
      return "some internal error happened while using the timer in BABE; "
             "please, see logs";
    case E::NODE_FALL_BEHIND:
      return "local node has fallen too far behind the others, most likely "
             "it is in one of the previous epochs";
  }
  return "unknown error";
}
