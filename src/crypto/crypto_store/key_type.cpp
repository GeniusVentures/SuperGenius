

#include "crypto/crypto_store/key_type.hpp"

#include <unordered_set>

#include <boost/endian/arithmetic.hpp>
#include <array>
namespace sgns::crypto {

  bool isSupportedKeyType(KeyTypeId k) {
    static const std::unordered_set<KeyTypeId> supported_types = {
        key_types::kProd,
        key_types::kGran,
        key_types::kAcco,
        key_types::kImon,
        key_types::kAudi,
        key_types::kLp2p};

    return supported_types.count(k) > 0;
  }

  std::string decodeKeyTypeId(KeyTypeId param) {
    constexpr unsigned size = sizeof(KeyTypeId);
    constexpr unsigned bits = size * 8u;
    std::array<char, size> key_type{};

    boost::endian::endian_buffer<boost::endian::order::big, KeyTypeId, bits>
        buf(param);
    for (size_t i = 0; i < size; ++i) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      key_type.at(i) = buf.data()[i];
    }

    return std::string(key_type.begin(), key_type.end());
  }
}  // namespace sgns::crypto

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::crypto, KeyTypeError, e) {
  using Error = sgns::crypto::KeyTypeError;
  switch (e) {
    case Error::UNSUPPORTED_KEY_TYPE:
      return "key type is not supported";
    case Error::UNSUPPORTED_KEY_TYPE_ID:
      return "key type id is not supported";
  }

  return "Unknown KeyTypeError";
}
