

#ifndef SUPERGENIUS_CORE_CRYPTO_MP_UTILS_HPP
#define SUPERGENIUS_CORE_CRYPTO_MP_UTILS_HPP

#include <boost/multiprecision/cpp_int.hpp>
#include <gsl/span>

namespace sgns::base {

  namespace detail {
    template <size_t size, typename uint>
    std::array<uint8_t, size> uint_to_bytes(uint &&i);

    template <size_t size, typename uint>
    uint bytes_to_uint(gsl::span<uint8_t, size> bytes);
  }  // namespace detail

  std::array<uint8_t, 8> uint64_t_to_bytes(uint64_t number);

  uint64_t bytes_to_uint64_t(gsl::span<uint8_t, 8> bytes);

  std::array<uint8_t, 16> uint128_t_to_bytes(
      const boost::multiprecision::uint128_t &i);

  boost::multiprecision::uint128_t bytes_to_uint128_t(
      gsl::span<uint8_t, 16> bytes);

  std::array<uint8_t, 32> uint256_t_to_bytes(
      const boost::multiprecision::uint256_t &i);

  boost::multiprecision::uint256_t bytes_to_uint256_t(
      gsl::span<uint8_t, 32> bytes);

}  // namespace sgns::base

#endif  // SUPERGENIUS_CORE_CRYPTO_MP_UTILS_HPP
