
#include <math.h>
#include <fstream>
#include <memory>
#include <iostream>
#include <processing/processing_core.hpp>
#include <crdt/globaldb/globaldb.hpp>
#include <processing/processing_processor.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/injector/host_injector.hpp>
#include "libp2p/injector/kademlia_injector.hpp"
#include "Singleton.hpp"
#include "FileManager.hpp"
#include "URLStringUtil.h"

namespace sgns::processing
{
    class ProcessingCoreImpl : public ProcessingCore
    {
    public:
        ProcessingCoreImpl(
            std::shared_ptr<sgns::crdt::GlobalDB> db,
            size_t subTaskProcessingTime,
            size_t maximalProcessingSubTaskCount)
            : m_db(db)
            , m_subTaskProcessingTime(subTaskProcessingTime)
            , m_maximalProcessingSubTaskCount(maximalProcessingSubTaskCount)
            , m_processingSubTaskCount(0)
            , m_processor(nullptr)
        {
        }

        void ProcessSubTask(
            const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
            uint32_t initialHashCode) override;

        void RegisterProcessorFactory(const std::string& name, std::function<std::unique_ptr<ProcessingProcessor>()> factoryFunction) {
            m_processorFactories[name] = factoryFunction;
        }

        void SetProcessorByName(const std::string& name) {
            auto factoryFunction = m_processorFactories.find(name);
            if (factoryFunction != m_processorFactories.end()) {
                m_processor = factoryFunction->second();
            }
            else {
                std::cerr << "Unknown processor name: " << name << std::endl;
            }
        }

        std::vector<size_t> m_chunkResulHashes;
        std::vector<size_t> m_validationChunkHashes;

    private:
        std::shared_ptr<sgns::crdt::GlobalDB> m_db;
        std::unique_ptr<ProcessingProcessor> m_processor;
        std::unordered_map<std::string, std::function<std::unique_ptr<ProcessingProcessor>()>> m_processorFactories;
        size_t m_subTaskProcessingTime;
        size_t m_maximalProcessingSubTaskCount;

        std::mutex m_subTaskCountMutex;
        size_t m_processingSubTaskCount;
        std::map<std::string, std::vector<char>> cidData_;
    };
}


