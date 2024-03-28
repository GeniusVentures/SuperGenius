#include <processing/impl/processing_core_impl.hpp>

namespace sgns::processing
{
    void ProcessingCoreImpl::ProcessSubTask(
        const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
        uint32_t initialHashCode)
    {
        {
            std::scoped_lock<std::mutex> subTaskCountLock(m_subTaskCountMutex);
            ++m_processingSubTaskCount;
            if ((m_maximalProcessingSubTaskCount > 0)
                && (m_processingSubTaskCount > m_maximalProcessingSubTaskCount))
            {
                // Reset the counter to allow processing restart
                m_processingSubTaskCount = 0;
                throw std::runtime_error("Maximal number of processed subtasks exceeded");
            }
        }

    }
}