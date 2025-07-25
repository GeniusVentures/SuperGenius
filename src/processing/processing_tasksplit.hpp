/**
 * @file       processing_tasksplit.hpp
 * @brief      
 * @date       2024-04-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _PROCESSING_TASKSPLIT_HPP_
#define _PROCESSING_TASKSPLIT_HPP_
#include <cstdlib>
#include <list>

#include <boost/format.hpp>

#include "processing/proto/SGProcessing.pb.h"
#include "processing/processing_imagesplit.hpp"
#include "processing/processing_subtask_state_storage.hpp"
#include <SGNSProcMain.hpp>

namespace sgns
{
    namespace processing
    {
        class ProcessTaskSplitter
        {
        public:
            ProcessTaskSplitter();

            void SplitTask( const SGProcessing::Task         &task,
                            std::list<SGProcessing::SubTask> &subTasks,
                            std::string                       json_data,
                            uint32_t numchunks, bool addvalidationsubtask, std::string ipfsid );

        private:

        };

        class ProcessSubTaskStateStorage : public SubTaskStateStorage
        {
        public:
            void ChangeSubTaskState( const std::string &subTaskId, SGProcessing::SubTaskState::Type state ) override;
            std::optional<SGProcessing::SubTaskState> GetSubTaskState( const std::string &subTaskId ) override;
        };
    }
}

#endif
