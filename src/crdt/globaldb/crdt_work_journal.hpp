/**
 * @file       crdt_work_journal.hpp
 * @brief      Persistent work-journal for CRDT key processing state.
 * @date       2026-04-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef SUPERGENIUS_CRDT_WORK_JOURNAL_HPP
#define SUPERGENIUS_CRDT_WORK_JOURNAL_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgns::storage
{
    class rocksdb;
}

namespace sgns::crdt
{
    /**
     * @brief Tracks key processing lifecycle persisted in RocksDB.
     *
     * Entries are stored under the internal work namespace and can be
     * recovered across process restarts.
     */
    class CRDTWorkJournal
    {
    public:
        /**
         * @brief Processing state for a tracked key.
         */
        enum class State : uint8_t
        {
            Seen       = 0, ///< Key was discovered and is pending work.
            Processing = 1, ///< Key is currently being processed.
            Stalled    = 2, ///< Key processing stalled and requires retry.
        };

        /**
         * @brief Serialized work-journal entry for a key.
         */
        struct Entry
        {
            std::string key;                          ///< Logical key being tracked.
            State       state          = State::Seen; ///< Current processing state.
            uint64_t    attempt_count  = 0;           ///< Number of processing/stall transitions.
            uint64_t    updated_at_ms  = 0;           ///< Last update time in Unix milliseconds.
            uint64_t    lease_until_ms = 0;           ///< Processing lease deadline in Unix milliseconds.
        };

        /**
         * @brief Creates a work journal backed by RocksDB.
         * @param[in] datastore RocksDB instance used to persist entries.
         * @return Shared pointer to a new journal, or `nullptr` when datastore is null.
         */
        static std::shared_ptr<CRDTWorkJournal> New( std::shared_ptr<storage::rocksdb> datastore );

        /**
         * @brief Marks a key as seen and pending processing.
         * @param[in] key Logical key to track.
         */
        void MarkSeen( const std::string &key );

        /**
         * @brief Marks an existing key as processing with a lease.
         * @param[in] key Logical key to update.
         * @param[in] lease Processing lease duration. Negative values are treated as zero.
         */
        void MarkProcessing( const std::string &key, std::chrono::milliseconds lease = std::chrono::minutes( 3 ) );

        /**
         * @brief Marks an existing key as stalled with a lease.
         * @param[in] key Logical key to update.
         * @param[in] lease Stall lease duration. Negative values are treated as zero.
         */
        void MarkStalled( const std::string &key, std::chrono::milliseconds lease = std::chrono::minutes( 3 ) );

        /**
         * @brief Removes a key from the journal.
         * @param[in] key Logical key to remove.
         * @return `true` when deletion succeeds, otherwise `false`.
         */
        bool MarkDone( const std::string &key );

        /**
         * @brief Retrieves one entry by logical key.
         * @param[in] key Logical key to fetch.
         * @return Journal entry when present and parseable, otherwise `std::nullopt`.
         */
        std::optional<Entry> GetEntry( const std::string &key ) const;

        /**
         * @brief Lists all unfinished entries, optionally filtered by key regex.
         * @param[in] key_pattern Optional regex applied to logical keys.
         * @return Vector with all matching unfinished entries.
         */
        std::vector<Entry> ListUnfinished( std::string_view key_pattern = {} ) const;

        /**
         * @brief Converts stale processing entries to stalled.
         * @param[in] key_pattern Regex to limit keys considered for recovery.
         * @param[in] stale Extra grace period in milliseconds before recovery.
         * @return Number of recovered entries.
         */
        size_t RecoverStaleProcessing( std::string_view          key_pattern,
                                       std::chrono::milliseconds stale = std::chrono::milliseconds( 0 ) );

    private:
        static constexpr std::string_view NAMESPACE_PREFIX = "/crdt/work/"; ///< Storage key namespace.

        /**
         * @brief Constructs a work journal.
         * @param[in] datastore RocksDB instance used to persist entries.
         */
        explicit CRDTWorkJournal( std::shared_ptr<storage::rocksdb> datastore );

        /**
         * @brief Returns current Unix time in milliseconds.
         * @return Current timestamp in milliseconds.
         */
        static uint64_t NowMs();

        /**
         * @brief Builds the internal storage key for a logical key.
         * @param[in] key Logical key.
         * @return Namespaced storage key used in RocksDB.
         */
        std::string BuildStorageKey( const std::string &key ) const;

        /**
         * @brief Parses a serialized entry payload.
         * @param[in] storage_key Full storage key used to infer logical key.
         * @param[in] value Serialized value payload.
         * @return Parsed entry, or `std::nullopt` when format/values are invalid.
         */
        static std::optional<Entry> DeserializeEntry( std::string_view storage_key, std::string_view value );

        /**
         * @brief Serializes an entry payload.
         * @param[in] entry Entry to serialize.
         * @return String payload suitable for RocksDB storage.
         */
        static std::string SerializeEntry( const Entry &entry );

        /**
         * @brief Splits a string by a separator.
         * @param[in] value Input string.
         * @param[in] separator Delimiter character.
         * @return List of split fields.
         */
        static std::vector<std::string> Split( const std::string &value, char separator );

        /**
         * @brief Retrieves an entry without taking the mutex.
         * @param[in] key Logical key to fetch.
         * @return Journal entry when available and parseable, otherwise `std::nullopt`.
         */
        std::optional<Entry> GetEntryUnlocked( const std::string &key ) const;

        /**
         * @brief Stores an entry without taking the mutex.
         * @param[in] entry Entry to persist.
         * @return `true` when persisted successfully, otherwise `false`.
         */
        bool PutEntryUnlocked( const Entry &entry ) const;

        std::shared_ptr<storage::rocksdb> datastore_; ///< Backing datastore for journal state.
        mutable std::mutex                mutex_;     ///< Synchronizes public journal operations.
    };
}

#endif // SUPERGENIUS_CRDT_WORK_JOURNAL_HPP
