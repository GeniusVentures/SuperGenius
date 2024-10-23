

#ifndef SUPERGENIUS_AUTHORITY_HPP
#define SUPERGENIUS_AUTHORITY_HPP

#include <cstdint>
#include "primitives/session_key.hpp"

namespace sgns::primitives {
  using AuthorityWeight = uint64_t;

  /**
   * Authority id
   */
  struct AuthorityId {
    // TODO(kamilsa): id types should be different for Production and Finality
    // Authority Ids
    SessionKey id;

    bool operator==(const AuthorityId &other) const {
      return id == other.id;
    }
    bool operator!=(const AuthorityId &other) const {
      return !(*this == other);
    }
    //added by Jin to fix link error in test
    friend std::ostream &operator<<(std::ostream &out, const AuthorityId &a)
    {
      return out << a.id; 
    }
    //end
  };

  inline bool operator<(const AuthorityId &lhs, const AuthorityId &rhs) {
    return lhs.id < rhs.id;
  }

  /**
   * Authority index
   */
  using AuthorityIndex = uint64_t;

  /**
   * Authority, which participate in block production and finalization
   */
  struct Authority {
    AuthorityId id;
    AuthorityWeight weight{};

    bool operator==(const Authority &other) const {
      return id == other.id && weight == other.weight;
    }
    bool operator!=(const Authority &other) const {
      return !(*this == other);
    }
    //added to fix link errors
    friend std::ostream &operator<<(std::ostream &out, const Authority &test_struct)
    {
        return out << test_struct.id;
    }
    //end
  };

  /**
   * @brief outputs object of type AuthorityId to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const AuthorityId &a) {
    return s << a.id;
  }

  /**
   * @brief decodes object of type AuthorityId from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to decode
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, AuthorityId &a) {
    return s >> a.id;
  }

  /**
   * @brief outputs object of type Authority to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const Authority &a) {
    return s << a.id << a.weight;
  }

  /**
   * @brief decodes object of type Authority from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to decode
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, Authority &a) {
    return s >> a.id >> a.weight;
  }

  /// Special type for vector of authorities
  struct AuthorityList : public std::vector<Authority> {
    // Attention: When adding a member, we need to ensure correct
    // destruction to avoid memory leaks or any other problem
    using std::vector<Authority>::vector;
  };
}  // namespace sgns::primitives

#endif  // SUPERGENIUS_AUTHORITY_HPP
