#include "processing_validation_core.hpp"

namespace sgns::processing
{
////////////////////////////////////////////////////////////////////////////////
ProcessingValidationCore::ProcessingValidationCore()
{
}

bool ProcessingValidationCore::ValidateResults(
    const SGProcessing::SubTaskCollection& subTasks,
    const std::map<std::string, SGProcessing::SubTaskResult>& results,
    std::set<std::string>& invalidSubTaskIds)
{
    bool areResultsValid = true;
    // Compare result hashes for each chunk
    // If a chunk hashes didn't match each other add the all subtasks with invalid hashes to VALID ITEMS LIST
    std::map<std::string, std::vector<uint8_t>> chunks;
    
    for (int itemIdx = 0; itemIdx < subTasks.items_size(); ++itemIdx)
    {
        const auto& subTask = subTasks.items(itemIdx);
        auto itResult = results.find(subTask.subtaskid());
        if (itResult != results.end())
        {
            if (itResult->second.chunk_hashes_size() != subTask.chunkstoprocess_size())
            {
                m_logger->error("WRONG_RESULT_HASHES_LENGTH {}", subTask.subtaskid());
                invalidSubTaskIds.insert(subTask.subtaskid());
            }
            else
            {
                for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
                {
                    auto it = chunks.insert(std::make_pair(
                        subTask.chunkstoprocess(chunkIdx).SerializeAsString(), std::vector<uint8_t>()));
                    const std::string& chunkHashBytes = itResult->second.chunk_hashes(chunkIdx);
                    //it.first->second.push_back(itResult->second.chunk_hashes(chunkIdx));
                    it.first->second.insert(it.first->second.end(), chunkHashBytes.begin(), chunkHashBytes.end());
                }
            }
        }
        else
        {
            // Since all subtasks are processed a result should be found for all of them
            m_logger->error("NO_RESULTS_FOUND {}", subTask.subtaskid());
            invalidSubTaskIds.insert(subTask.subtaskid());
        }
    }

    for (int itemIdx = 0; itemIdx < subTasks.items_size(); ++itemIdx)
    {
        const auto& subTask = subTasks.items(itemIdx);
        if ((invalidSubTaskIds.find(subTask.subtaskid()) == invalidSubTaskIds.end())
            && !CheckSubTaskResultHashes(subTask, chunks))
        {
            invalidSubTaskIds.insert(subTask.subtaskid());
        }
    }

    if (!invalidSubTaskIds.empty())
    {
        areResultsValid = false;
    }
    return areResultsValid;
}

bool ProcessingValidationCore::CheckSubTaskResultHashes(
    const SGProcessing::SubTask& subTask,
    const std::map<std::string, std::vector<uint8_t>>& chunks) const
{
    std::unordered_set<std::string> encounteredHashes;
    for (int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx)
    {
        const auto& chunk = subTask.chunkstoprocess(chunkIdx);
        auto it = chunks.find(chunk.SerializeAsString());
        if (it != chunks.end())
        {
            // Check duplicated chunks only
            //if ((it->second.size() >= 2)
            //    && !std::equal(it->second.begin() + 1, it->second.end(), it->second.begin()))
            //{
            //    m_logger->debug("INVALID_CHUNK_RESULT_HASH [{}, {}]", subTask.subtaskid(), chunk.chunkid());
            //    return false;
            //}
            std::string chunkHash(it->second.begin(), it->second.end());
            if (!encounteredHashes.insert(chunkHash).second) {
                m_logger->debug("INVALID_CHUNK_RESULT_HASH [{}, {}]", subTask.subtaskid(), chunk.chunkid());
                return false;
            }
        }
        else
        {
            m_logger->debug("NO_CHUNK_RESULT_FOUND [{}, {}]", subTask.subtaskid(), chunk.chunkid());
            return false;
        }
    }
    return true;
}

}

////////////////////////////////////////////////////////////////////////////////
