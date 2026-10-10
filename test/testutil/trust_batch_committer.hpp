#ifndef SGNS_TESTUTIL_TRUST_BATCH_COMMITTER_HPP
#define SGNS_TESTUTIL_TRUST_BATCH_COMMITTER_HPP

#include <functional>
#include <system_error>
#include <utility>
#include <vector>

#include "storage/rocksdb/rocksdb.hpp"
#include "trustedpeer/TrustStateStore.hpp"

namespace sgns::test
{
    /// Same write path as TrustStateStore's default committer; shared so the
    /// injection variants cannot drift from the production layout.
    inline outcome::result<void> CommitWritesToBatch(
        storage::rocksdb                              &database,
        const std::vector<trustedpeer::TrustStateStore::Write> &writes )
    {
        auto batch = database.batch();
        if ( !batch )
        {
            return outcome::failure( std::errc::io_error );
        }
        for ( const auto &[key, value] : writes )
        {
            auto put = batch->put( key, value );
            if ( put.has_error() )
            {
                return put.error();
            }
        }
        return batch->commit();
    }

    /// BatchCommitter over CommitWritesToBatch with optional failure injection.
    inline trustedpeer::TrustStateStore::BatchCommitter MakeBatchCommitter(
        std::function<bool()> should_fail = {} )
    {
        return [should_fail = std::move( should_fail )](
                   storage::rocksdb                              &database,
                   const std::vector<trustedpeer::TrustStateStore::Write> &writes )
                   -> outcome::result<void>
        {
            if ( should_fail && should_fail() )
            {
                return outcome::failure( std::errc::io_error );
            }
            return CommitWritesToBatch( database, writes );
        };
    }
} // namespace sgns::test

#endif // SGNS_TESTUTIL_TRUST_BATCH_COMMITTER_HPP
