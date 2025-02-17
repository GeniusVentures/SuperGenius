#ifndef SUPERGENIUS_SRC_COMMON_OUTCOME_THROW_HPP
#define SUPERGENIUS_SRC_COMMON_OUTCOME_THROW_HPP

#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>

namespace sgns::base {
  /**
   * @brief throws outcome::result error as boost exception
   * @tparam T enum error type, only outcome::result enums are allowed
   * @param t error value
   */
  template <typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
  void raise(T t) {
    std::error_code ec = make_error_code(t);
    boost::throw_exception(std::system_error(ec));
  }

  /**
   * @brief throws outcome::result error made of error as boost exception
   * @tparam T outcome error type
   * @param t outcome error value
   */
  template <typename T, typename = std::enable_if_t<!std::is_enum_v<T>>>
  void raise(const T &t) {
    boost::throw_exception(std::system_error(t.value(), t.category()));
  }
}  // namespace sgns::base

#endif  // SUPERGENIUS_SRC_COMMON_OUTCOME_THROW_HPP
