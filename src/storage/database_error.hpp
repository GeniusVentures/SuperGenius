#ifndef STORAGE_DATABASE_ERROR_HPP
#define STORAGE_DATABASE_ERROR_HPP

#include "outcome/outcome.hpp"

#include <rocksdb/status.h>

namespace sgns::storage
{
    /**
    * @brief universal database interface error
    */
    enum class DatabaseError : uint8_t
    {
        OK,
        NOT_FOUND,
        CORRUPTION,
        NOT_SUPPORTED,
        INVALID_ARGUMENT,
        IO_ERROR,
        MERGE_IN_PROGRESS,
        INCOMPLETE,
        SHUTDOWN_IN_PROGRESS,
        TIMED_OUT,
        ABORTED,
        BUSY,
        EXPIRED,
        TRY_AGAIN_,
        COMPACTION_TOO_LARGE,
        COLUMN_FAMILY_DROPPED,
        UNITIALIZED,
        UNKNOWN,
    };

    DatabaseError error_from_rocksdb( rocksdb::Status::Code code );
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns::storage, DatabaseError );

#endif // STORAGE_DATABASE_ERROR_HPP
