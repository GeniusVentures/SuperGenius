#ifndef GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_IMPL_HPP
#define GRPC_FOR_SUPERGENIUS_PROCESSING_CORE_IMPL_HPP

#include <cmath>
#include <memory>
#include <iostream>
#include <utility>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/injector/kademlia_injector.hpp>

#include "processing/processing_core.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "account/TokenID.hpp"


namespace sgns::processing
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:

        enum class Error {
            MAX_NUMBER_SUBTASKS = 1,
            GLOBALDB_READ_ERROR,
            NO_BUFFER_FROM_JOB_DATA,
        };
        ProcessingCoreImpl(
            std::shared_ptr<sgns::crdt::GlobalDB> db,
            size_t subTaskProcessingTime,
            size_t maximalProcessingSubTaskCount,
            TokenID tokenId)
            : m_db(std::move(db))
            //, m_subTaskProcessingTime(subTaskProcessingTime)
            , m_tokenId(std::move(tokenId))
            , m_processor(nullptr)
            , m_maximalProcessingSubTaskCount(maximalProcessingSubTaskCount)
            , m_processingSubTaskCount(0)
        {
        }

        ~ProcessingCoreImpl()
        {
        }
        /** Process a single subtask
        * @param subTask - subtask that needs to be processed
        * @param result - subtask result
        * @param initialHashCode - an initial hash code which is used to calculate result hash
        */
        outcome::result<SGProcessing::SubTaskResult> ProcessSubTask(
        const SGProcessing::SubTask& subTask, uint32_t initialHashCode) override;

        /** Register an available processor
        * @param name - Name of processor
        * @param factoryFunction - Pointer to processor
        */
        //void RegisterProcessorFactory(const std::string& name, std::function<std::unique_ptr<ProcessingProcessor>()> factoryFunction) {
        //    m_processorFactories[name] = std::move( factoryFunction );
        //}

        /** Set the current processor by name
        * @param name - Name of processor
        */
       // bool SetProcessorByName(const std::string& name) {
       //     auto factoryFunction = m_processorFactories.find(name);
       //     if (factoryFunction != m_processorFactories.end()) {
       //         m_processor = factoryFunction->second();
       //         return true;
       //     }
       //     std::cerr << "Unknown processor name: " << name << std::endl;
       //     return false;
       // }

       //bool CheckRegisteredProcessor(const std::string& name)
       // {
       //     auto factoryFunction = m_processorFactories.find(name);
       //     if (factoryFunction == m_processorFactories.end())
       //     {
       //         return false;
       //     }
       //     else
       //     {
       //         return true;
       //     }
       // }

        /** Get processing type from json data to set processor
        * @param jsondata - jsondata that needs to be parsed
        */
        //bool SetProcessingTypeFromJson(std::string jsondata) override;

        /** Get settings.json and then get data we need for processing based on parsing
        * @param CID - CID of directory to get settings.json from
        */
        std::shared_ptr<std::pair<std::shared_ptr<std::vector<char>>, std::shared_ptr<std::vector<char>>>>  GetCidForProc(std::string json_data, std::string base_json) override;
        /** Get files from a set URL and insert them into pair reference 
        * @param ioc - IO context to run on
        * @param url - ipfs gateway url to get from
        * @param results - reference to data pair to insert into.
        */
        void GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc, std::string url, std::shared_ptr<std::vector<char>> results) override;

        std::vector<size_t> m_chunkResulHashes;
        std::vector<size_t> m_validationChunkHashes;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        TokenID                              m_tokenId;
        //std::unique_ptr<ProcessingProcessor> m_processor;
        //std::unordered_map<std::string, std::function<std::unique_ptr<ProcessingProcessor>()>> m_processorFactories;
        //size_t m_subTaskProcessingTime;
        size_t m_maximalProcessingSubTaskCount;

        std::mutex m_subTaskCountMutex;
        size_t m_processingSubTaskCount;



        std::map<std::string, std::shared_ptr<std::pair<std::shared_ptr<std::vector<char>>, std::shared_ptr<std::vector<char>>>>> cidData_;
        
    };
}

OUTCOME_HPP_DECLARE_ERROR_2(sgns::processing, ProcessingCoreImpl::Error);



#endif