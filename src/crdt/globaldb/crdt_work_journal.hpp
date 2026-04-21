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
    class CRDTWorkJournal
    {
    public:
        enum class State : uint8_t
        {
            Seen       = 0,
            Processing = 1,
        };

        struct Entry
        {
            std::string key;
            State       state          = State::Seen;
            uint64_t    attempt_count  = 0;
            uint64_t    updated_at_ms  = 0;
            uint64_t    lease_until_ms = 0;
        };

        explicit CRDTWorkJournal( std::shared_ptr<storage::rocksdb> datastore );

        void MarkSeen( const std::string &key );
        void MarkProcessing( const std::string &key, std::chrono::milliseconds lease = std::chrono::minutes( 5 ) );
        bool MarkDone( const std::string &key );

        std::optional<Entry> GetEntry( const std::string &key ) const;
        std::vector<Entry>   ListUnfinished() const;
        size_t               RecoverStaleProcessing( std::chrono::milliseconds stale = std::chrono::milliseconds( 0 ) );

    private:
        static constexpr std::string_view NAMESPACE_PREFIX = "/crdt/work/";
        static uint64_t                   NowMs();
        std::string                       BuildStorageKey( const std::string &key ) const;
        static std::optional<Entry>       DeserializeEntry( std::string_view storage_key, std::string_view value );
        static std::string                SerializeEntry( const Entry &entry );
        static std::vector<std::string>   Split( const std::string &value, char separator );

        std::optional<Entry> GetEntryUnlocked( const std::string &key ) const;
        bool                 PutEntryUnlocked( const Entry &entry ) const;

        std::shared_ptr<storage::rocksdb> datastore_;
        mutable std::mutex                mutex_;
    };
}

#endif // SUPERGENIUS_CRDT_WORK_JOURNAL_HPP
