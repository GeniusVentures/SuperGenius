#ifndef SUPERGENIUS_LOGGER_HPP
#define SUPERGENIUS_LOGGER_HPP

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

#if defined( ANDROID )
#include <spdlog/sinks/android_sink.h>
#endif

namespace sgns::base
{
    using Logger = std::shared_ptr<spdlog::logger>;

    /**
     * @brief Create a logger instance.
     * @param tag Tagging name for identifying logger.
     * @param basepath Optional base path for log output (platform dependent).
     * @return Logger object.
     */
    Logger createLogger( const std::string &tag, const std::string &basepath = "" );
} // namespace sgns::base

#endif // SUPERGENIUS_LOGGER_HPP
