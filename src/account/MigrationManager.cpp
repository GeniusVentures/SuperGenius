/**
 * @file       MigrationManager.cpp
 * @brief      Implementation of MigrationManager and Migration0_2_0To1_0_0.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/MigrationManager.hpp"

#include <boost/format.hpp>
#include <boost/system/error_code.hpp>
#include "account/TransactionManager.hpp"
#include "proof/IBasicProof.hpp"

namespace sgns
{
    void MigrationManager::RegisterStep( std::unique_ptr<IMigrationStep> step )
    {
        steps_.push_back( std::move( step ) );
        m_logger->debug( "Registered migration step from {} to {}",
                         steps_.back()->FromVersion(),
                         steps_.back()->ToVersion() );
    }

    outcome::result<void> MigrationManager::Migrate( const std::string &currentVersion,
                                                     const std::string &targetVersion )
    {
        std::string version = currentVersion;
        while ( version != targetVersion )
        {
            bool applied = false;
            for ( auto &step : steps_ )
            {
                if ( step->FromVersion() == version )
                {
                    m_logger->debug( "Applying migration step from {} to {}", version, step->ToVersion() );
                    OUTCOME_TRY( step->Apply() );
                    version = step->ToVersion();
                    applied = true;
                    break;
                }
            }
            if ( !applied )
            {
                m_logger->error( "No migration step found from version {}", version );
                return outcome::failure( boost::system::error_code{} );
            }
        }
        m_logger->debug( "Migration completed successfully from {} to {}", currentVersion, targetVersion );
        return outcome::success();
    }

    Migration0_2_0To1_0_0::Migration0_2_0To1_0_0(
        std::shared_ptr<crdt::GlobalDB>                                       newDb,
        std::shared_ptr<boost::asio::io_context>                              ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                            pubSub,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::protocol::Scheduler>                          scheduler,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        const std::string                                                    &writeBasePath,
        const std::string                                                    &base58key ) :
        newDb_( std::move( newDb ) ),
        ioContext_( std::move( ioContext ) ),
        pubSub_( std::move( pubSub ) ),
        graphsync_( std::move( graphsync ) ),
        scheduler_( std::move( scheduler ) ),
        generator_( std::move( generator ) ),
        writeBasePath_( writeBasePath ),
        base58key_( base58key )
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

    outcome::result<std::shared_ptr<crdt::GlobalDB>> Migration0_2_0To1_0_0::InitLegacyDb( const std::string &suffix )
    {
        static constexpr auto LEGACY_PREFIX_FMT = "/SuperGNUSNode.TestNet.2a.00.%1%";

        const auto legacyNetworkFullPath = ( boost::format( LEGACY_PREFIX_FMT ) % base58key_ ).str();

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
            return outcome::failure( boost::system::error_code{} );
        }
        auto &entries          = maybeTransactionKeys.value();
        auto  crdt_transaction = newDb->BeginTransaction();
        for ( const auto &entry : entries )
        {
            auto keyOpt = oldDb->KeyToString( entry.first );
            if ( !keyOpt.has_value() )
            {
                continue;
            }

            std::string transaction_key = keyOpt.value();

            auto maybe_transaction = TransactionManager::FetchTransaction( oldDb, transaction_key );
            if ( !maybe_transaction.has_value() )
            {
                m_logger->debug( "Can't fetch transaction" );
                continue;
            }
            auto tx = maybe_transaction.value();
            if ( !IGeniusTransactions::CheckDAGStructSignature( tx->dag_st ) )
            {
                m_logger->error( "Could not validate signature of transaction {}", transaction_key );
                continue;
            }
            // Until here it's supposed to work.
            std::string proof_key;
            if ( transaction_key.find( "notify" ) != std::string::npos )
            {
                auto maybeProofKeyMap = oldDb->QueryKeyValues( BASE, "*", "/proof/" + tx->dag_st.data_hash() );
                if ( !maybeProofKeyMap.has_value() )
                {
                    m_logger->error( "Can't find the proof key {}", transaction_key );
                    continue;
                }
                auto proof_map = maybeProofKeyMap.value();
                if ( proof_map.size() != 1 )
                {
                    m_logger->error( "More than 1 proof for {}", transaction_key );
                    continue;
                }
                auto proof_key_buffer = proof_map.begin()->first;
                auto maybe_proof_key = oldDb->KeyToString( proof_key_buffer );
                if ( !maybe_proof_key.has_value() )
                {
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
                m_logger->error( "Can't find the proof data of {}", keyOpt.value() );
                continue;
            }
            auto proof_data_vector = maybe_proof_data.value().toVector();
            auto maybe_valid_proof = IBasicProof::VerifyFullProof( proof_data_vector );
            if ( maybe_valid_proof.has_error() || ( !maybe_valid_proof.value() ) )
            {
                m_logger->error( "Could not verify proof of tx {}", keyOpt.value() );
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
            return outcome::failure( boost::system::error_code{} );
        }
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
