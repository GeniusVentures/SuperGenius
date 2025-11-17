/**
 * @file       MigrationManager.cpp
 * @brief      Implementation of MigrationManager and migration steps.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "account/MigrationManager.hpp"
#include "account/Migration0_2_0To1_0_0.hpp"
#include "account/Migration1_0_0To3_4_0.hpp"
#include "account/Migration3_4_0To3_5_0.hpp"

#include <boost/format.hpp>
#include <boost/system/error_code.hpp>
#include "base/sgns_version.hpp"

namespace sgns
{

    std::shared_ptr<MigrationManager> MigrationManager::New(
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::protocol::Scheduler>                    scheduler,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::string                                                     writeBasePath,
        std::string                                                     base58key,
        std::shared_ptr<GeniusAccount>                                  account )
    {
        auto instance = std::shared_ptr<MigrationManager>( new MigrationManager() );
        instance->RegisterStep( std::make_shared<Migration0_2_0To1_0_0>( ioContext,
                                                                         pubSub,
                                                                         graphsync,
                                                                         scheduler,
                                                                         generator,
                                                                         writeBasePath,
                                                                         base58key ) );
        instance->RegisterStep( std::make_shared<Migration1_0_0To3_4_0>( ioContext,
                                                                         pubSub,
                                                                         graphsync,
                                                                         scheduler,
                                                                         generator,
                                                                         writeBasePath,
                                                                         base58key ) );
        instance->RegisterStep( std::make_shared<Migration3_4_0To3_5_0>( ioContext,
                                                                         pubSub,
                                                                         graphsync,
                                                                         scheduler,
                                                                         generator,
                                                                         writeBasePath,
                                                                         base58key,
                                                                         account ) );
        return instance;
    }

    MigrationManager::MigrationManager() = default;

    void MigrationManager::RegisterStep( std::shared_ptr<IMigrationStep> step )
    {
        steps_.push_back( std::move( step ) );
        m_logger->debug( "Registered migration step from {} to {}",
                         steps_.back()->FromVersion(),
                         steps_.back()->ToVersion() );
    }

    outcome::result<void> MigrationManager::Migrate()
    {
        for ( auto &step : steps_ )
        {
            m_logger->debug( "Starting migration step from {} to {}", step->FromVersion(), step->ToVersion() );

            OUTCOME_TRY( step->Init() );

            OUTCOME_TRY( bool is_req, step->IsRequired() );

            if ( is_req )
            {
                OUTCOME_TRY( step->Apply() );
                m_logger->debug( "Completed migration step to {}", step->ToVersion() );
            }
            else
            {
                m_logger->debug( "Skipping migration step from {} to {}", step->FromVersion(), step->ToVersion() );
            }
            OUTCOME_TRY( step->ShutDown() );
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        }

        const auto currentVersion = ( boost::format( "%d.%d.%d" ) % version::SuperGeniusVersionMajor() %
                                      version::SuperGeniusVersionMinor() % version::SuperGeniusVersionPatch() )
                                        .str();

        m_logger->debug( "Current SuperGenius version: {}", currentVersion );
        m_logger->debug( "All migration steps completed" );

        return outcome::success();
    }

} // namespace sgns
