#ifndef SUPERGENIUS_BLOB_HPP
#define SUPERGENIUS_BLOB_HPP

#include <cstddef>
#include <array>

#include <boost/functional/hash.hpp>
#include "base/hexutil.hpp"
#include <iostream>

namespace sgns::base {

  /**
   * Error codes for exceptions that may occur during blob initialization
   */
  enum class BlobError { INCORRECT_LENGTH = 1 };

  using byte_t = uint8_t;

  /**
   * Base type which represents blob of fixed size.
   *
   * std::string is convenient to use but it is not safe.
   * We can not specify the fixed length for string.
   *
   * For std::array it is possible, so we prefer it over std::string.
   */
  template <size_t size_>
  class Blob : public std::array<byte_t, size_> {
   public:
    /**
     * Initialize blob value
     */
    Blob() {
      this->fill(0);
    }

    /**
     * @brief constructor enabling initializer list
     * @param l initializer list
     */
    explicit constexpr Blob( const std::array<byte_t, size_> &l )
    {
        std::copy( l.begin(), l.end(), this->begin() );
    }

    /**
     * In compile-time returns size of current blob.
     */
    constexpr static size_t size() {
      return size_;
    }

    /**
     * Converts current blob to std::string
     */
    [[nodiscard]] std::string toString() const noexcept
    {
        return std::string{ this->begin(), this->end() };
    }

    /**
     * Converts current blob to a readable std::string
     */
    [[nodiscard]] std::string toReadableString() const noexcept
    {
        std::string out_str;
        char        temp_buf[3];

        for ( auto it = this->begin(); it != this->end(); ++it )
        {
            snprintf( temp_buf, sizeof( temp_buf ), "%02x", *it );
            out_str.append( temp_buf, sizeof( temp_buf ) - 1 );
        }

        return out_str;
    }

    /**
     * Converts current blob to hex string.
     */
    [[nodiscard]] std::string toHex() const noexcept
    {
        // return hex_lower({this->begin(), this->end()});
        return hex_lower( gsl::make_span( *this ) );
    }

    /**
     * Create Blob from arbitrary string, putting its bytes into the blob
     * @param data arbitrary string containing
     * @return result containing Blob object if string has proper size
     */
    static outcome::result<Blob<size_>> fromString(std::string_view data) {
      if (data.size() != size_) {
        return BlobError::INCORRECT_LENGTH;
      }

      Blob<size_> b;
      std::copy(data.begin(), data.end(), b.begin());

      return b;
    }


    static outcome::result<Blob<size_>> fromReadableString(std::string_view data) {
      if (data.size()/2 != size_) {
        return BlobError::INCORRECT_LENGTH;
      }
      Blob<size_> b;

      for ( std::size_t i = 0; i < data.size(); i+=2 )
      {
          std::string byteString = std::string(data).substr(i, 2);
          char byte = static_cast<char>(std::stoul(byteString, nullptr, 16));
          b[i/2] = static_cast<uint8_t>(byte);
      }


      return b;
    }

    /**
     * Create Blob from hex string
     * @param hex hex string
     * @return result containing Blob object if hex string has proper size and
     * is in hex format
     */
    static outcome::result<Blob<size_>> fromHex(std::string_view hex) {
      OUTCOME_TRY((auto &&, res), unhex(hex));
      return fromSpan(res);
    }

    /**
     * Create Blob from hex string prefixed with 0x
     * @param hex hex string
     * @return result containing Blob object if hex string has proper size and
     * is in hex format
     */
    static outcome::result<Blob<size_>> fromHexWithPrefix(
        std::string_view hex) {
      OUTCOME_TRY((auto &&, res), unhexWith0x(hex));
      return fromSpan(res);
    }

    /**
     * Create Blob from span of uint8_t
     * @param buffer
     * @return
     */
    static outcome::result<Blob<size_>> fromSpan(
        const gsl::span<uint8_t> &span) {
      if (span.size() != size_) {
        return BlobError::INCORRECT_LENGTH;
      }

      Blob<size_> blob;
      std::copy(span.begin(), span.end(), blob.begin());
      return blob;
    }
  };

  // extern specification of the most frequently instantiated blob
  // specializations, used mostly for Hash instantiation
  extern template class Blob<8ul>;
  extern template class Blob<16ul>;
  extern template class Blob<32ul>;
  extern template class Blob<64ul>;

  // Hash specializations
  using Hash64 = Blob<8>;
  using Hash128 = Blob<16>;
  using Hash256 = Blob<32>;
  using Hash512 = Blob<64>;

  /**
   * @brief scale-encodes blob instance to stream
   * @tparam Stream output stream type
   * @tparam size blob size
   * @param s output stream reference
   * @param blob value to encode
   * @return reference to stream
   */
  template <class Stream,
            size_t size,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const Blob<size> &blob) {
    for (auto &&it = blob.begin(), end = blob.end(); it != end; ++it) {
      s << *it;
    }
    return s;
  }

  /**
   * @brief decodes blob instance from stream
   * @tparam Stream output stream type
   * @tparam size blob size
   * @param s input stream reference
   * @param blob value to encode
   * @return reference to stream
   */
  template <class Stream,
            size_t size,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, Blob<size> &blob) {
    for (auto &&it = blob.begin(), end = blob.end(); it != end; ++it) {
      s >> *it;
    }
    return s;
  }

  template <size_t N>
  inline std::ostream &operator<<(std::ostream &os, const Blob<N> &blob) {
    return os << blob.toHex();
  }

}  // namespace sgns::base

template <size_t N>
struct std::hash<sgns::base::Blob<N>> {
  auto operator()(const sgns::base::Blob<N> &blob) const {
    return boost::hash_range(blob.data(), blob.data() + N);  // NOLINT
  }
};

OUTCOME_HPP_DECLARE_ERROR_2(sgns::base, BlobError);

#endif  // SUPERGENIUS_BLOB_HPP
