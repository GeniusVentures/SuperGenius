/**
 * @file       processing_tasksplit.cpp
 * @brief      
 * @date       2024-04-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "processing/processing_tasksplit.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace sgns
{
    namespace processing
    {
        ProcessTaskSplitter::ProcessTaskSplitter()
        {
        }

        std::string generate_uuid_with_ipfs_id(const std::string& ipfs_id) {
            // Hash the IPFS ID
            std::hash<std::string> hasher;
            uint64_t id_hash = hasher(ipfs_id);

            // Get a high-resolution timestamp
            auto now = std::chrono::high_resolution_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                now.time_since_epoch())
                                .count();

            // Combine the IPFS ID hash and timestamp to create a seed
            uint64_t seed = id_hash ^ static_cast<uint64_t>(timestamp);

            // Seed the PRNG
            std::mt19937 gen(seed);
            boost::uuids::basic_random_generator<std::mt19937> uuid_gen(gen);

            // Generate UUID
            boost::uuids::uuid uuid = uuid_gen();
            return boost::uuids::to_string(uuid);
        }

        void ProcessTaskSplitter::SplitTask( const SGProcessing::Task &task, std::list<SGProcessing::SubTask> &subTasks, std::string json_data,
                                             uint32_t numchunks, bool addvalidationsubtask, std::string ipfsid)
        {
            std::optional<SGProcessing::SubTask> validationSubtask;
            if (addvalidationsubtask)
            {
                validationSubtask = SGProcessing::SubTask();
            }

            size_t chunkId = 0;
            //auto subtaskId = base58.value();
            SGProcessing::SubTask subtask;

            //IPFS Block is the task ID for lookup
            subtask.set_ipfsblock( task.ipfs_block_id() );

            subtask.set_json_data(json_data);

            //Generate a subtask uuid.
            //boost::uuids::uuid uuid = boost::uuids::random_generator()();
            // boost::uuids::random_generator_pure gen;
            // boost::uuids::uuid uuid = gen();
            // auto uuidstring = boost::uuids::to_string(uuid);
            auto uuidstring = generate_uuid_with_ipfs_id(ipfsid);
            std::cout << "Subtask UID: " << uuidstring << std::endl;
            subtask.set_subtaskid(uuidstring);
            for ( size_t chunkIdx = 0; chunkIdx < numchunks; ++chunkIdx )
            {
                std::cout << "AddChunk : " << chunkIdx << std::endl;
                SGProcessing::ProcessingChunk chunk;
                chunk.set_chunkid( ( boost::format( "CHUNK_%d_%d" ) % uuidstring % chunkId ).str() );
                chunk.set_n_subchunks( 1 );

                auto chunkToProcess = subtask.add_chunkstoprocess();
                chunkToProcess->CopyFrom( chunk );

                if ( validationSubtask )
                {
                    if ( chunkIdx == 0 )
                    {
                        // Add the first chunk of a processing subtask into the validation subtask
                        auto chunkToValidate = validationSubtask->add_chunkstoprocess();
                        chunkToValidate->CopyFrom( chunk );
                    }
                }

                ++chunkId;
            }
            std::cout << "Subtask? " << subtask.chunkstoprocess_size() << std::endl;
            subTasks.push_back( std::move( subtask ) );
            

            if (addvalidationsubtask)
            {
                auto subtaskId = ( boost::format( "subtask_validation" ) ).str();
                validationSubtask->set_ipfsblock( task.ipfs_block_id() );
                validationSubtask->set_subtaskid( subtaskId );
                subTasks.push_back( std::move( *validationSubtask ) );
            }
        }

        void ProcessSubTaskStateStorage::ChangeSubTaskState( const std::string &subTaskId, SGProcessing::SubTaskState::Type state )
        {
        }
        std::optional<SGProcessing::SubTaskState> ProcessSubTaskStateStorage::GetSubTaskState( const std::string &subTaskId )
        {
            return std::nullopt;
        }
    }
}