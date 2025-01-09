#ifndef SUPERGENIUS_PRODUCTION_BLOCK_HEADER_HPP
#define SUPERGENIUS_PRODUCTION_BLOCK_HEADER_HPP

#include "verification/production/common.hpp"
#include "crypto/sr25519_types.hpp"

namespace sgns::verification {
  /**
   * Contains specific data, needed in PRODUCTION for validation
   */
  struct ProductionBlockHeader {
    /// slot, in which the block was produced
    ProductionSlotNumber slot_number{};
    /// output of VRF function
    crypto::VRFOutput vrf_output;
    /// authority index of the producer
    uint64_t authority_index{};
  };

  /**
   * @brief outputs object of type ProductionBlockHeader to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const ProductionBlockHeader &bh) {
    // were added to immitate substrate's enum type for ProductionPreDigest where our
    // ProductionBlockHeader is Primary type and missing weight. For now just set
    // weight to 1
    uint8_t fake_type_index = 0;
    uint32_t fake_weight = 1;
    return s << fake_type_index << bh.authority_index << bh.slot_number
             << fake_weight << bh.vrf_output;
  }

  /**
   * @brief decodes object of type ProductionBlockHeader from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, ProductionBlockHeader &bh) {
    uint8_t fake_type_index = 0;
    uint32_t fake_weight = 0;
    return s >> fake_type_index >> bh.authority_index >> bh.slot_number
           >> fake_weight >> bh.vrf_output;
  }
}

#endif
