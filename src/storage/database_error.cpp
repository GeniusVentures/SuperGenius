#include "storage/database_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::storage, DatabaseError, e )
{
    using E = sgns::storage::DatabaseError;

    switch ( e )
    {
        case E::OK:
            return "success";
        case E::NOT_SUPPORTED:
            return "operation is not supported";
        case E::CORRUPTION:
            return "data corruption";
        case E::INVALID_ARGUMENT:
            return "invalid argument";
        case E::IO_ERROR:
            return "IO error";
        case E::NOT_FOUND:
            return "not found";
        case E::MERGE_IN_PROGRESS:
            return "merge in progress";
        case E::INCOMPLETE:
            return "incomplete";
        case E::SHUTDOWN_IN_PROGRESS:
            return "shutdown in progress";
        case E::TIMED_OUT:
            return "timed out";
        case E::ABORTED:
            return "aborted";
        case E::BUSY:
            return "busy";
        case E::EXPIRED:
            return "expired";
        case E::TRY_AGAIN_:
            return "try again";
        case E::COMPACTION_TOO_LARGE:
            return "compaction too large";
        case E::COLUMN_FAMILY_DROPPED:
            return "column family dropped";
        case E::UNITIALIZED:
            return "unitialized";
        case E::UNKNOWN:
            break;
    }

    return "unknown error";
}

namespace sgns::storage
{
    DatabaseError error_from_rocksdb( rocksdb::Status::Code code )
    {
        using E = sgns::storage::DatabaseError;

        switch ( code )
        {
            case rocksdb::Status::kOk:
                return E::OK;
            case rocksdb::Status::kNotFound:
                return E::NOT_FOUND;
            case rocksdb::Status::kCorruption:
                return E::CORRUPTION;
            case rocksdb::Status::kNotSupported:
                return E::NOT_SUPPORTED;
            case rocksdb::Status::kInvalidArgument:
                return E::INVALID_ARGUMENT;
            case rocksdb::Status::kIOError:
                return E::IO_ERROR;
            case rocksdb::Status::kMergeInProgress:
                return E::MERGE_IN_PROGRESS;
            case rocksdb::Status::kIncomplete:
                return E::INCOMPLETE;
            case rocksdb::Status::kShutdownInProgress:
                return E::SHUTDOWN_IN_PROGRESS;
            case rocksdb::Status::kTimedOut:
                return E::TIMED_OUT;
            case rocksdb::Status::kAborted:
                return E::ABORTED;
            case rocksdb::Status::kBusy:
                return E::BUSY;
            case rocksdb::Status::kExpired:
                return E::EXPIRED;
            case rocksdb::Status::kTryAgain:
                return E::TRY_AGAIN_;
            case rocksdb::Status::kCompactionTooLarge:
                return E::COMPACTION_TOO_LARGE;
            case rocksdb::Status::kColumnFamilyDropped:
                return E::COLUMN_FAMILY_DROPPED;
            case rocksdb::Status::kMaxCode:
                return E::UNKNOWN;
        }

        return E::UNKNOWN;
    }
}
