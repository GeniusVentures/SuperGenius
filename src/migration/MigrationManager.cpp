/**
 * @file       MigrationManager.cpp
 * @brief      Implementation of MigrationManager and migration steps.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "MigrationManager.hpp"
#include "Migration0_2_0To1_0_0.hpp"
#include "Migration1_0_0To3_4_0.hpp"
#include "Migration3_4_0To3_5_0.hpp"
#include "Migration3_5_0To3_6_0.hpp"
#include "Migration3_6_0To3_7_0.hpp"

#include <boost/format.hpp>
#include <boost/system/error_code.hpp>
#include "base/sgns_version.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, MigrationManager::Error, e )
{
    using Err = sgns::MigrationManager::Error;
    switch ( e )
    {
        case Err::BLOCKCHAIN_INIT_FAILED:
            return "Blockchain initialization failed during migration";
    }
    return "Unknown error";
}

namespace sgns
{

    std::shared_ptr<MigrationManager> MigrationManager::New(
        std::shared_ptr<boost::asio::io_context>                        ioContext,
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubSub,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync,
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler,
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::string                                                     writeBasePath,
        std::string                                                     base58key,
        std::shared_ptr<GeniusAccount>                                  account,
        bool                                                            is_full_node )
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
        instance->RegisterStep( std::make_shared<Migration3_5_0To3_6_0>( ioContext,
                                                                         pubSub,
                                                                         graphsync,
                                                                         scheduler,
                                                                         generator,
                                                                         writeBasePath,
                                                                         base58key ) );
        instance->RegisterStep( std::make_shared<Migration3_6_0To3_7_0>( ioContext,
                                                                         pubSub,
                                                                         graphsync,
                                                                         scheduler,
                                                                         generator,
                                                                         writeBasePath,
                                                                         base58key,
                                                                         account,
                                                                         is_full_node ) );
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
        total_steps_ = steps_.size();
        current_step_index_.store( 0 );

        size_t index = 0;
        for ( auto &step : steps_ )
        {
            current_step_index_.store( ++index ); // 1-based, signals which step we are working on

            m_logger->debug( "Starting migration step from {} to {}", step->FromVersion(), step->ToVersion() );

            BOOST_OUTCOME_TRY( step->Init() );

            BOOST_OUTCOME_TRY( bool is_req, step->IsRequired() );

            if ( is_req )
            {
                BOOST_OUTCOME_TRY( step->Apply() );
                m_logger->debug( "Completed migration step to {}", step->ToVersion() );
            }
            else
            {
                m_logger->debug( "Skipping migration step from {} to {}", step->FromVersion(), step->ToVersion() );
            }
            BOOST_OUTCOME_TRY( step->ShutDown() );
        }

        const auto currentVersion = ( boost::format( "%d.%d.%d" ) % version::SuperGeniusVersionMajor() %
                                      version::SuperGeniusVersionMinor() % version::SuperGeniusVersionPatch() )
                                        .str();

        m_logger->debug( "Current SuperGenius version: {}", currentVersion );
        m_logger->debug( "All migration steps completed" );

        return outcome::success();
    }

    std::string MigrationManager::GetCurrentStepDescription() const
    {
        auto current = current_step_index_.load();
        if ( current == 0 || total_steps_ == 0 )
        {
            return "Preparing migration";
        }
        // current is 1-based; steps_ uses 0-based indexing
        auto &step = steps_[current - 1];
        return ( boost::format( "Migrating database (step %1% of %2%): v%3% -> v%4%" ) % current % total_steps_ %
                 step->FromVersion() % step->ToVersion() )
            .str();
    }

} // namespace sgns
