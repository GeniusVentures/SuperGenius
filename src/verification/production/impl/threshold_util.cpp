

#include "verification/production/impl/threshold_util.hpp"

#include <boost/range/adaptors.hpp>
#include <boost/range/numeric.hpp>

namespace sgns::verification {

  Threshold calculateThreshold(
      const std::pair<uint64_t, uint64_t> &c_pair,
      const primitives::AuthorityList &authorities,
      primitives::AuthorityIndex authority_index) {
      double c = static_cast<double>( c_pair.first ) / static_cast<double>( c_pair.second );

      using boost::adaptors::transformed;
      double theta =
          static_cast<double>( authorities[authority_index].weight ) /
          boost::accumulate( authorities | transformed( []( auto &authority ) { return authority.weight; } ), 0. );

      using namespace boost::multiprecision; // NOLINT
      cpp_rational      p_rat( 1. - pow( 1. - c, theta ) );
      static const auto a = ( uint256_t{ 1 } << 128 );
      cpp_int           t = a * numerator( p_rat ) / denominator( p_rat );
      return Threshold{ t };
  }

}  // namespace sgns::verification
