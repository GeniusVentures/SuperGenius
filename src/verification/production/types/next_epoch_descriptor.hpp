

#ifndef SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_TYPES_NEXT_EPOCH_DESCRIPTOR_HPP
#define SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_TYPES_NEXT_EPOCH_DESCRIPTOR_HPP

#include "primitives/authority.hpp"

namespace sgns::verification {

  /// Information about the epoch after next epoch
  struct NextEpochDescriptor {
    /// The authorities.
    std::vector<primitives::Authority> authorities;

    /// The value of randomness to use for the slot-assignment.
    Randomness randomness;

    bool operator==(const NextEpochDescriptor &rhs) const {
      return authorities == rhs.authorities and randomness == rhs.randomness;
    }
    bool operator!=(const NextEpochDescriptor &rhs) const {
      return not operator==(rhs);
    }
  };

  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const NextEpochDescriptor &ned) {
    return s << ned.authorities << ned.randomness;
  }

  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, NextEpochDescriptor &ned) {
    return s >> ned.authorities >> ned.randomness;
  }

}  // namespace sgns::verification

#endif  // SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_TYPES_NEXT_EPOCH_DESCRIPTOR_HPP
