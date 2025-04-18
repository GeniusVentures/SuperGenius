

#ifndef SUPERGENIUS_SRC_PRIMITIVES_TRANSACTION_VALIDITY_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_TRANSACTION_VALIDITY_HPP

#include <cstdint>
#include <vector>

#include <boost/variant.hpp>
#include "outcome/outcome.hpp"
#include "primitives/transaction.hpp"

namespace sgns::primitives {

  /**
   * @brief Information concerning a valid transaction.
   *
   * This is the same structure as in
  */
  struct ValidTransaction {
    /**
     * @brief Priority of the transaction.
     * Priority determines the ordering of two transactions that have all
     * their dependencies (required tags) satisfied.
     */
    Transaction::Priority priority{};

    /**
     * @brief Transaction dependencies
     * A non-empty list signifies that some other transactions which provide
     * given tags are required to be included before that one.
     */
    std::vector<Transaction::Tag> requires;

    /**
     * @brief Provided tags
     * A list of tags this transaction provides. Successfuly importing the
     * transaction will enable other transactions that depend on (require)
     * those tags to be included as well. Provided and requried tags allow
     * Substrate to build a dependency graph of transactions and import them
     * in the right (linear) order.
     */
    std::vector<Transaction::Tag> provides;

    /**
     * @brief Transaction longevity
     * Longevity describes minimum number of blocks the validity is correct.
     * After this period transaction should be removed from the pool or
     * revalidated.
     */
    Transaction::Longevity longevity{};

    /**
     * @brief A flag indicating if the transaction should be propagated to other
     * peers. By setting `false` here the transaction will still be considered
     * for including in blocks that are authored on the current node, but will
     * never be sent to other peers.
     */
    bool propagate{};

    bool operator==(const ValidTransaction &rhs) const {
      return priority == rhs.priority && requires == rhs.requires
             && provides == rhs.provides && longevity == rhs.longevity
             && propagate == rhs.propagate;
    }

    bool operator!=(const ValidTransaction &rhs) const {
      return !operator==(rhs);
    }
    //added to fix link error
    friend std::ostream &operator<<(std::ostream &out, const ValidTransaction &v)
    {
      out << v.priority;
      out << v.requires.size() ;
      for(auto it = v.requires.begin() ; it != v.requires.end() ; it++)
      {
      //   // out << *it;
      }
      out << v.provides.size();
      for(auto it = v.provides.begin() ; it != v.provides.end() ; it++)
      {
      //   // out << *it;
      }
      out << v.longevity
             << v.propagate;
      return out;
    }
    //end
  };

  /// Transaction is invalid. Details are described by the error code.
  enum class InvalidTransaction : uint8_t {
    /// The call of the transaction is not expected.
    Call = 1,
    /// General error to do with the inability to pay some fees (e.g. account
    /// balance too low).
    Payment,
    /// General error to do with the transaction not yet being valid (e.g. nonce
    /// too high).
    Future,
    /// General error to do with the transaction being outdated (e.g. nonce too
    /// low).
    Stale,
    /// General error to do with the transaction's proofs (e.g. signature).
    BadProof,
    /// The transaction birth block is ancient.
    AncientBirthBlock,
    /// The transaction would exhaust the resources of current block.
    ///
    /// The transaction might be valid, but there are not enough resources left
    /// in the current block.
    ExhaustsResources,
    /// Any other custom invalid validity that is not covered by this enum.
    Custom
  };

  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const InvalidTransaction &v) {
    // -1 is needed for compatibility with Rust; indices of error codes start
    // from 0 there, while in supergenius they must start from 1 because of
    // std::error_code policy
    return s << static_cast<uint8_t>(v) - 1;
  }

  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, InvalidTransaction &v) {
    uint8_t value = 0u;
    s >> value;

    // increment is needed for compatibility with Rust; indices of error codes
    // start from 0 there, while in supergenius they must start from 1 because of
    // std::error_code policy
    value++;
    if (value > static_cast<uint8_t>(InvalidTransaction::ExhaustsResources)) {
      v = InvalidTransaction::Custom;
    } else {
      v = static_cast<InvalidTransaction>(value);
    }
    return s;
  }

  /// An unknown transaction validity.
  enum class UnknownTransaction : uint8_t {
    /// Could not lookup some information that is required to validate the
    /// transaction.
    CannotLookup = 1,
    /// No validator found for the given unsigned transaction.
    NoUnsignedValidator,
    /// Any other custom unknown validity that is not covered by this enum.
    Custom
  };

  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const UnknownTransaction &v) {
    // -1 is needed for compatibility with Rust; indices of error codes start
    // from 0 there, while in supergenius they must start from 1 because of
    // std::error_code policy
    return s << static_cast<uint8_t>(v) - 1;
  }

  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, UnknownTransaction &v) {
    uint8_t value = 0u;
    s >> value;

    // increment is needed for compatibility with Rust; indices of error codes
    // start from 0 there, while in supergenius they must start from 1 because of
    // std::error_code policy
    value++;
    if (value > static_cast<uint8_t>(UnknownTransaction::NoUnsignedValidator)) {
      v = UnknownTransaction::Custom;
    } else {
      v = static_cast<UnknownTransaction>(value);
    }
    return s;
  }

  using TransactionValidityError =
      boost::variant<InvalidTransaction, UnknownTransaction>;

  /**
   * Information on a transaction's validity and, if valid, on how it relate to
   * other transactions.
   */
  using TransactionValidity =
      boost::variant<ValidTransaction, TransactionValidityError>;

  /**
   * @brief outputs object of type Valid to stream
   * @tparam Stream stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const ValidTransaction &v) {
    return s << v.priority << v.requires << v.provides << v.longevity
             << v.propagate;
  }
  /**
   * @brief decodes object of type Valid from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to decode
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, ValidTransaction &v) {
    return s >> v.priority >> v.requires >> v.provides >> v.longevity
           >> v.propagate;
  }
}  // namespace sgns::primitives

OUTCOME_HPP_DECLARE_ERROR_2(sgns::primitives, InvalidTransaction)
OUTCOME_HPP_DECLARE_ERROR_2(sgns::primitives, UnknownTransaction)

#endif  // SUPERGENIUS_SRC_PRIMITIVES_TRANSACTION_VALIDITY_HPP
