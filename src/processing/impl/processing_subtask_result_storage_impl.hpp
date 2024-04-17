/**
* Header file for results data storage implementation over CRDT
* @author creativeid00, Comments added by Justin Church
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
        /** Create a subtask storage
        * @param db - CRDT globaldb to use
        */
        SubTaskResultStorageImpl(std::shared_ptr<sgns::crdt::GlobalDB> db);

        /** Add a subtask result
        * @param result - Result to add
        */
        void AddSubTaskResult(const SGProcessing::SubTaskResult& result) override;

        /** Remove a subtask result
        * @param subTaskId - Result ID to remove
        */
        void RemoveSubTaskResult(const std::string& subTaskId) override;

        /** Get results for subtasks
        * @param subTaskIds - List of subtasks IDs to get results for
        * @param results - List of results reference.
        */
        void GetSubTaskResults(
            const std::set<std::string>& subTaskIds,
            std::vector<SGProcessing::SubTaskResult>& results) override;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
    };
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_RESULT_STORAGE_IMPL_HPP
