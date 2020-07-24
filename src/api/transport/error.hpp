
#ifndef SUPERGENIUS_SRC_API_TRANSPORT_ERROR_HPP
#define SUPERGENIUS_SRC_API_TRANSPORT_ERROR_HPP

#include "outcome/outcome.hpp"

namespace sgns::api {
  enum class ApiTransportError {
    FAILED_SET_OPTION = 1,   // cannot set an option
    FAILED_START_LISTENING,  // cannot start listening, invalid address or port
                             // is busy
    LISTENER_ALREADY_STARTED,  // cannot start listener, already started
    CANNOT_ACCEPT_LISTENER_NOT_WORKING,  // cannot accept new connection, state
                                         // mismatch
  };
}

OUTCOME_HPP_DECLARE_ERROR_2(sgns::api, ApiTransportError)

#endif  // SUPERGENIUS_SRC_API_TRANSPORT_ERROR_HPP
