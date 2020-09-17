
#include "verification/finality/impl/voting_round_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::verification::finality, VotingRoundError, e) {
  using E = sgns::verification::finality::VotingRoundError;
  switch (e) {
    case E::FIN_VALIDATION_FAILED:
      return "Validation of finalization message failed";
    case E::LAST_ESTIMATE_BETTER_THAN_PREVOTE:
      return "Current state does not contain prevote which is equal to the "
             "last round estimate or is descendant of it";
    case E::NEW_STATE_EQUAL_TO_OLD:
      return "New state is equal to the new one";
  }
}
