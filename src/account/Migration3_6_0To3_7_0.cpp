#include "Migration3_6_0To3_7_0.hpp"

#include "account/MigrationAllowList.hpp"
#include "account/MigrationManager.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "base/sgns_version.hpp"
#include "storage/database_error.hpp"

#include <algorithm>
#include <filesystem>

namespace sgns
{
    namespace
    {
        constexpr std::string_view kLegacyUTXOPrefix = "/utxo/";

        std::optional<std::string> ParseLegacyUTXOOwnerAddress( std::string_view key )
        {
            if ( key.substr( 0, kLegacyUTXOPrefix.size() ) != kLegacyUTXOPrefix )
            {
                return std::nullopt;
            }

            const auto address = key.substr( kLegacyUTXOPrefix.size() );
            if ( address.empty() || address.find( '/' ) != std::string_view::npos )
            {
                return std::nullopt;
            }

            return std::string( address );
        }
    }

    Migration3_6_0To3_7_0::Migration3_6_0To3_7_0(
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::string                                                     writeBasePath,
        std::string                                                     base58key ) :
        ioContext_( std::move( ioContext ) ),
        pubSub_( std::move( pubSub ) ),
        graphsync_( std::move( graphsync ) ),
        scheduler_( std::move( scheduler ) ),
        generator_( std::move( generator ) ),
        writeBasePath_( std::move( writeBasePath ) ),
        base58key_( std::move( base58key ) )
    {
    }

    std::string Migration3_6_0To3_7_0::FromVersion() const
    {
        return "3.6.0";
    }

    std::string Migration3_6_0To3_7_0::ToVersion() const
    {
        return "3.7.0";
    }

    outcome::result<bool> Migration3_6_0To3_7_0::IsRequired() const
    {
        if ( !db_3_6_0_ )
        {
            logger_->info( "Legacy {} DB not found; skipping migration to {}", FromVersion(), ToVersion() );
            return false;
        }

        if ( !db_3_7_0_ )
        {
            logger_->warn( "Target {} DB not initialized yet", ToVersion() );
            return false;
        }

        crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        auto version_ret = db_3_7_0_->GetDataStore()->get( version_key );

        if ( version_ret.has_error() )
        {
            logger_->info( "No version info found in GlobalDB, migration from {} to {} is required",
                           FromVersion(),
                           ToVersion() );
            return true;
        }

        const auto version_buffer = version_ret.value();
        if ( !IsVersionLessThan( std::string( version_buffer.toString() ), ToVersion() ) )
        {
            logger_->info( "GlobalDB already at target version {}, skipping migration", ToVersion() );
            return false;
        }

        logger_->info( "GlobalDB at version {}, need to migrate to {}", version_buffer.toString(), ToVersion() );
        return true;
    }

    outcome::result<void> Migration3_6_0To3_7_0::Init()
    {
        BOOST_OUTCOME_TRY( auto legacy_db, InitLegacyDb() );
        db_3_6_0_ = std::move( legacy_db );
        if ( db_3_6_0_ )
        {
            BOOST_OUTCOME_TRY( auto new_db, InitTargetDb() );
            db_3_7_0_ = std::move( new_db );
        }
        return outcome::success();
    }

    outcome::result<void> Migration3_6_0To3_7_0::Apply()
    {
        if ( !db_3_6_0_ )
        {
            logger_->error( "Legacy {} DB not initialized; nothing to migrate to {}", FromVersion(), ToVersion() );
            return outcome::success();
        }
        if ( !db_3_7_0_ )
        {
            logger_->error( "Target {} DB not initialized", ToVersion() );
            return outcome::failure( std::errc::no_such_device );
        }

        BOOST_OUTCOME_TRY( auto balances, ComputeLegacyBalances() );
        MigrationAllowList allow_list( db_3_7_0_->GetDataStore(), ToVersion() );
        BOOST_OUTCOME_TRY( allow_list.StoreObservedBalances( balances ) );
        logger_->info( "Computed {} legacy address balances for {} -> {} migration",
                       balances.size(),
                       FromVersion(),
                       ToVersion() );

        return outcome::success();
    }

    outcome::result<void> Migration3_6_0To3_7_0::ShutDown()
    {
        db_3_6_0_.reset();
        db_3_7_0_.reset();

        return outcome::success();
    }

    outcome::result<std::vector<Migration3_6_0To3_7_0::AddressBalance>> Migration3_6_0To3_7_0::ComputeLegacyBalances()
        const
    {
        if ( !db_3_6_0_ )
        {
            logger_->error( "Legacy {} DB not initialized", FromVersion() );
            return std::errc::state_not_recoverable;
        }

        crdt::GlobalDB::Buffer key_buf;
        key_buf.put( std::string( kLegacyUTXOPrefix.substr( 0, kLegacyUTXOPrefix.size() - 1 ) ) );
        auto utxo_list = db_3_6_0_->GetDataStore()->query( key_buf );
        if ( utxo_list.has_error() )
        {
            if ( utxo_list.error() == storage::DatabaseError::NOT_FOUND )
            {
                return std::vector<AddressBalance>{};
            }
            logger_->error( "Failed to query legacy UTXOs: {}", utxo_list.error().message() );
            return utxo_list.error();
        }

        std::vector<AddressBalance> balances;
        balances.reserve( utxo_list.value().size() );

        for ( const auto &[key, value] : utxo_list.value() )
        {
            auto address_opt = ParseLegacyUTXOOwnerAddress( key.toString() );
            if ( !address_opt.has_value() )
            {
                logger_->debug( "Skipping non-legacy UTXO key {}", key.toString() );
                continue;
            }

            SGTransaction::UTXOList utxos;
            if ( !utxos.ParseFromArray( value.data(), value.size() ) )
            {
                logger_->error( "Failed to deserialize legacy UTXOs for address {}", address_opt.value() );
                return std::errc::bad_message;
            }

            uint64_t balance = 0;
            for ( int i = 0; i < utxos.utxos_size(); ++i )
            {
                balance += utxos.utxos( i ).amount();
            }

            balances.emplace_back( std::move( address_opt.value() ), balance );
        }

        std::sort( balances.begin(),
                   balances.end(),
                   []( const AddressBalance &lhs, const AddressBalance &rhs ) { return lhs.first < rhs.first; } );

        return balances;
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_6_0To3_7_0::InitLegacyDb() const
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_6_0 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_6_0 ) +
                         version::GetNetAndVersionAppendix( 3, 6, version::GetNetworkID() ) + base58key_;

        if ( !std::filesystem::exists( full_path ) )
        {
            logger_->info( "Legacy {} DB not found at {}; skipping initialization", FromVersion(), full_path );
            return std::shared_ptr<crdt::GlobalDB>{};
        }

        logger_->debug( "Initializing legacy {} DB at path {}", FromVersion(), full_path );

        auto maybe_db_3_6_0 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_6_0.has_value() )
        {
            logger_->error( "Legacy {} DB error at path {}", FromVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started legacy {} DB at path {}", FromVersion(), full_path );
        return std::move( maybe_db_3_6_0.value() );
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration3_6_0To3_7_0::InitTargetDb() const
    {
        static constexpr std::string_view GNUS_NETWORK_PATH_3_7_0 = "SuperGNUSNode.Node";

        auto full_path = writeBasePath_ + std::string( GNUS_NETWORK_PATH_3_7_0 ) +
                         version::GetNetAndVersionAppendix( 3, 7, version::GetNetworkID() ) + base58key_;

        logger_->debug( "Initializing target {} DB at path {}", ToVersion(), full_path );

        auto maybe_db_3_7_0 = crdt::GlobalDB::New( ioContext_,
                                                   full_path,
                                                   pubSub_,
                                                   crdt::CrdtOptions::DefaultOptions(),
                                                   graphsync_,
                                                   scheduler_,
                                                   generator_ );

        if ( !maybe_db_3_7_0.has_value() )
        {
            logger_->error( "Target {} DB error at path {}", ToVersion(), full_path );
            return outcome::failure( boost::system::error_code{} );
        }

        logger_->debug( "Started target {} DB at path {}", ToVersion(), full_path );
        return std::move( maybe_db_3_7_0.value() );
    }
}
