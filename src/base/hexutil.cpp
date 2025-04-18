#include "base/hexutil.hpp"

#include <boost/algorithm/hex.hpp>
#include <gsl/span>

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::base, UnhexError, e) {
  using sgns::base::UnhexError;
  switch (e) {
    case UnhexError::NON_HEX_INPUT:
      return "Input contains non-hex characters";
    case UnhexError::NOT_ENOUGH_INPUT:
      return "Input contains odd number of characters";
    case UnhexError::VALUE_OUT_OF_RANGE:
      return "Decoded value is out of range of requested type";
    case UnhexError::MISSING_0X_PREFIX:
      return "Missing expected 0x prefix";
    case UnhexError::UNKNOWN:
      return "Unknown error";
  }
  return "Unknown error (error id not listed)";
}

namespace sgns::base {
  std::string hex_upper(const gsl::span<const uint8_t> bytes) noexcept {
    std::string res(bytes.size() * 2, '\x00');
    boost::algorithm::hex(bytes.begin(), bytes.end(), res.begin());
    return res;
  }

  std::string hex_lower(const gsl::span<const uint8_t> bytes) noexcept {
    std::string res(bytes.size() * 2, '\x00');
    boost::algorithm::hex_lower(bytes.begin(), bytes.end(), res.begin());
    return res;
  }

  outcome::result<std::vector<uint8_t>> unhex(std::string_view hex) {
    std::vector<uint8_t> blob;
    blob.reserve((hex.size() + 1) / 2);

    if ( hex[0] == '0' && hex[1] == 'x' )
    {
        hex = std::string_view( &hex[2], hex.length() - 2 );
    }

    try {
        boost::algorithm::unhex( hex, std::back_inserter( blob ) );
        return blob;
    } catch (const boost::algorithm::not_enough_input &e) {
      return UnhexError::NOT_ENOUGH_INPUT;

    } catch (const boost::algorithm::non_hex_input &e) {
      return UnhexError::NON_HEX_INPUT;

    } catch (const std::exception &e) {
      return UnhexError::UNKNOWN;
    }
  }

  outcome::result<std::vector<uint8_t>> unhexWith0x(
      std::string_view hex_with_prefix) {
    const static std::string leading_chrs = "0x";

    if (hex_with_prefix.substr(0, leading_chrs.size()) != leading_chrs) {
      return UnhexError::MISSING_0X_PREFIX;
    }

    auto without_prefix = hex_with_prefix.substr(leading_chrs.size());

    return base::unhex(without_prefix);
  }
}
