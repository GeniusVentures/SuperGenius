

#ifndef SUPERGENIUS_SRC_PRIMITIVES_APPLY_RESULT_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_APPLY_RESULT_HPP

#include <boost/variant.hpp>

namespace sgns::primitives {

  enum class ApplyOutcome : uint8_t { SUCCESS = 0, FAIL = 1 };

  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const ApplyOutcome &outcome) {
    uint8_t r{static_cast<uint8_t>(outcome)};
    return s << r;
  }

  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, ApplyOutcome &outcome) {
    uint8_t int_res = 0;
    s >> int_res;
    outcome = ApplyOutcome{int_res};
    return s;
  }

  enum class ApplyError : uint8_t {
    /// Bad signature.
    BAD_SIGNATURE = 0,
    /// Nonce too low.
    STALE = 1,
    /// Nonce too high.
    FUTURE = 2,
    /// Sending account had too low a balance.
    CANT_PAY = 3,
    /// Block is full, no more extrinsics can be applied.
    FULL_BLOCK = 255,
  };

  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const ApplyError &error) {
    uint8_t r{static_cast<uint8_t>(error)};
    return s << r;
  }

  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, ApplyError &error) {
    uint8_t int_err = 0;
    s >> int_err;
    error = ApplyError{int_err};
    return s;
  }

  using ApplyResult = boost::variant<ApplyOutcome, ApplyError>;

}  // namespace sgns::primitives

#endif  // SUPERGENIUS_SRC_PRIMITIVES_APPLY_RESULT_HPP
