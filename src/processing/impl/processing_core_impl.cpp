#include "processing/impl/processing_core_impl.hpp"

#include <rapidjson/document.h>

#include <boost/di/extension/scopes/shared.hpp>
#include <libp2p/security/noise.hpp>

#include "base/logger.hpp"
#include "FileManager.hpp"
#include <processingbase/ProcessingManager.hpp>
#include <Generators.hpp>

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::processing, ProcessingCoreImpl::Error, e )
{
    using E = sgns::processing::ProcessingCoreImpl::Error;
    switch ( e )
    {
        case E::MAX_NUMBER_SUBTASKS:
            return "Maximal number of processed subtasks exceeded";
        case E::GLOBALDB_READ_ERROR:
            return "GlobaDB Read error ";
        case E::NO_BUFFER_FROM_JOB_DATA:
            return "No buffer from job data";
        case E::TASK_DESERIALIZATION_ERROR:
            return "Task deserialization error";
        case E::JOB_INCOMPATIBILITY_ERROR:
            return "Job incompatibility error";
        case E::INVALID_MODEL_ERROR:
            return "Invalid model error";
        case E::PNET_INITIALIZATION_ERROR:
            return "Private-network initialization error";
    }
    return "Unknown error";
}

namespace sgns::processing
{

    namespace
    {
        base::Logger ProcessingCoreLogger()
        {
            return base::createLogger( "ProcessingCoreImpl" );
        }

        /**
         * @brief   Materializes the eagerly-needed io_context and exposes the gated
         *          Host lazily from one injector composition (both branch compositions
         *          have distinct injector types, so the sink is a template - the same
         *          structure the vendored GossipPubSub::InitHostFromInjector uses).
         *          The move-only injector is held via shared_ptr so the copyable
         *          std::function closure can keep it (and its shared singletons) alive;
         *          the Host later created from it shares this io_context.
         */
        template <typename InjectorT>
        ProcessingCoreImpl::GatedHostContext MakeContextFromInjector( InjectorT &&injector )
        {
            auto held = std::make_shared<std::decay_t<InjectorT>>( std::move( injector ) );
            ProcessingCoreImpl::GatedHostContext context;
            context.io_context = held->template create<std::shared_ptr<boost::asio::io_context>>();
            context.make_host  = [held]() -> std::shared_ptr<libp2p::Host> {
                return held->template create<std::shared_ptr<libp2p::Host>>();
            };
            return context;
        }
    } // namespace

    ProcessingCoreImpl::GatedHostContext ProcessingCoreImpl::MakeGatedHostInjector(
        const std::string                                               &network_key,
        const std::shared_ptr<sgns::ipfs_pubsub::DenyListConnectionGater> &gater,
        libp2p::protocol::kademlia::Config                                kademlia_config )
    {
        namespace di = boost::di;
        using namespace libp2p;

        // Identical binding set the gossip host applies (MakeCustomHostInjector):
        // Noise-only security - the unauthenticated transport adaptor is never
        // offered in any mode (D-11) - plus the connection gater.
        // usePrivateNetwork validates the key EAGERLY and throws PskValidationError
        // (a std::exception) on invalid key material before anything assembles.
        if ( network_key.empty() )
        {
            auto injector = libp2p::injector::makeHostInjector<di::extension::shared_config>(
                libp2p::injector::makeKademliaInjector<di::extension::shared_config>(
                    libp2p::injector::useKademliaConfig( std::move( kademlia_config ) ) ),
                libp2p::injector::useSecurityAdaptors<libp2p::security::Noise>(),
                di::bind<libp2p::network::ConnectionGater>().TEMPLATE_TO( gater )[di::override] );
            return MakeContextFromInjector( std::move( injector ) );
        }

        auto injector = libp2p::injector::makeHostInjector<di::extension::shared_config>(
            libp2p::injector::makeKademliaInjector<di::extension::shared_config>(
                libp2p::injector::useKademliaConfig( std::move( kademlia_config ) ) ),
            libp2p::injector::useSecurityAdaptors<libp2p::security::Noise>(),
            di::bind<libp2p::network::ConnectionGater>().TEMPLATE_TO( gater )[di::override],
            libp2p::injector::usePrivateNetwork( network_key ) );
        return MakeContextFromInjector( std::move( injector ) );
    }

    std::shared_ptr<ProcessingCoreImpl> ProcessingCoreImpl::New( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                                                 uint32_t maximalProcessingSubTaskCount,
                                                                 TokenID  tokenID,
                                                                 std::string network_key )
    {
        if ( ( maximalProcessingSubTaskCount == 0 ) || ( !task_queue ) )
        {
            return nullptr;
        }
        auto instance = std::shared_ptr<ProcessingCoreImpl>(
            new ProcessingCoreImpl( std::move( task_queue ),
                                    maximalProcessingSubTaskCount,
                                    std::move( tokenID ),
                                    std::move( network_key ) ) );
        return instance;
    }

    ProcessingCoreImpl::ProcessingCoreImpl( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                            uint32_t                             maximalProcessingSubTaskCount,
                                            TokenID                              tokenID,
                                            std::string                          network_key ) :
        task_queue_( std::move( task_queue ) ),
        token_ID_( std::move( tokenID ) ),
        max_processing_subtask_count_( maximalProcessingSubTaskCount ),
        network_key_( std::move( network_key ) ),
        connection_gater_( std::make_shared<sgns::ipfs_pubsub::DenyListConnectionGater>() )
    {
    }

    outcome::result<SGProcessing::SubTaskResult> ProcessingCoreImpl::ProcessSubTask(
        const SGProcessing::SubTask &subTask,
        uint32_t                     initialHashCode )
    {
        //Check if we're processing too much.
        BOOST_OUTCOME_TRY( IncProcessingSubTaskCount() );

        Error error{ Error::GLOBALDB_READ_ERROR };

        do
        {
            auto get_task_retval = task_queue_->GetTask( subTask.ipfsblock() );
            if ( !get_task_retval.has_value() )
            {
                error = Error::GLOBALDB_READ_ERROR;
                break;
            }

            const SGProcessing::Task &task = get_task_retval.value();

            auto manager_retval = sgns::sgprocessing::ProcessingManager::Create( task.json_data() );

            if ( !manager_retval.has_value() )
            {
                error = Error::JOB_INCOMPATIBILITY_ERROR;
                break;
            }
            processing_manager_ = std::move( manager_retval.value() );

            auto model_retval = sgns::sgprocessing::ProcessingManager::GetModelNodeFromJson( subTask.json_data() );

            if ( !model_retval.has_value() )
            {
                error = Error::INVALID_MODEL_ERROR;
                break;
            }

            libp2p::protocol::kademlia::Config kademlia_config;
            kademlia_config.randomWalk.enabled  = true;
            kademlia_config.randomWalk.interval = std::chrono::seconds( 300 );
            kademlia_config.requestConcurency   = 20;

            // Per-subtask host composition with the same private-network enforcement
            // as the gossip host (D-11): Noise-only security + connection gater, plus
            // the pnet PSK boundary when a network key is configured. Invalid key
            // material throws eagerly - caught here and mapped to an Error instead of
            // leaking an exception or proceeding with a half-configured host.
            std::shared_ptr<boost::asio::io_context> ioc;
            try
            {
                auto host_context = MakeGatedHostInjector( network_key_, connection_gater_, kademlia_config );
                ioc               = std::move( host_context.io_context );
            }
            catch ( const std::exception &e )
            {
                // Never log the key material itself - only the error message.
                ProcessingCoreLogger()->error( "Private-network (pnet) host initialization failed: {}", e.what() );
                error = Error::PNET_INITIALIZATION_ERROR;
                break;
            }

            std::vector<std::vector<uint8_t>> chunk_hashes;
            std::vector<std::string>        output_locations;
            auto result_retval = processing_manager_->Process( ioc, chunk_hashes, model_retval.value(), output_locations );

            DecProcessingSubTaskCount();
            
            if ( !result_retval.has_value() )
            {
                return result_retval.error();
            }

            SGProcessing::SubTaskResult result;
            for ( auto &chunk_hash : chunk_hashes )
            {
                std::string hash_string( chunk_hash.begin(), chunk_hash.end() );
                result.add_chunk_hashes( hash_string );
            }

            std::string hash_string( result_retval.value().begin(), result_retval.value().end() );
            result.set_result_hash( hash_string );
            result.set_token_id( token_ID_.bytes().data(), token_ID_.size() );

            // Populate output location(s) in the result so the job requester
            // can discover where their output was saved (file path, IPFS CID, etc.)
            if ( !output_locations.empty() )
            {
                // Build a delimited string of all non-empty output locations
                std::string joined_locations;
                for ( const auto &loc : output_locations )
                {
                    if ( !loc.empty() )
                    {
                        if ( !joined_locations.empty() )
                        {
                            joined_locations += "\n";
                        }
                        joined_locations += loc;
                    }
                }
                if ( !joined_locations.empty() )
                {
                    result.set_ipfs_results_data_id( joined_locations );
                }
            }

            return result;

        } while ( 0 );

        DecProcessingSubTaskCount();

        return outcome::failure( error );
    }

    float ProcessingCoreImpl::GetProgress() const
    {
        if ( processing_manager_ )
        {
            return processing_manager_->GetProgress();
        }
        return 0.0f;
    }

    outcome::result<void> ProcessingCoreImpl::IncProcessingSubTaskCount()
    {
        std::scoped_lock<std::mutex> subTaskCountLock( subtask_count_mutex_ );

        if ( processing_subtask_count_ >= max_processing_subtask_count_ )
        {
            // Reset the counter to allow processing restart
            return outcome::failure( Error::MAX_NUMBER_SUBTASKS );
        }
        processing_subtask_count_++;
        return outcome::success();
    }

    void ProcessingCoreImpl::DecProcessingSubTaskCount()
    {
        std::scoped_lock<std::mutex> subTaskCountLock( subtask_count_mutex_ );
        if ( processing_subtask_count_ > 0 )
        {
            --processing_subtask_count_;
        }
    }

}
