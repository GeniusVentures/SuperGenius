/**
 * @file       MigrationAllowList.hpp
 * @brief      Persistent allow-list used to track balances eligible for migration claims.
 * @date       2026-05-01
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_MIGRATION_ALLOW_LIST_HPP
#define SGNS_MIGRATION_ALLOW_LIST_HPP

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
    /**
     * @brief Stores observed legacy balances and validates migration claim eligibility.
     */
    class MigrationAllowList
    {
    public:
        /**
         * @brief Address and observed balance pair stored in the migration allow-list.
         */
        using AddressBalance = std::pair<std::string, uint64_t>;

        /**
         * @brief Creates an allow-list view scoped to a specific migration version.
         * @param[in] db RocksDB instance used to persist and query observed balances.
         * @param[in] migration_version Source migration version namespace, for example "3.6.0".
         */
        MigrationAllowList( std::shared_ptr<storage::rocksdb> db, std::string migration_version );

        /**
         * @brief Persists the observed legacy balance for a single address.
         * @param[in] address Legacy source address whose balance was observed.
         * @param[in] balance Observed balance for @p address.
         * @return Success when the balance is written, or a database error on failure.
         */
        outcome::result<void>                    StoreObservedBalance( const std::string &address, uint64_t balance );

        /**
         * @brief Persists observed legacy balances for multiple addresses.
         * @param[in] balances Address and balance pairs to persist.
         * @return Success when every balance is written, or the first write error encountered.
         */
        outcome::result<void>                    StoreObservedBalances( const std::vector<AddressBalance> &balances );

        /**
         * @brief Loads the observed legacy balance for a single address when present.
         * @param[in] address Legacy source address to look up.
         * @return Optional observed balance; empty when no allow-list entry exists for @p address.
         */
        outcome::result<std::optional<uint64_t>> LoadObservedBalance( const std::string &address ) const;

        /**
         * @brief Checks whether an address may claim the provided migrated balance.
         * @param[in] address Legacy source address making the migration claim.
         * @param[in] claimed_balance Balance claimed by the migration transaction.
         * @return True when @p address exists in the allow-list and @p claimed_balance is no more than twice the observed balance.
         * @note The maximum allowed claim saturates at @c std::numeric_limits<uint64_t>::max() to avoid overflow.
         */
        outcome::result<bool>                    IsEligible( const std::string &address, uint64_t claimed_balance ) const;

        /**
         * @brief Lists every stored observed balance in the current migration namespace.
         * @return Address and balance pairs sorted by address.
         */
        outcome::result<std::vector<AddressBalance>> ListObservedBalances() const;

        /**
         * @brief Builds the RocksDB key prefix used for a migration version namespace.
         * @param[in] migration_version Source migration version namespace.
         * @return Key prefix for all allow-list entries under @p migration_version.
         */
        static std::string BuildPrefix( std::string_view migration_version );

        /**
         * @brief Builds the RocksDB key used for a specific address inside a migration namespace.
         * @param[in] migration_version Source migration version namespace.
         * @param[in] address Legacy source address.
         * @return Full allow-list key for @p address under @p migration_version.
         */
        static std::string BuildKey( std::string_view migration_version, std::string_view address );

    private:
        friend class ::MigrationParamTest;

        /**
         * @brief Enables or disables eligibility checks for migration tests.
         * @param[in] enabled True to enforce allow-list checks, false to allow every claim.
         */
        static void SetEligibilityCheckEnabledForTests( bool enabled );

        /**
         * @brief Returns whether eligibility checks are enabled for migration tests.
         * @return True when allow-list eligibility is enforced.
         */
        static bool IsEligibilityCheckEnabledForTests();

        /**
         * @brief Decodes a stored observed balance from its serialized database value.
         * @param[in] buffer Buffer containing an encoded 64-bit balance.
         * @return Decoded balance, or @c std::errc::bad_message when the buffer size is invalid.
         */
        static outcome::result<uint64_t> DecodeBalance( const base::Buffer &buffer );

        std::shared_ptr<storage::rocksdb> db_;                 ///< Database storing allow-list entries.
        std::string                       migration_version_;  ///< Source migration version namespace.
        std::string                       prefix_;             ///< Cached key prefix for @ref migration_version_.
        base::Logger                      logger_ = base::createLogger( "MigrationAllowList" ); ///< Component logger.
    };
}

#endif // SGNS_MIGRATION_ALLOW_LIST_HPP
