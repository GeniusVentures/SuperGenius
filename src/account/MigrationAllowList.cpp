#include "account/MigrationAllowList.hpp"

#include "storage/database_error.hpp"

#include <algorithm>
#include <limits>
#include <system_error>

namespace sgns
{
    namespace
    {
        constexpr std::string_view kPrefixBase = "/migration-allowlist";
    }

    MigrationAllowList::MigrationAllowList( std::shared_ptr<storage::rocksdb> db, std::string migration_version ) :
        db_( std::move( db ) ),
        migration_version_( std::move( migration_version ) ),
        prefix_( BuildPrefix( migration_version_ ) )
    {
    }

    outcome::result<void> MigrationAllowList::StoreObservedBalance( const std::string &address, uint64_t balance )
    {
        if ( !db_ )
        {
            return outcome::failure( storage::DatabaseError::UNITIALIZED );
        }

        base::Buffer key_buf;
        key_buf.put( BuildKey( migration_version_, address ) );

        base::Buffer value_buf;
        value_buf.putUint64( balance );

        BOOST_OUTCOME_TRY( db_->put( key_buf, value_buf ) );
        return outcome::success();
    }

    outcome::result<void> MigrationAllowList::StoreObservedBalances( const std::vector<AddressBalance> &balances )
    {
        for ( const auto &[address, balance] : balances )
        {
            BOOST_OUTCOME_TRY( StoreObservedBalance( address, balance ) );
        }

        return outcome::success();
    }

    outcome::result<std::optional<uint64_t>> MigrationAllowList::LoadObservedBalance( const std::string &address ) const
    {
        if ( !db_ )
        {
            return outcome::failure( storage::DatabaseError::UNITIALIZED );
        }

        base::Buffer key_buf;
        key_buf.put( BuildKey( migration_version_, address ) );
        auto value = db_->get( key_buf );
        if ( value.has_error() )
        {
            if ( value.error() == storage::DatabaseError::NOT_FOUND )
            {
                return std::optional<uint64_t>{};
            }
            return value.error();
        }

        BOOST_OUTCOME_TRY( auto balance, DecodeBalance( value.value() ) );
        return std::optional<uint64_t>{ balance };
    }

    outcome::result<bool> MigrationAllowList::IsEligible( const std::string &address, uint64_t claimed_balance ) const
    {
        BOOST_OUTCOME_TRY( auto maybe_balance, LoadObservedBalance( address ) );
        if ( !maybe_balance.has_value() )
        {
            return false;
        }

        const auto observed_balance = maybe_balance.value();
        const auto max_claim = observed_balance > ( std::numeric_limits<uint64_t>::max() / 2 )
                                   ? std::numeric_limits<uint64_t>::max()
                                   : observed_balance * 2;
        return claimed_balance <= max_claim;
    }

    outcome::result<std::vector<MigrationAllowList::AddressBalance>> MigrationAllowList::ListObservedBalances() const
    {
        if ( !db_ )
        {
            return outcome::failure( storage::DatabaseError::UNITIALIZED );
        }

        base::Buffer prefix_buf;
        prefix_buf.put( prefix_ );
        auto entries = db_->query( prefix_buf );
        if ( entries.has_error() )
        {
            if ( entries.error() == storage::DatabaseError::NOT_FOUND )
            {
                return std::vector<AddressBalance>{};
            }
            return entries.error();
        }

        std::vector<AddressBalance> balances;
        balances.reserve( entries.value().size() );
        for ( const auto &[key, value] : entries.value() )
        {
            const auto key_str = std::string( key.toString() );
            if ( key_str.size() <= prefix_.size() + 1 || key_str.compare( 0, prefix_.size(), prefix_ ) != 0 ||
                 key_str[prefix_.size()] != '/' )
            {
                logger_->warn( "Skipping malformed migration allowlist key {}", key_str );
                continue;
            }

            BOOST_OUTCOME_TRY( auto balance, DecodeBalance( value ) );
            balances.emplace_back( key_str.substr( prefix_.size() + 1 ), balance );
        }

        std::sort( balances.begin(),
                   balances.end(),
                   []( const AddressBalance &lhs, const AddressBalance &rhs ) { return lhs.first < rhs.first; } );

        return balances;
    }

    std::string MigrationAllowList::BuildPrefix( std::string_view migration_version )
    {
        return std::string( kPrefixBase ) + "/" + std::string( migration_version );
    }

    std::string MigrationAllowList::BuildKey( std::string_view migration_version, std::string_view address )
    {
        return BuildPrefix( migration_version ) + "/" + std::string( address );
    }

    outcome::result<uint64_t> MigrationAllowList::DecodeBalance( const base::Buffer &buffer )
    {
        if ( buffer.size() != sizeof( uint64_t ) )
        {
            return outcome::failure( std::errc::bad_message );
        }

        uint64_t value = 0;
        for ( size_t i = 0; i < sizeof( uint64_t ); ++i )
        {
            value = ( value << 8u ) | buffer[i];
        }
        return value;
    }
}
