

#ifndef SUPERGENIUS_SRC_PRIMITIVES_CHECK_INHERENTS_RESULT_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_CHECK_INHERENTS_RESULT_HPP

#include "primitives/inherent_data.hpp"

namespace sgns::primitives {
  /**
   * @brief result of check_inherents method of BlockBuilder runtime api
   */
  struct CheckInherentsResult {
    /// Did the check succeed?
    bool is_okay = false;
    /// Did we encounter a fatal error?
    bool is_fatal_error = false;
    /// We use the `InherentData` to store our errors.
    primitives::InherentData errors;
  };

  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, CheckInherentsResult &v) {
    return s >> v.is_okay >> v.is_fatal_error >> v.errors;
  }

}  // namespace sgns::primitives

#endif  // SUPERGENIUS_SRC_PRIMITIVES_CHECK_INHERENTS_RESULT_HPP
