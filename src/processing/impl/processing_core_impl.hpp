
#include <math.h>
#include <fstream>
#include <memory>
#include <iostream>
#include <processing/processing_core.hpp>
#include <crdt/globaldb/globaldb.hpp>

namespace sgns::processing
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(
            std::shared_ptr<sgns::crdt::GlobalDB> db,
            size_t subTaskProcessingTime,
            size_t maximalProcessingSubTaskCount)
            : m_db(db)
            , m_subTaskProcessingTime(subTaskProcessingTime)
            , m_maximalProcessingSubTaskCount(maximalProcessingSubTaskCount)
            , m_processingSubTaskCount(0)
        {
        }

        void ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override;

        std::vector<size_t> m_chunkResulHashes;
        std::vector<size_t> m_validationChunkHashes;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        size_t m_subTaskProcessingTime;
        size_t m_maximalProcessingSubTaskCount;

        std::mutex m_subTaskCountMutex;
        size_t m_processingSubTaskCount;
    };
}


