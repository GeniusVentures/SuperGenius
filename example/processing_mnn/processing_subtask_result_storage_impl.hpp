/**
* Header file for results data storage implementation over CRDT
* @author creativeid00
*/

#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_RESULT_STORAGE_IMPL_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_RESULT_STORAGE_IMPL_HPP

#include <processing/processing_subtask_result_storage.hpp>
#include <crdt/globaldb/globaldb.hpp>

namespace sgns::processing
{
/** Handles subtask states storage
*/
    class SubTaskResultStorageImpl : public SubTaskResultStorage
    {
    public:
        SubTaskResultStorageImpl(std::shared_ptr<sgns::crdt::GlobalDB> db);

        /** SubTaskResultStorage overrides
        */
        void AddSubTaskResult(const SGProcessing::SubTaskResult& result) override;
        void RemoveSubTaskResult(const std::string& subTaskId) override;
        void GetSubTaskResults(
            const std::set<std::string>& subTaskIds,
            std::vector<SGProcessing::SubTaskResult>& results) override;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
    };
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_RESULT_STORAGE_IMPL_HPP
