#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_RESULT_STORAGE_IMPL_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_RESULT_STORAGE_IMPL_HPP

#include "processing/processing_subtask_result_storage.hpp"
#include "crdt/globaldb/globaldb.hpp"

namespace sgns::processing
{
    /** Handles subtask states storage
*/
    class SubTaskResultStorageImpl : public SubTaskResultStorage
    {
    public:
        /** Create a subtask storage
        * @param db - CRDT globaldb to use
        * @param storageTopic - The topic to use for atomic commits.
        */
        SubTaskResultStorageImpl( std::shared_ptr<sgns::crdt::GlobalDB> db, const std::string &storageTopic );

        /** Add a subtask result
        * @param result - Result to add
        */
        void AddSubTaskResult( const SGProcessing::SubTaskResult &result ) override;

        /** Remove a subtask result
        * @param subTaskId - Result ID to remove
        */
        void RemoveSubTaskResult( const std::string &subTaskId ) override;

        /** Get results for subtasks
        * @param subTaskIds - List of subtasks IDs to get results for
        * @param results - List of results reference.
        */
        std::vector<SGProcessing::SubTaskResult> GetSubTaskResults( const std::set<std::string> &subTaskIds ) override;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        std::string m_storageTopic;
    };
}

#endif // GRPC_FOR_SUPERGENIUS_PROCESSING_SUBTASK_RESULT_STORAGE_IMPL_HPP
