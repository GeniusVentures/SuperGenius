

#ifndef SUPERGENIUS_SCALE_ERROR_HPP
#define SUPERGENIUS_SCALE_ERROR_HPP

#include <outcome/outcome.hpp>
#include "scale/types.hpp"

namespace sgns::scale {
  /**
   * @brief EncodeError enum provides error codes for Encode methods
   */
  enum class EncodeError {        // 0 is reserved for success
    COMPACT_INTEGER_TOO_BIG = 1,  ///< compact integer can't be more than 2**536
    NEGATIVE_COMPACT_INTEGER,     ///< cannot compact-encode negative integers
    WRONG_CATEGORY,               ///< wrong compact category
    WRONG_ALTERNATIVE,            ///< wrong cast to alternative
  };

  /**
   * @brief DecoderError enum provides codes of errors for Decoder methods
   */
  enum class DecodeError {  // 0 is reserved for success
    NOT_ENOUGH_DATA = 1,    ///< not enough data to decode value
    UNEXPECTED_VALUE,       ///< unexpected value
    TOO_MANY_ITEMS,         ///< too many items, cannot address them in memory
    WRONG_TYPE_INDEX,       ///< wrong type index, cannot decode variant
    INVALID_DATA,           ///< invalid data
    OUT_OF_BOUNDARIES       ///< advance went out of boundaries
  };

  /**
   * @brief base errors
   */
  enum class CommonError {
    UNKNOWN_ERROR = 1  ///< unknown error
  };
}  // namespace sgns::scale

OUTCOME_HPP_DECLARE_ERROR_2(sgns::scale, EncodeError)
OUTCOME_HPP_DECLARE_ERROR_2(sgns::scale, DecodeError)
OUTCOME_HPP_DECLARE_ERROR_2(sgns::scale, CommonError)

#endif  // SUPERGENIUS_SCALE_ERROR_HPP
