#include "base/logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace
{
    void setGlobalPattern( spdlog::logger &logger )
    {
        logger.set_pattern( "[%Y-%m-%d %H:%M:%S][%l][%n] %v" );
    }

    void setDebugPattern( spdlog::logger &logger )
    {
        logger.set_pattern( "[%Y-%m-%d %H:%M:%S.%F][th:%t][%l][%n] %v" );
    }

    std::shared_ptr<spdlog::logger> createLogger( const std::string &tag, bool debug_mode = false )
    {
#if defined( ANDROID )
        auto logger = spdlog::android_logger_mt( tag );
#else
        auto logger = spdlog::stdout_color_mt( tag );
#endif
        if ( debug_mode )
        {
            setDebugPattern( *logger );
        }
        else
        {
            setGlobalPattern( *logger );
        }
        return logger;
    }
} // namespace

namespace sgns::base
{
    Logger createLogger( const std::string &tag )
    {
        static std::mutex           mutex;
        std::lock_guard<std::mutex> lock( mutex );
        auto                        logger = spdlog::get( tag );
        if ( logger == nullptr )
        {
            logger = ::createLogger( tag );
        }
        return logger;
    }
} // namespace sgns::base
