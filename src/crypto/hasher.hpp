#ifndef SUPERGENIUS_SRC_HASHER_HASHER_HPP_
#define SUPERGENIUS_SRC_HASHER_HASHER_HPP_

#include "base/blob.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::crypto {
  class Hasher : public IComponent {
   protected:
    using Hash64 = base::Hash64;
    using Hash128 = base::Hash128;
    using Hash256 = base::Hash256;

   public:
       ~Hasher() override = default;

       /**
     * @brief twox_128 calculates 16-byte twox hash
     * @param buffer source buffer
     * @return 128-bit hash value
     */
       //------------ by ruymaster ----//
       [[nodiscard]] virtual Hash64 twox_64( gsl::span<const uint8_t> buffer ) const = 0;

       /**
     * @brief twox_128 calculates 16-byte twox hash
     * @param buffer source buffer
     * @return 128-bit hash value
     */
       //------------ by ruymaster ----//
       [[nodiscard]] virtual Hash128 twox_128( gsl::span<const uint8_t> buffer ) const = 0;

       /**
     * @brief blake2b_128 function calculates 16-byte blake2b hash
     * @param buffer source value
     * @return 128-bit hash value
     */
       [[nodiscard]] virtual Hash128 blake2b_128( gsl::span<const uint8_t> buffer ) const = 0;

       /**
     * @brief twox_256 calculates 32-byte twox hash
     * @param buffer source buffer
     * @return 256-bit hash value
     */
       //---------------------
       [[nodiscard]] virtual Hash256 twox_256( gsl::span<const uint8_t> buffer ) const = 0;

       /**
     * @brief blake2b_256 function calculates 32-byte blake2b hash
     * @param buffer source value
     * @return 256-bit hash value
     */
       [[nodiscard]] virtual Hash256 blake2b_256( gsl::span<const uint8_t> buffer ) const = 0;

       /**
     * @brief keccak_256 function calculates 32-byte keccak hash
     * @param buffer source value
     * @return 256-bit hash value
     */
       [[nodiscard]] virtual Hash256 keccak_256( gsl::span<const uint8_t> buffer ) const = 0;

       /**
     * @brief sha2_256 function calculates 32-byte sha2-256 hash
     * @param buffer source value
     * @return 256-bit hash value
     */
       [[nodiscard]] virtual Hash256 sha2_256( gsl::span<const uint8_t> buffer ) const = 0;
  };
}

#endif
