#include "processing/impl/processing_core_impl.hpp"

#include <rapidjson/document.h>

#include "FileManager.hpp"
#include <processingbase/ProcessingManager.hpp>

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
    }
    return "Unknown error";
}

namespace sgns::processing
{
    outcome::result<SGProcessing::SubTaskResult> ProcessingCoreImpl::ProcessSubTask(
        const SGProcessing::SubTask &subTask,
        uint32_t                     initialHashCode )
    {
        SGProcessing::SubTaskResult result;

        //Check if we're processing too much.
        std::scoped_lock<std::mutex> subTaskCountLock( m_subTaskCountMutex );
        ++m_processingSubTaskCount;
        if ( ( m_maximalProcessingSubTaskCount > 0 ) && ( m_processingSubTaskCount > m_maximalProcessingSubTaskCount ) )
        {
            // Reset the counter to allow processing restart
            m_processingSubTaskCount = 0;
            return outcome::failure( Error::MAX_NUMBER_SUBTASKS );
        }

        auto queryTasks = m_db->Get( "tasks/TASK_" + subTask.ipfsblock() );
        if ( !queryTasks.has_value() )
        {
            return outcome::failure( Error::GLOBALDB_READ_ERROR );
            //task.ParseFromArray(element, element.second.size());
        }
        SGProcessing::Task task;

        //Create io context for obtaining data
        libp2p::protocol::kademlia::Config kademlia_config;
        kademlia_config.randomWalk.enabled  = true;
        kademlia_config.randomWalk.interval = std::chrono::seconds( 300 );
        kademlia_config.requestConcurency   = 20;
        auto injector                       = libp2p::injector::makeHostInjector(
            libp2p::injector::makeKademliaInjector( libp2p::injector::useKademliaConfig( kademlia_config ) ) );
        auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();

        task.ParseFromArray( queryTasks.value().data(), queryTasks.value().size() );
        auto jsondata = task.json_data();
        OUTCOME_TRY( auto procmgr, sgns::sgprocessing::ProcessingManager::Create( jsondata ) );
        std::vector<std::vector<uint8_t>> chunkhashes;
        auto                              tempResult = procmgr->Process( ioc, chunkhashes , 1);
        if ( tempResult )
        {
            std::string hashString( tempResult.value().begin(), tempResult.value().end() );
            result.set_result_hash( hashString );
            result.set_token_id( m_tokenId.bytes().data(), m_tokenId.size() );
            --m_processingSubTaskCount;
        }
        else
        {
            return tempResult.error();
        }
        return result;
    }
    
}
