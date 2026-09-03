#include "processing/impl/processing_core_impl.hpp"

#include <rapidjson/document.h>

#include "processing/processing_validation_core.hpp"

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
    }
    return "Unknown error";
}

namespace sgns::processing
{

    std::shared_ptr<ProcessingCoreImpl> ProcessingCoreImpl::New( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                                                 uint32_t           maximalProcessingSubTaskCount,
                                                                 TokenID            tokenID,
                                                                 const std::string &developerAddress,
                                                                 uint64_t           developerCut )
    {
        if ( ( maximalProcessingSubTaskCount == 0 ) || ( !task_queue ) || developerAddress.empty() ||
             ( developerCut > ProcessingValidationCore::DEVELOPER_CUT_SCALE ) )
        {
            return nullptr;
        }
        auto instance = std::shared_ptr<ProcessingCoreImpl>( new ProcessingCoreImpl( std::move( task_queue ),
                                                                                     maximalProcessingSubTaskCount,
                                                                                     std::move( tokenID ),
                                                                                     developerAddress,
                                                                                     developerCut ) );
        return instance;
    }

    ProcessingCoreImpl::ProcessingCoreImpl( std::shared_ptr<ProcessingTaskQueue> task_queue,
                                            uint32_t                             maximalProcessingSubTaskCount,
                                            TokenID                              tokenID,
                                            std::string                          developerAddress,
                                            uint64_t                             developerCut ) :
        task_queue_( std::move( task_queue ) ),
        token_ID_( std::move( tokenID ) ),
        developer_address_( std::move( developerAddress ) ),
        developer_cut_( developerCut ),
        max_processing_subtask_count_( maximalProcessingSubTaskCount )
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
            auto injector                       = libp2p::injector::makeHostInjector(
                libp2p::injector::makeKademliaInjector( libp2p::injector::useKademliaConfig( kademlia_config ) ) );
            auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();

            std::vector<std::vector<uint8_t>> chunk_hashes;
            std::vector<std::string>          output_locations;
            auto                              result_retval = processing_manager_->Process( ioc,
                                                                                            chunk_hashes,
                                                                                            model_retval.value(),
                                                                                            output_locations );

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
            // Payout metadata is reported by the peer that ran the work, not by the job poster:
            // peers of the same job may be running apps from different developers.
            result.set_developer_address( developer_address_ );
            result.set_developer_cut( developer_cut_ );

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

    std::shared_ptr<ProcessingTaskQueue> ProcessingCoreImpl::GetTaskQueue() const
    {
        return task_queue_;
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
