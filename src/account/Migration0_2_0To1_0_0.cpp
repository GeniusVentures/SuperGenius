/**
 * @file       Migration0_2_0To1_0_0.cpp
 * @brief      Implementation of MigrationManager and migration steps.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "account/Migration0_2_0To1_0_0.hpp"

#include <boost/format.hpp>
#include <boost/system/error_code.hpp>
#include "account/TransactionManager.hpp"
#include "proof/IBasicProof.hpp"

namespace sgns
{

    Migration0_2_0To1_0_0::Migration0_2_0To1_0_0(
        std::shared_ptr<crdt::GlobalDB>                                 newDb,
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::string                                                     writeBasePath,
        std::string                                                     base58key ) :
        newDb_( std::move( newDb ) ),
        ioContext_( std::move( ioContext ) ),
        pubSub_( std::move( pubSub ) ),
        graphsync_( std::move( graphsync ) ),
        scheduler_( std::move( scheduler ) ),
        generator_( std::move( generator ) ),
        writeBasePath_( std::move( writeBasePath ) ),
        base58key_( std::move( base58key ) )
    {
    }

    std::string Migration0_2_0To1_0_0::FromVersion() const
    {
        return "0.2.0";
    }

    std::string Migration0_2_0To1_0_0::ToVersion() const
    {
        return "1.0.0";
    }

    outcome::result<bool> Migration0_2_0To1_0_0::IsRequired() const
    {
        auto maybe_values = newDb_->QueryKeyValues( "" );
        if ( maybe_values.has_error() )
        {
            m_logger->debug( "Cannot query transactions: {}", maybe_values.error().message() );
            return outcome::failure( maybe_values.error() );
        }
        auto key_value_map = maybe_values.value();
        if ( !newDb_->GetDataStore()->empty() )
        {
            m_logger->debug( "newDb is not empty; skipping Migration0_2_0To1_0_0" );
            return false;
        }

        return true;
    }

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration0_2_0To1_0_0::InitLegacyDb( const std::string &suffix )
    {
        static constexpr auto LEGACY_PREFIX_FMT     = "/SuperGNUSNode.TestNet.2a.00.%1%";
        const auto            legacyNetworkFullPath = ( boost::format( LEGACY_PREFIX_FMT ) % base58key_ ).str();
        const auto fullPath = ( boost::format( "%s%s_%s" ) % writeBasePath_ % legacyNetworkFullPath % suffix ).str();

        m_logger->trace( "Initializing legacy DB at path {}", fullPath );

        OUTCOME_TRY( auto &&db,
                     crdt::GlobalDB::New( ioContext_,
                                          fullPath,
                                          pubSub_,
                                          crdt::CrdtOptions::DefaultOptions(),
                                          graphsync_,
                                          scheduler_,
                                          generator_ ) );
        db->Start();
        m_logger->trace( "Started legacy DB at path {}", fullPath );

        return db;
    }

    outcome::result<void> Migration0_2_0To1_0_0::MigrateDb( const std::shared_ptr<crdt::GlobalDB> &oldDb,
                                                            const std::shared_ptr<crdt::GlobalDB> &newDb )
    {
        // Outgoing transactions were /bc-963/[self address]/tx/[type]/[nonce]
        // Incoming transactions were /bc-963/[other address]/notify/tx/[tx hash]
        // Outgoing proofs were /bc-963/[self address]/proof/[nonce]
        // Incoming proofs were /bc-963/[other address]/notify/proof/[tx hash]

        const std::string BASE                 = "/bc-963/";
        auto              maybeTransactionKeys = oldDb->QueryKeyValues( BASE, "*", "/tx" );
        if ( !maybeTransactionKeys.has_value() )
        {
            m_logger->error( "Failed to query transaction keys with base {}", BASE );
            return outcome::failure( boost::system::error_code{} );
        }

        auto &entries = maybeTransactionKeys.value();
        m_logger->trace( "Found {} transaction keys to migrate", entries.size() );

        auto crdt_transaction = newDb->BeginTransaction();

        for ( const auto &entry : entries )
        {
            auto keyOpt = oldDb->KeyToString( entry.first );
            if ( !keyOpt.has_value() )
            {
                m_logger->error( "Failed to convert key buffer to string" );
                continue;
            }

            std::string transaction_key   = keyOpt.value();
            auto        maybe_transaction = TransactionManager::FetchTransaction( oldDb, transaction_key );
            if ( !maybe_transaction.has_value() )
            {
                m_logger->error( "Can't fetch transaction for key {}", transaction_key );
                continue;
            }
            auto tx = maybe_transaction.value();

            if ( !IGeniusTransactions::CheckDAGStructSignature( tx->dag_st ) )
            {
                m_logger->error( "Could not validate signature of transaction {}", transaction_key );
                continue;
            }

            std::string proof_key;
            if ( transaction_key.find( "notify" ) != std::string::npos )
            {
                auto maybeProofKeyMap = oldDb->QueryKeyValues( BASE, "*", "/proof/" + tx->dag_st.data_hash() );
                if ( !maybeProofKeyMap.has_value() )
                {
                    m_logger->error( "Can't find the proof key for incoming transaction {}", transaction_key );
                    continue;
                }
                auto proof_map = maybeProofKeyMap.value();
                if ( proof_map.size() != 1 )
                {
                    m_logger->error( "More than 1 proof for incoming transaction {}", transaction_key );
                    continue;
                }
                auto proof_key_buffer = proof_map.begin()->first;
                auto maybe_proof_key  = oldDb->KeyToString( proof_key_buffer );
                if ( !maybe_proof_key.has_value() )
                {
                    m_logger->error( "Failed to convert proof key buffer to string for transaction {}",
                                     transaction_key );
                    continue;
                }
                proof_key = maybe_proof_key.value();
            }
            else
            {
                proof_key = BASE + tx->GetSrcAddress() + "/proof/" + std::to_string( tx->dag_st.nonce() );
            }

            auto maybe_proof_data = oldDb->Get( proof_key );
            if ( !maybe_proof_data.has_value() )
            {
                m_logger->error( "Can't find the proof data for {}", transaction_key );
                continue;
            }

            auto                         transaction_path = TransactionManager::GetTransactionPath( *tx );
            sgns::crdt::HierarchicalKey  tx_key( transaction_path );
            sgns::crdt::GlobalDB::Buffer data_transaction;
            data_transaction.put( tx->SerializeByteVector() );
            BOOST_OUTCOME_TRYV2( auto &&, crdt_transaction->Put( std::move( tx_key ), std::move( data_transaction ) ) );

            sgns::crdt::HierarchicalKey  proof_crdt_key( TransactionManager::GetTransactionProofPath( *tx ) );
            sgns::crdt::GlobalDB::Buffer proof_transaction;
            proof_transaction.put( maybe_proof_data.value() );
            BOOST_OUTCOME_TRYV2( auto &&,
                                 crdt_transaction->Put( std::move( proof_crdt_key ), std::move( proof_transaction ) ) );
        }

        if ( crdt_transaction->Commit().has_error() )
        {
            m_logger->error( "Failed to commit transaction batch to new DB" );
            return outcome::failure( boost::system::error_code{} );
        }

        m_logger->trace( "Successfully committed all migrated entries to new DB" );
        return outcome::success();
    }

    outcome::result<void> Migration0_2_0To1_0_0::Apply()
    {
        m_logger->trace( "Starting Apply step of Migration0_2_0To1_0_0" );

        OUTCOME_TRY( auto outDb, InitLegacyDb( "out" ) );
        OUTCOME_TRY( auto inDb, InitLegacyDb( "in" ) );

        m_logger->trace( "Migrating output DB into new DB" );
        OUTCOME_TRY( MigrateDb( outDb, newDb_ ) );

        m_logger->trace( "Migrating input DB into new DB" );
        OUTCOME_TRY( MigrateDb( inDb, newDb_ ) );

        m_logger->trace( "Apply step of Migration0_2_0To1_0_0 finished successfully" );
        return outcome::success();
    }

} // namespace sgns
