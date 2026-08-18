#include "Migration3_6_0To3_7_0.hpp"

#include "MigrationManager.hpp"
#include "account/MigrationAllowList.hpp"
#include "account/MintTransaction.hpp"
#include "account/TransactionManager.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "base/sgns_version.hpp"
#include "blockchain/Blockchain.hpp"
#include "storage/database_error.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace sgns
{
    namespace
    {
        constexpr std::string_view kLegacyUTXOPrefix = "/utxo/";

        struct LegacyProducedOutput
        {
            std::string owner_address;
            uint64_t    amount;
        };

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

        std::string MakeLegacyOutPointKey( std::string_view tx_hash, uint32_t output_idx )
        {
            return std::string( tx_hash ) + ":" + std::to_string( output_idx );
        }

        bool IsMigratableBalanceAddress( std::string_view address )
        {
            return utxo_address::IsAccountPublicKeyAddress( address );
        }
    }

    Migration3_6_0To3_7_0::Migration3_6_0To3_7_0(
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::string                                                     writeBasePath,
        std::string                                                     base58key,
        std::shared_ptr<GeniusAccount>                                  account,
        NodeType                                                        node_type ) :
        ioContext_( std::move( ioContext ) ),
        pubSub_( std::move( pubSub ) ),
        graphsync_( std::move( graphsync ) ),
        scheduler_( std::move( scheduler ) ),
        generator_( std::move( generator ) ),
        writeBasePath_( std::move( writeBasePath ) ),
        base58key_( std::move( base58key ) ),
        account_( std::move( account ) ),
        node_type_( node_type )
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

        logger_->info( "Starting migration from {} to {}", FromVersion(), ToVersion() );

        account_->ConfigureDatabaseDependencies( db_3_7_0_ );
        logger_->debug( "{}: Configured account database dependencies for {}", __func__, ToVersion() );

        BOOST_OUTCOME_TRY( Blockchain::MigrateCids( db_3_6_0_, db_3_7_0_ ) );
        logger_->debug( "{}: Migrated blockchain CIDs from {} to {}", __func__, FromVersion(), ToVersion() );
        db_3_7_0_->StartCICSync();
        logger_->debug( "{}: Started CID processing for target {}", __func__, ToVersion() );

        if ( !blockchain_ )
        {
            logger_->debug( "{}: Creating blockchain for target {}", __func__, ToVersion() );
            blockchain_ = Blockchain::New( db_3_7_0_,
                                           account_,
                                           pubSub_,
                                           [wptr( weak_from_this() )]( outcome::result<void> result )
                                           {
                                               if ( auto strong = wptr.lock() )
                                               {
                                                   if ( result.has_error() )
                                                   {
                                                       strong->logger_->error( "Error starting blockchain: {}",
                                                                               result.error().message() );
                                                       strong->blockchain_status_.store( Status::ST_ERROR );
                                                       return;
                                                   }
                                                   strong->blockchain_status_.store( Status::ST_SUCCESS );
                                               }
                                           },
                                           node_type_ );
        }
        blockchain_status_.store( Status::ST_INIT, std::memory_order_release );
        logger_->debug( "{}: Starting blockchain bootstrap for {}", __func__, ToVersion() );

        auto                  retry_duration   = std::chrono::minutes( 2 );
        auto                  retry_interval   = std::chrono::seconds( 5 );
        auto                  retry_start_time = std::chrono::steady_clock::now();
        auto                  last_log_time    = retry_start_time;
        outcome::result<void> start_result     = outcome::failure( Blockchain::Error::BLOCKCHAIN_NOT_INITIALIZED );
        do
        {
            start_result = blockchain_->Start();
            if ( start_result.has_error() )
            {
                logger_->error( "Error starting blockchain: {}", start_result.error().message() );
            }

            const auto current_time = std::chrono::steady_clock::now();
            if ( current_time - last_log_time >= std::chrono::seconds( 30 ) )
            {
                const auto elapsed_seconds =
                    std::chrono::duration_cast<std::chrono::seconds>( current_time - retry_start_time ).count();
                logger_->info( "{}: Retrying blockchain start (elapsed: {}s)", __func__, elapsed_seconds );
                last_log_time = current_time;
            }
            std::this_thread::sleep_for( retry_interval );
        } while ( std::chrono::steady_clock::now() - retry_start_time < retry_duration && start_result.has_error() );

        const auto timeout_duration = std::chrono::minutes( 4 );
        auto       start_time       = std::chrono::steady_clock::now();
        last_log_time               = start_time;
        bool blockchain_succeeded   = false;

        while ( std::chrono::steady_clock::now() - start_time < timeout_duration )
        {
            const auto current_time = std::chrono::steady_clock::now();
            if ( blockchain_status_.load( std::memory_order_acquire ) != Status::ST_INIT )
            {
                if ( blockchain_status_.load( std::memory_order_acquire ) == Status::ST_SUCCESS )
                {
                    blockchain_succeeded = true;
                }
                break;
            }
            if ( current_time - last_log_time >= std::chrono::seconds( 30 ) )
            {
                const auto elapsed_seconds =
                    std::chrono::duration_cast<std::chrono::seconds>( current_time - start_time ).count();
                logger_->info( "{}: Still waiting for the blockchain to initialize (elapsed: {}s)",
                               __func__,
                               elapsed_seconds );
                last_log_time = current_time;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        if ( !blockchain_succeeded )
        {
            return outcome::failure( MigrationManager::Error::BLOCKCHAIN_INIT_FAILED );
        }

        start_time           = std::chrono::steady_clock::now();
        blockchain_succeeded = false;
        while ( std::chrono::steady_clock::now() - start_time < timeout_duration )
        {
            auto genesis_cid_result          = blockchain_->GetGenesisCID();
            auto account_creation_cid_result = blockchain_->GetAccountCreationCID();
            if ( genesis_cid_result.has_value() && account_creation_cid_result.has_value() &&
                 blockchain_->validator_registry_initialized_.load( std::memory_order_acquire ) )
            {
                blockchain_succeeded = true;
                break;
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        if ( !blockchain_succeeded )
        {
            logger_->error( "{}: Genesis, Account Creation and/or Validator Registry not initialized", __func__ );
            return outcome::failure( MigrationManager::Error::BLOCKCHAIN_INIT_FAILED );
        }
        logger_->debug( "{}: Blockchain bootstrap complete for {}", __func__, ToVersion() );

        BOOST_OUTCOME_TRY( auto balances, ComputeLegacyBalances() );
        MigrationAllowList allow_list( db_3_7_0_->GetDataStore(), FromVersion() );
        BOOST_OUTCOME_TRY( allow_list.StoreObservedBalances( balances ) );
        logger_->info( "Computed {} legacy address balances for {} -> {} migration",
                       balances.size(),
                       FromVersion(),
                       ToVersion() );

        BOOST_OUTCOME_TRY( auto observed_balance, allow_list.LoadObservedBalance( account_->GetAddress() ) );
        logger_->debug( "{}: Observed balance lookup for {} returned has_value={} balance={}",
                        __func__,
                        account_->GetAddress(),
                        observed_balance.has_value(),
                        observed_balance.has_value() ? observed_balance.value() : 0 );

        if ( observed_balance.has_value() && observed_balance.value() > 0 )
        {
            logger_->debug( "{}: Starting CID receiving for target {}", __func__, ToVersion() );
            db_3_7_0_->StartCIDReceiving();
            logger_->debug( "{}: Starting head rebroadcast for target {}", __func__, ToVersion() );
            db_3_7_0_->StartRebroadcastHeads();

            if ( !transaction_manager_ )
            {
                logger_->debug( "{}: Creating transaction manager for migration flow", __func__ );
                transaction_manager_ = TransactionManager::New( db_3_7_0_,
                                                                ioContext_,
                                                                account_,
                                                                blockchain_,
                                                                node_type_ );
            }

            logger_->debug( "{}: Registering transaction topic names for migration flow", __func__ );
            transaction_manager_->RegisterTopicNames();
            logger_->debug( "{}: Starting transaction manager core for migration flow", __func__ );
            transaction_manager_->StartCore();

            start_time = std::chrono::steady_clock::now();
            while ( std::chrono::steady_clock::now() - start_time < timeout_duration )
            {
                if ( transaction_manager_->GetState() == TransactionManager::State::READY )
                {
                    break;
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            }
            if ( transaction_manager_->GetState() != TransactionManager::State::READY )
            {
                logger_->error( "{}: Transaction Manager did not reach READY", __func__ );
                return outcome::failure( MigrationManager::Error::BLOCKCHAIN_INIT_FAILED );
            }
            logger_->debug( "{}: Transaction Manager is READY for migration flow", __func__ );

            const auto token_id = TokenID::FromBytes( { 0x00 } );
            logger_->info( "{}: Submitting migration transaction for {:.8} amount={}",
                           __func__,
                           account_->GetAddress(),
                           observed_balance.value() );
            BOOST_OUTCOME_TRY( auto tx_hash,
                               transaction_manager_->MigrationFunds( observed_balance.value(),
                                                                     FromVersion(),
                                                                     token_id,
                                                                     account_->GetAddress() ) );
            logger_->debug( "{}: Waiting for migration transaction confirmation tx={}", __func__, tx_hash );

            auto tx_status = transaction_manager_->WaitForTransactionOutgoing( tx_hash, std::chrono::minutes( 4 ) );
            if ( tx_status != TransactionManager::TransactionStatus::CONFIRMED )
            {
                logger_->error( "{}: Migration transaction {} did not confirm. status={}",
                                __func__,
                                tx_hash,
                                static_cast<int>( tx_status ) );
                return outcome::failure( std::errc::timed_out );
            }
        }
        else
        {
            logger_->info( "{}: Local account has no observed legacy balance; skipping migration claim", __func__ );
        }

        crdt::GlobalDB::Buffer version_key;
        version_key.put( std::string( MigrationManager::VERSION_INFO_KEY ) );
        crdt::GlobalDB::Buffer version_value;
        version_value.put( ToVersion() );
        BOOST_OUTCOME_TRY( db_3_7_0_->GetDataStore()->put( version_key, version_value ) );
        logger_->info( "Migration from {} to {} completed successfully", FromVersion(), ToVersion() );

        return outcome::success();
    }

    outcome::result<void> Migration3_6_0To3_7_0::ShutDown()
    {
        if ( transaction_manager_ )
        {
            transaction_manager_->Stop();
            transaction_manager_.reset();
        }
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
            blockchain_.reset();
        }
        if ( account_ )
        {
            account_->DeconfigureDatabaseDependencies();
            account_->GetUTXOManager().ReleaseStorage();
        }
        blockchain_status_.store( Status::ST_INIT, std::memory_order_release );
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

            if ( IsMigratableBalanceAddress( address_opt.value() ) )
            {
                balances.emplace_back( std::move( address_opt.value() ), balance );
            }
        }

        if ( !balances.empty() )
        {
            std::sort( balances.begin(),
                       balances.end(),
                       []( const AddressBalance &lhs, const AddressBalance &rhs ) { return lhs.first < rhs.first; } );
            return balances;
        }

        logger_->info( "No legacy UTXO snapshots found in {}; reconstructing balances from migrated transactions",
                       FromVersion() );

        std::unordered_map<std::string, LegacyProducedOutput> produced_outputs;
        std::unordered_set<std::string>                       consumed_outpoints;
        size_t                                                scanned_transactions = 0;

        for ( auto network_id : TransactionManager::GetMonitoredNetworkIDs() )
        {
            const std::string query_path = TransactionManager::GetBlockChainBase( network_id ) + "tx";
            BOOST_OUTCOME_TRY( auto transaction_list, db_3_6_0_->QueryKeyValues( query_path ) );

            for ( const auto &[key, value] : transaction_list )
            {
                ++scanned_transactions;

                BOOST_OUTCOME_TRY( auto tx, TransactionManager::DeSerializeTransaction( value ) );
                if ( tx->GetHash().empty() )
                {
                    tx->FillHash();
                }

                const auto tx_hash = tx->GetHash();
                if ( tx_hash.empty() )
                {
                    logger_->error( "Failed to determine hash while reconstructing legacy balance from {}",
                                    key.toString() );
                    return std::errc::bad_message;
                }

                if ( auto params_opt = tx->GetUTXOParametersOpt() )
                {
                    const auto &[inputs, outputs] = params_opt.value();
                    for ( const auto &input : inputs )
                    {
                        consumed_outpoints.emplace(
                            MakeLegacyOutPointKey( input.txid_hash_.toReadableString(), input.output_idx_ ) );
                    }

                    for ( std::uint32_t i = 0; i < outputs.size(); ++i )
                    {
                        produced_outputs[MakeLegacyOutPointKey( tx_hash, i )] = LegacyProducedOutput{
                            outputs[i].dest_address,
                            outputs[i].encrypted_amount };
                    }

                    continue;
                }

                if ( auto mint_tx = std::dynamic_pointer_cast<MintTransaction>( tx ) )
                {
                    produced_outputs[MakeLegacyOutPointKey( tx_hash, 0 )] = LegacyProducedOutput{
                        mint_tx->GetSrcAddress(),
                        mint_tx->GetAmount() };
                }
            }
        }

        std::unordered_map<std::string, uint64_t> balance_by_address;
        for ( const auto &[outpoint_key, output] : produced_outputs )
        {
            if ( consumed_outpoints.find( outpoint_key ) != consumed_outpoints.end() ||
                 !IsMigratableBalanceAddress( output.owner_address ) )
            {
                continue;
            }

            balance_by_address[output.owner_address] += output.amount;
        }

        balances.clear();
        balances.reserve( balance_by_address.size() );
        for ( auto &[address, balance] : balance_by_address )
        {
            balances.emplace_back( std::move( address ), balance );
        }

        std::sort( balances.begin(),
                   balances.end(),
                   []( const AddressBalance &lhs, const AddressBalance &rhs ) { return lhs.first < rhs.first; } );

        logger_->info( "Reconstructed {} legacy balances from {} transactions and {} produced outputs",
                       balances.size(),
                       scanned_transactions,
                       produced_outputs.size() );

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
