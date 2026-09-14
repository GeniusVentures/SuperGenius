/**
 * @file       processing_tasksplit_elm.hpp
 * @brief      ELM task splitter: one elms[] work item -> one subtask with one
 *             notional chunk + the elm_subtask_map written into the task JSON
 *             (elmbridge Phase 4, plan 04-03; binding contract 01-DESIGN-SUBTASK-MAPPING).
 * @date       2026-09-14
 */
#ifndef _PROCESSING_TASKSPLIT_ELM_HPP_
#define _PROCESSING_TASKSPLIT_ELM_HPP

#include <list>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "processing/proto/SGProcessing.pb.h"

#include <SGNSProcMain.hpp>

namespace sgns
{
    namespace processing
    {
        class ProcessTaskSplitterELM
        {
        public:
            ProcessTaskSplitterELM();

            /** Split an elm_processing task 1:1 (01-DESIGN-SUBTASK-MAPPING §1).
             *
             * Per elms[] work item: mints one SubTask (uuid via the
             * generate_uuid_with_ipfs_id discipline) whose json_data is a
             * serialized sgns::ModelNode with source "input:<work_item_id>"
             * (the id the 04-02 worker routing resolves), carrying EXACTLY ONE
             * notional chunk (subtask-unique CHUNK_<uuid>_0 — §2, keeps
             * ValidateResults cross-comparison structurally inert). No
             * validation subtask ever (no ELM analog).
             *
             * Also writes taskJsonRoot["elm_subtask_map"] — entries
             * {"work_item_id", "subtaskid"} in elms[] order (§3) — OVERWRITING
             * any requestor-supplied map unconditionally (T-04-03-01: the
             * splitter is the only writer; the map is immutable after publish).
             * The caller re-serializes taskJsonRoot into task.json_data AFTER
             * this call so the published task JSON carries the map.
             *
             * @param task         - the task (ipfs_block_id copied to subtasks)
             * @param subTasks     - out: one SubTask per elms[] entry, in order
             * @param elms         - the parse-gated work items
             * @param ipfsid       - host id seeding the subtask uuids
             * @param taskJsonRoot - out: the task JSON object receiving elm_subtask_map
             */
            void SplitTask( const SGProcessing::Task         &task,
                            std::list<SGProcessing::SubTask> &subTasks,
                            const std::vector<sgns::Elm>     &elms,
                            std::string                       ipfsid,
                            nlohmann::json                   &taskJsonRoot );
        };
    }
}

#endif
