
#ifndef SUPERGENIUS_APPLICATION_UTIL_HPP
#define SUPERGENIUS_APPLICATION_UTIL_HPP

#include <boost/optional.hpp>
#include "application/impl/config_reader/error.hpp"

namespace sgns::application {

  template <typename T>
  outcome::result<std::decay_t<T>> ensure(boost::optional<T> opt_entry) {
    if (! opt_entry) {
      return ConfigReaderError::MISSING_ENTRY;
    }
    return opt_entry.value();
  }

}  // namespace sgns::application

#endif  // SUPERGENIUS_APPLICATION_UTIL_HPP
