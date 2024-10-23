
#ifndef SUPERGENIUS_ADAPTERS_ERRORS
#define SUPERGENIUS_ADAPTERS_ERRORS

#include "outcome/outcome.hpp"

namespace sgns::network {
  /**
   * @brief interface adapters errors
   */
  enum class AdaptersError : int {
    EMPTY_DATA = 1,
    DATA_SIZE_CORRUPTED,
    PARSE_FAILED,
    UNEXPECTED_VARIANT,
  };

}  // namespace sgns::network

OUTCOME_HPP_DECLARE_ERROR_2(sgns::network, AdaptersError)

#endif  // SUPERGENIUS_ADAPTERS_ERRORS
