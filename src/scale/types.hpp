
#ifndef SUPERGENIUS_SCALE_TYPES_HPP
#define SUPERGENIUS_SCALE_TYPES_HPP

#include <vector>

#include <boost/multiprecision/cpp_int.hpp>
#include <outcome/outcome.hpp>

namespace sgns::scale {
  /**
   * @brief convenience alias for arrays of bytes
   */
  using ByteArray = std::vector<uint8_t>;
  /**
   * @brief represents compact integer value
   */
  using CompactInteger = boost::multiprecision::cpp_int;

  /// @brief OptionalBool is internal extended bool type
  enum class OptionalBool : uint8_t { NONE_ = 0u, FALSE_ = 1u, TRUE_ = 2u };
}  // namespace sgns::scale

namespace sgns::scale::compact {
  /**
   * @brief categories of compact encoding
   */
  struct EncodingCategoryLimits {
    // min integer encoded by 2 bytes
    constexpr static size_t kMinUint16 = (1ul << 6u);
    // min integer encoded by 4 bytes
    constexpr static size_t kMinUint32 = (1ul << 14u);
    // min integer encoded as multibyte
    constexpr static size_t kMinBigInteger = (1ul << 30u);
  };
}  // namespace sgns::scale::compact
#endif  // SUPERGENIUS_SCALE_TYPES_HPP
