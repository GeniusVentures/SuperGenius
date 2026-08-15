#include "crdt/globaldb/crdt_work_journal.hpp"

#include <algorithm>
#include <regex>

#include "base/buffer.hpp"
#include "storage/rocksdb/rocksdb.hpp"

namespace sgns::crdt
{
    std::shared_ptr<CRDTWorkJournal> CRDTWorkJournal::New( std::shared_ptr<storage::rocksdb> datastore )
    {
        if ( !datastore )
        {
            return nullptr;
        }
        return std::shared_ptr<CRDTWorkJournal>( new CRDTWorkJournal( std::move( datastore ) ) );
    }

    CRDTWorkJournal::CRDTWorkJournal( std::shared_ptr<storage::rocksdb> datastore ) :
        datastore_( std::move( datastore ) )
    {
    }

    void CRDTWorkJournal::MarkSeen( const std::string &key )
    {
        if ( key.empty() )
        {
            return;
        }
        std::lock_guard<std::mutex> lock( mutex_ );
        auto                        maybe_entry = GetEntryUnlocked( key );

        Entry entry;
        if ( maybe_entry.has_value() )
        {
            entry = maybe_entry.value();
            if ( entry.state == State::Processing )
            {
                return;
            }
        }
        entry.key            = key;
        entry.state          = State::Seen;
        entry.updated_at_ms  = NowMs();
        entry.lease_until_ms = 0;
        PutEntryUnlocked( entry );
    }

    void CRDTWorkJournal::MarkProcessing( const std::string &key, std::chrono::milliseconds lease )
    {
        if ( key.empty() )
        {
            return;
        }
        std::lock_guard<std::mutex> lock( mutex_ );
        auto                        maybe_entry = GetEntryUnlocked( key );
        if ( !maybe_entry.has_value() )
        {
            return;
        }

        auto entry            = maybe_entry.value();
        entry.state           = State::Processing;
        entry.updated_at_ms   = NowMs();
        entry.lease_until_ms  = entry.updated_at_ms + static_cast<uint64_t>( std::max<int64_t>( 0, lease.count() ) );
        entry.attempt_count  += 1;
        PutEntryUnlocked( entry );
    }

    void CRDTWorkJournal::MarkStalled( const std::string &key, std::chrono::milliseconds lease )
    {
        if ( key.empty() )
        {
            return;
        }
        std::lock_guard<std::mutex> lock( mutex_ );
        auto                        maybe_entry = GetEntryUnlocked( key );
        if ( !maybe_entry.has_value() )
        {
            return;
        }

        auto entry            = maybe_entry.value();
        entry.state           = State::Stalled;
        entry.updated_at_ms   = NowMs();
        entry.lease_until_ms  = entry.updated_at_ms + static_cast<uint64_t>( std::max<int64_t>( 0, lease.count() ) );
        entry.attempt_count  += 1;
        PutEntryUnlocked( entry );
    }

    bool CRDTWorkJournal::MarkDone( const std::string &key )
    {
        if ( key.empty() )
        {
            return false;
        }
        std::lock_guard<std::mutex> lock( mutex_ );
        if ( !datastore_ )
        {
            return false;
        }
        base::Buffer key_buf;
        key_buf.put( BuildStorageKey( key ) );
        return datastore_->remove( key_buf ).has_value();
    }

    std::optional<CRDTWorkJournal::Entry> CRDTWorkJournal::GetEntry( const std::string &key ) const
    {
        if ( key.empty() )
        {
            return std::nullopt;
        }
        std::lock_guard<std::mutex> lock( mutex_ );
        return GetEntryUnlocked( key );
    }

    std::vector<CRDTWorkJournal::Entry> CRDTWorkJournal::ListUnfinished( std::optional<std::regex> pattern ) const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        std::vector<Entry>          out;
        if ( !datastore_ )
        {
            return out;
        }

        base::Buffer prefix_buf;
        prefix_buf.put( NAMESPACE_PREFIX );
        auto result = datastore_->query( prefix_buf );
        if ( result.has_error() )
        {
            return out;
        }

        out.reserve( result.value().size() );
        for ( const auto &[raw_key, raw_value] : result.value() )
        {
            auto parsed = DeserializeEntry( raw_key.toString(), raw_value.toString() );
            if ( parsed.has_value() )
            {
                if ( pattern.has_value() && !std::regex_match( parsed->key, pattern.value() ) )
                {
                    continue;
                }
                out.push_back( std::move( parsed.value() ) );
            }
        }
        return out;
    }

    size_t CRDTWorkJournal::RecoverStaleProcessing( std::optional<std::regex> pattern, std::chrono::milliseconds stale )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        if ( !datastore_ )
        {
            return 0;
        }

        const uint64_t now_ms    = NowMs();
        const uint64_t grace_ms  = static_cast<uint64_t>( std::max<int64_t>( 0, stale.count() ) );
        size_t         recovered = 0;

        base::Buffer prefix_buf;
        prefix_buf.put( NAMESPACE_PREFIX );
        auto result = datastore_->query( prefix_buf );
        if ( result.has_error() )
        {
            return recovered;
        }

        for ( const auto &[raw_key, raw_value] : result.value() )
        {
            auto parsed = DeserializeEntry( raw_key.toString(), raw_value.toString() );
            if ( !parsed.has_value() )
            {
                continue;
            }
            auto &entry = parsed.value();
            if ( pattern.has_value() && !std::regex_match( entry.key, pattern.value() ) )
            {
                continue;
            }
            if ( entry.state != State::Processing )
            {
                continue;
            }
            if ( entry.lease_until_ms != 0 && entry.lease_until_ms + grace_ms > now_ms )
            {
                continue;
            }
            entry.state          = State::Stalled;
            entry.updated_at_ms  = now_ms;
            entry.lease_until_ms = 0;
            PutEntryUnlocked( entry );
            recovered += 1;
        }
        return recovered;
    }

    uint64_t CRDTWorkJournal::NowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    std::string CRDTWorkJournal::BuildStorageKey( const std::string &key ) const
    {
        return std::string( NAMESPACE_PREFIX ) + key;
    }

    std::optional<CRDTWorkJournal::Entry> CRDTWorkJournal::DeserializeEntry( std::string_view storage_key,
                                                                             std::string_view value )
    {
        auto fields = Split( std::string( value ), '|' );
        if ( fields.size() != 5 || fields[0] != "v1" )
        {
            return std::nullopt;
        }
        Entry entry;
        try
        {
            const auto state = std::stoi( fields[1] );
            if ( state != static_cast<int>( State::Seen ) && state != static_cast<int>( State::Processing ) &&
                 state != static_cast<int>( State::Stalled ) )
            {
                return std::nullopt;
            }
            entry.state          = static_cast<State>( state );
            entry.attempt_count  = static_cast<uint64_t>( std::stoull( fields[2] ) );
            entry.updated_at_ms  = static_cast<uint64_t>( std::stoull( fields[3] ) );
            entry.lease_until_ms = static_cast<uint64_t>( std::stoull( fields[4] ) );
        }
        catch ( ... )
        {
            return std::nullopt;
        }

        const auto key_str = std::string( storage_key );
        const auto pos     = key_str.find( "/crdt/work/" );
        if ( pos == std::string::npos )
        {
            return std::nullopt;
        }
        entry.key = key_str.substr( pos + std::string( "/crdt/work/" ).size() );
        return entry;
    }

    std::string CRDTWorkJournal::SerializeEntry( const Entry &entry )
    {
        return "v1|" + std::to_string( static_cast<int>( entry.state ) ) + "|" + std::to_string( entry.attempt_count ) +
               "|" + std::to_string( entry.updated_at_ms ) + "|" + std::to_string( entry.lease_until_ms );
    }

    std::vector<std::string> CRDTWorkJournal::Split( const std::string &value, char separator )
    {
        std::vector<std::string> out;
        size_t                   start = 0;
        while ( true )
        {
            const auto pos = value.find( separator, start );
            if ( pos == std::string::npos )
            {
                out.push_back( value.substr( start ) );
                break;
            }
            out.push_back( value.substr( start, pos - start ) );
            start = pos + 1;
        }
        return out;
    }

    std::optional<CRDTWorkJournal::Entry> CRDTWorkJournal::GetEntryUnlocked( const std::string &key ) const
    {
        base::Buffer key_buf;
        key_buf.put( BuildStorageKey( key ) );
        auto maybe_value = datastore_->get( key_buf );
        if ( maybe_value.has_error() )
        {
            return std::nullopt;
        }
        return DeserializeEntry( BuildStorageKey( key ), maybe_value.value().toString() );
    }

    bool CRDTWorkJournal::PutEntryUnlocked( const Entry &entry ) const
    {
        base::Buffer key_buf;
        key_buf.put( BuildStorageKey( entry.key ) );
        base::Buffer value_buf;
        value_buf.put( SerializeEntry( entry ) );
        return datastore_->put( key_buf, value_buf ).has_value();
    }
}
