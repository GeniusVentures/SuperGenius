/**
 * @file       processing_tasksplit.cpp
 * @brief      
 * @date       2024-04-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "processing/processing_tasksplit.hpp"
namespace sgns
{
    namespace processing
    {
        ProcessTaskSplitter::ProcessTaskSplitter( size_t nSubTasks, size_t nChunks, bool addValidationSubtask ) :
            m_nSubTasks( nSubTasks ), //
            m_nChunks( nChunks ),     //
            m_addValidationSubtask( addValidationSubtask )
        {
        }

        void ProcessTaskSplitter::SplitTask( const SGProcessing::Task &task, std::list<SGProcessing::SubTask> &subTasks,
                                             sgns::processing::ImageSplitter &SplitImage, std::vector<std::vector<uint32_t>> chunkOptions )
        {
            std::optional<SGProcessing::SubTask> validationSubtask;
            if ( m_addValidationSubtask )
            {
                validationSubtask = SGProcessing::SubTask();
            }

            size_t chunkId = 0;
            for ( size_t i = 0; i < m_nSubTasks; ++i )
            {
                auto cidcheck = SplitImage.GetPartCID( i );
                auto base58   = libp2p::multi::ContentIdentifierCodec::toString( cidcheck );
                //std::cout << "Base56:    " << base58.value() << std::endl;
                //std::cout << "Task BlockID  : " << task.ipfs_block_id() << std::endl;

                auto subtaskId = ( boost::format( "subtask_%d" ) % i ).str();

                //auto subtaskId = base58.value();
                SGProcessing::SubTask subtask;
                subtask.set_ipfsblock( task.ipfs_block_id() );
                //subtask.set_ipfsblock(base58.value());
                //std::cout << "BLOCK ID CHECK: " << task.ipfs_block_id() << std::endl;
                std::cout << "Subtask ID :: " << base58.value() + "_" + std::to_string( i ) << std::endl;
                std::cout << "Subtask CID :: " << task.ipfs_block_id() << std::endl;

                subtask.set_subtaskid( base58.value() + "_" + std::to_string( i ) );
                for ( size_t chunkIdx = 0; chunkIdx < chunkOptions.at( i ).at( 5 ); ++chunkIdx )
                {
                    //std::cout << "AddChunk : " << chunkIdx << std::endl;
                    SGProcessing::ProcessingChunk chunk;
                    chunk.set_chunkid( ( boost::format( "CHUNK_%d_%d" ) % i % chunkId ).str() );
                    chunk.set_n_subchunks( 1 );
                    chunk.set_line_stride( chunkOptions.at( i ).at( 0 ) );
                    chunk.set_offset( chunkOptions.at( i ).at( 1 ) );
                    chunk.set_stride( chunkOptions.at( i ).at( 2 ) );
                    chunk.set_subchunk_height( chunkOptions.at( i ).at( 3 ) );
                    chunk.set_subchunk_width( chunkOptions.at( i ).at( 4 ) );

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
                //std::cout << "Subtask? " << subtask.chunkstoprocess_size() << std::endl;
                subTasks.push_back( std::move( subtask ) );
            }

            if ( validationSubtask )
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