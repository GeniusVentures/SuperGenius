

#ifndef SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_TYPES_VERIFICATION_LOG_HPP
#define SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_TYPES_VERIFICATION_LOG_HPP

#include <boost/variant.hpp>

#include "verification/production/types/next_epoch_descriptor.hpp"
#include "primitives/authority.hpp"

namespace sgns::verification {

  /**
   * A consensus log item for PRODUCTION.
   * The usage of AuthorityIndex option is yet unclear.
   * Name and implementation are taken from substrate.
   */
  using ConsensusLog =
      boost::variant<uint32_t,  // = 0 fake type, should never be used

                     /// The epoch has changed. This provides
                     /// information about the _next_ epoch -
                     /// information about the _current_ epoch (i.e.
                     /// the one we've just entered) should already
                     /// be available earlier in the chain.
                     NextEpochDescriptor,  // = 1

                     /// Disable the authority with given index.
                     primitives::AuthorityIndex>  // = 2 Don't know why this
                                                  // type is in this variant,
                                                  // but in substrate we have it
      ;

}  // namespace sgns::verification

#endif  // SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_TYPES_VERIFICATION_LOG_HPP
