/**
 * @file soralog_shutdown.hpp
 * @brief Deterministic shutdown helpers for bounded asynchronous Soralog sinks.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <soralog/logging_system.hpp>
#include <soralog/sink.hpp>

namespace sgns::logging
{
    /**
     * Drain a bounded sink after all of its producers have stopped.
     *
     * The pinned Soralog file sink consumes at most one queued event from each
     * flush() call. Its destructor otherwise waits one configured latency
     * interval per remaining event while joining the sink worker. Calling
     * flush() once per queue slot makes destruction deterministic without
     * dropping records or detaching the worker.
     */
    inline void DrainBoundedSinkForShutdown(
        const std::shared_ptr<soralog::LoggingSystem> &logging_system,
        const std::string                             &sink_name,
        std::size_t                                    capacity ) noexcept
    {
        if ( !logging_system )
        {
            return;
        }

        const auto sink = logging_system->getSink( sink_name );
        if ( !sink )
        {
            return;
        }

        for ( std::size_t slot = 0; slot < capacity; ++slot )
        {
            sink->flush();
        }
    }
}
