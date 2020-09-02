

#ifndef SUPERGENIUS_CORE_PRIMITIVES_PRODUCTION_CONFIGURATION_HPP
#define SUPERGENIUS_CORE_PRIMITIVES_PRODUCTION_CONFIGURATION_HPP

#include "base/blob.hpp"
#include "verification/production/common.hpp"
#include "crypto/sr25519_types.hpp"
#include "primitives/authority.hpp"

namespace sgns::primitives {

  using ProductionSlotNumber = uint64_t;
  using ProductionClock = clock::SystemClock;
  using ProductionDuration = ProductionClock::Duration;
  using Randomness = base::Blob<crypto::constants::sr25519::vrf::OUTPUT_SIZE>;

  /// Configuration data used by the PRODUCTION verification engine.
  struct ProductionConfiguration {
    /// The slot duration in milliseconds for PRODUCTION. Currently, only
    /// the value provided by this type at genesis will be used.
    ///
    /// Dynamic slot duration may be supported in the future.
    ProductionDuration slot_duration{};

    ProductionSlotNumber epoch_length{};

    /// A constant value that is used in the threshold calculation formula.
    /// Expressed as a rational where the first member of the tuple is the
    /// numerator and the second is the denominator. The rational should
    /// represent a value between 0 and 1.
    /// In substrate it is called `c`
    /// In the threshold formula calculation, `1 - leadership_rate` represents
    /// the probability of a slot being empty.
    std::pair<uint64_t, uint64_t> leadership_rate;

    /// The authorities for the genesis epoch.
    primitives::AuthorityList genesis_authorities;

    /// The randomness for the genesis epoch.
    Randomness randomness;
  };

  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const ProductionConfiguration &config) {
    size_t slot_duration_u64 =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            config.slot_duration)
            .count();
    return s << slot_duration_u64 << config.epoch_length
             << config.leadership_rate << config.genesis_authorities
             << config.randomness;
  }

  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, ProductionConfiguration &config) {
    size_t slot_duration_u64{};
    s >> slot_duration_u64 >> config.epoch_length >> config.leadership_rate
        >> config.genesis_authorities >> config.randomness;
    config.slot_duration = std::chrono::milliseconds(slot_duration_u64);
    return s;
  }
}  // namespace sgns::primitives

#endif  // SUPERGENIUS_CORE_PRIMITIVES_PRODUCTION_CONFIGURATION_HPP
