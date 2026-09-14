/**
 * @file       processing_tasksplit_elm.cpp
 * @brief      ELM task splitter implementation (elmbridge Phase 4, plan 04-03).
 * @date       2026-09-14
 */
#include "processing/processing_tasksplit_elm.hpp"

#include <boost/format.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <chrono>
#include <cstdint>
#include <random>

namespace sgns
{
    namespace processing
    {
        namespace
        {
            /// File-local copy of the uuid discipline (processing_tasksplit.cpp
            /// and GeniusNode.cpp each hold their own; do NOT refactor them —
            /// the unity build would collide identical external names).
            std::string generate_uuid_with_ipfs_id_elm( const std::string &ipfs_id )
            {
                std::hash<std::string> hasher;
                uint64_t               id_hash = hasher( ipfs_id );

                auto now       = std::chrono::high_resolution_clock::now();
                auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     now.time_since_epoch() )
                                     .count();

                uint64_t seed = id_hash ^ static_cast<uint64_t>( timestamp );

                std::mt19937                                       gen( seed );
                boost::uuids::basic_random_generator<std::mt19937> uuid_gen( gen );

                boost::uuids::uuid uuid = uuid_gen();
                return boost::uuids::to_string( uuid );
            }
        } // namespace

        ProcessTaskSplitterELM::ProcessTaskSplitterELM()
        {
        }

        void ProcessTaskSplitterELM::SplitTask( const SGProcessing::Task         &task,
                                                std::list<SGProcessing::SubTask> &subTasks,
                                                const std::vector<sgns::Elm>     &elms,
                                                std::string                       ipfsid,
                                                nlohmann::json                   &taskJsonRoot )
        {
            // §3: the splitter is the ONLY writer — overwrite unconditionally so
            // a requestor-supplied elm_subtask_map never survives (T-04-03-01).
            nlohmann::json subtaskMap = nlohmann::json::array();

            for ( const auto &elm : elms )
            {
                SGProcessing::SubTask subtask;
                subtask.set_ipfsblock( task.ipfs_block_id() );

                const std::string uuidstring = generate_uuid_with_ipfs_id_elm( ipfsid );
                subtask.set_subtaskid( uuidstring );

                // Worker-side contract (04-02): the subtask's json_data is a
                // serialized ModelNode whose source names this work item —
                // exactly what ResolveElmWorkItem scans for.
                sgns::ModelNode node;
                node.set_source( "input:" + elm.get_work_item_id() );
                nlohmann::json nodeJson;
                sgns::to_json( nodeJson, node );
                subtask.set_json_data( nodeJson.dump( -1 ) );

                // §2: exactly ONE notional chunk, subtask-unique id — the 1:1
                // counterpart of the result's single hash.
                SGProcessing::ProcessingChunk chunk;
                chunk.set_chunkid( ( boost::format( "CHUNK_%d_%d" ) % uuidstring % 0 ).str() );
                chunk.set_n_subchunks( 1 );
                subtask.add_chunkstoprocess()->CopyFrom( chunk );

                subtaskMap.push_back( {
                    { "work_item_id", elm.get_work_item_id() },
                    { "subtaskid", uuidstring },
                } );

                subTasks.push_back( std::move( subtask ) );
            }

            taskJsonRoot["elm_subtask_map"] = std::move( subtaskMap );
        }
    }
}
