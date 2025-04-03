#include "base/logger.hpp"
#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

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

    std::shared_ptr<spdlog::logger> createLogger( const std::string &tag, bool debug_mode = false, const std::string &basepath = "" )
    {
        std::shared_ptr<spdlog::logger> logger;
#if defined( ANDROID )
        if (basepath.size() > 0)
        {
            logger = spdlog::basic_logger_mt(tag, basepath);
        }
        else {
            logger = spdlog::android_logger_mt(tag);
        }
        
#else
        if (basepath.size() > 0)
        {
            logger = spdlog::basic_logger_mt(tag, basepath);
        }
        else {
            logger = spdlog::stdout_color_mt(tag); 
        }
        
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
    Logger createLogger( const std::string &tag, const std::string& basepath )
    {
        static std::mutex           mutex;
        std::lock_guard<std::mutex> lock( mutex );
        auto                        logger = spdlog::get( tag );
        if ( logger == nullptr )
        {
            logger = ::createLogger( tag, false, basepath );
        }
        return logger;
    }
} // namespace sgns::base
