#pragma once

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "outcome/outcome.hpp"
#include "storage/rocksdb/rocksdb.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class MigrationParamTest;

namespace sgns
{
    class MigrationAllowList
    {
    public:
        using AddressBalance = std::pair<std::string, uint64_t>;

        MigrationAllowList( std::shared_ptr<storage::rocksdb> db, std::string migration_version );

        outcome::result<void>                    StoreObservedBalance( const std::string &address, uint64_t balance );
        outcome::result<void>                    StoreObservedBalances( const std::vector<AddressBalance> &balances );
        outcome::result<std::optional<uint64_t>> LoadObservedBalance( const std::string &address ) const;
        outcome::result<bool>                    IsEligible( const std::string &address, uint64_t claimed_balance ) const;
        outcome::result<std::vector<AddressBalance>> ListObservedBalances() const;

        static std::string BuildPrefix( std::string_view migration_version );
        static std::string BuildKey( std::string_view migration_version, std::string_view address );

    private:
        friend class ::MigrationParamTest;

        static void SetEligibilityCheckEnabledForTests( bool enabled );
        static bool IsEligibilityCheckEnabledForTests();
        static outcome::result<uint64_t> DecodeBalance( const base::Buffer &buffer );

        std::shared_ptr<storage::rocksdb> db_;
        std::string                       migration_version_;
        std::string                       prefix_;
        base::Logger                      logger_ = base::createLogger( "MigrationAllowList" );
    };
}
