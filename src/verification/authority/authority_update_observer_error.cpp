#include "verification/authority/authority_update_observer_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::authority,
                            AuthorityUpdateObserverError,
                            e) {
  using E = sgns::authority::AuthorityUpdateObserverError;
  switch (e) {
    case E::UNSUPPORTED_MESSAGE_TYPE:
      return "unsupported message type";
    case E::WRONG_AUTHORITY_INDEX:
      return "wrong authority index (out of bound)";
    case E::NO_SCHEDULED_CHANGE_APPLIED_YET:
      return "no previous change (sheduled) applied yet";
    case E::NO_FORCED_CHANGE_APPLIED_YET:
      return "no previous change (forced) applied yet";
    case E::NO_PAUSE_APPLIED_YET:
      return "no previous change (pause) applied yet";
    case E::NO_RESUME_APPLIED_YET:
      return "no previous change (resume) applied yet";
  }
  return "unknown error (invalid AuthorityUpdateObserverError)";
}
