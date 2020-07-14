

#ifndef SUPERGENIUS_LOGGER_HPP
#define SUPERGENIUS_LOGGER_HPP

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

namespace sgns::base {
  using Logger = std::shared_ptr<spdlog::logger>;

  /**
   * Provide logger object
   * @param tag - tagging name for identifying logger
   * @return logger object
   */
  Logger createLogger(const std::string &tag);
}  // namespace sgns::base

#endif  // SUPERGENIUS_LOGGER_HPP
