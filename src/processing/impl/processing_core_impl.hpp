#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_IMPL_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_IMPL_HPP

#include <cmath>
#include <memory>
#include <iostream>
#include <utility>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/injector/kademlia_injector.hpp>

#include "processing/processing_core.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "account/TokenID.hpp"

// Forward declaration
namespace sgns::sgprocessing
{
    class ProcessingManager;
}

namespace sgns::processing
{
    /**
     * @brief Default implementation of ProcessingCore backed by GlobalDB.
     */
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        enum class Error
        {
            MAX_NUMBER_SUBTASKS = 1,
            GLOBALDB_READ_ERROR,
            NO_BUFFER_FROM_JOB_DATA,
        };

        ProcessingCoreImpl( std::shared_ptr<sgns::crdt::GlobalDB> db,
                            size_t                                maximalProcessingSubTaskCount,
                            TokenID                               tokenId ) :
            m_db( std::move( db ) ),
            m_tokenId( std::move( tokenId ) ),
            m_maximalProcessingSubTaskCount( maximalProcessingSubTaskCount ),
            m_processingSubTaskCount( 0 )
        {
        }

        ~ProcessingCoreImpl() {}

        /** Process a single subtask.
        * @param subTask - Subtask that needs to be processed.
        * @param initialHashCode - Initial hash code used to calculate result hash.
        */
        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask( const SGProcessing::SubTask &subTask,
                                                                     uint32_t initialHashCode ) override;

        /** Get current processing progress.
        * @return Progress percentage (0.0 to 100.0).
        */
        float GetProgress() const override;

        std::vector<size_t> m_chunkResulHashes;
        std::vector<size_t> m_validationChunkHashes;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        TokenID                               m_tokenId;
        size_t                                m_maximalProcessingSubTaskCount;

        std::mutex m_subTaskCountMutex;
        size_t     m_processingSubTaskCount;

        mutable std::shared_ptr<sgprocessing::ProcessingManager> m_currentProcessingManager;

        std::map<std::string,
                 std::shared_ptr<std::pair<std::shared_ptr<std::vector<char>>, std::shared_ptr<std::vector<char>>>>>
            cidData_;
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns::processing, ProcessingCoreImpl::Error );

#endif
