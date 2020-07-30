

#ifndef SUPERGENIUS_SRC_PRIMITIVES_EXTRINSIC_API_PRIMITIVES_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_EXTRINSIC_API_PRIMITIVES_HPP

#include <memory>

#include <boost/optional.hpp>
#include <boost/variant.hpp>

/**
 * Authoring api primitives
 */

namespace sgns::primitives {
  /**
   * @brief SubscriptionId primitive
   */
  using SubscriptionId = uint64_t;

  /**
   * @brief Session primitive
   */
  struct Session {
    uint32_t id{};
  };
  // TODO(yuraz): PRE-221 investigate and implement Session primitive

  // /**
  //  * @brief Metadata primitive
  //  */
  // using Metadata = boost::optional<std::shared_ptr<Session>>;

  /**
   * @brief Subscriber primitive
   */
  struct Subscriber {
    uint32_t id{};
  };

  /**
   * @brief ExtrinsicKey is used as a key to search extrinsic
   */
  using ExtrinsicKey = std::vector<uint8_t>;
  // TODO(yuraz): PRE-221 investigate and implement Subscriber primitive

}  // namespace sgns::primitives

#endif  //SUPERGENIUS_SRC_PRIMITIVES_EXTRINSIC_API_PRIMITIVES_HPP
