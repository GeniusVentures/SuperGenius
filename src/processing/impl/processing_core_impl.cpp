#include <processing/impl/processing_core_impl.hpp>
#include <rapidjson/document.h>
namespace sgns::processing
{
    void ProcessingCoreImpl::ProcessSubTask(
        const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
        uint32_t initialHashCode)
    {
        {
            std::scoped_lock<std::mutex> subTaskCountLock(m_subTaskCountMutex);
            ++m_processingSubTaskCount;
            if ((m_maximalProcessingSubTaskCount > 0)
                && (m_processingSubTaskCount > m_maximalProcessingSubTaskCount))
            {
                // Reset the counter to allow processing restart
                m_processingSubTaskCount = 0;
                throw std::runtime_error("Maximal number of processed subtasks exceeded");
            }
        }
        if (cidData_.find(subTask.ipfsblock()) == cidData_.end())
        {
                        //ASIO for Async, should probably be made to use the main IO but this class doesn't have it 
            libp2p::protocol::kademlia::Config kademlia_config;
            kademlia_config.randomWalk.enabled = true;
            kademlia_config.randomWalk.interval = std::chrono::seconds(300);
            kademlia_config.requestConcurency = 20;
            auto injector = libp2p::injector::makeHostInjector(
                libp2p::injector::makeKademliaInjector(
                    libp2p::injector::useKademliaConfig(kademlia_config)));
            auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();

            boost::asio::io_context::executor_type executor = ioc->get_executor();
            boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(executor);

            //Get Image Async
            FileManager::GetInstance().InitializeSingletons();
            string fileURL = "ipfs://" + subTask.ipfsblock() + "/test.png";

            auto data = FileManager::GetInstance().LoadASync(fileURL, false, false, ioc, [&result, &subTask](const int& status)
                {
                    std::cout << "status: " << status << std::endl;
                }, [subTask, &result, initialHashCode, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
                    {
                        std::cout << "Final Callback" << std::endl;
                        
                        if (!buffers || (buffers->first.empty() && buffers->second.empty()))
                        {
                            std::cout << "Buffer from AsyncIO is 0" << std::endl;
                            return;
                        }
                        else {
                            //Process settings json
                            size_t index = std::string::npos;
                            for (size_t i = 0; i < buffers->first.size(); ++i) {
                                if (buffers->first[i].find("settings.json") != std::string::npos) {
                                    index = i;
                                    break;
                                }
                            }
                            if (index == std::string::npos)
                            {
                                std::cerr << "settings.json doesn't exist" << std::endl;
                                return;
                            }
                            std::vector<char>& jsonData = buffers->second[index];
                            std::string jsonString(jsonData.begin(), jsonData.end());

                            //Set processor or fail.
                            if (!this->SetProcessingTypeFromJson(jsonString))
                            {
                                return;
                            }
                            

                            //Get Task
                            SGProcessing::Task task;
                            auto queryTasks = m_db->Get("tasks/TASK_" + subTask.ipfsblock());
                            if (queryTasks.has_value())
                            {
                                auto element = queryTasks.value();

                                task.ParseFromArray(element.data(), element.size());
                                //task.ParseFromArray(element, element.second.size());
                            }
                            this->cidData_.insert({ subTask.ipfsblock(), buffers->second.at(0) });
                            //this->ProcessSubTask2(subTask, result, initialHashCode, buffers->second.at(0));
                            this->m_processor->SetData(buffers);
                            auto tempresult = this->m_processor->StartProcessing(result, task, subTask);
                            std::string hashString(tempresult.begin(), tempresult.end());
                            result.set_result_hash(hashString);
                        }
                    }, "file");
            ioc->reset();
            ioc->run();
        }
        //TODO: Pass CIDData Into this and start processing
        //m_processor->SetData(cidData_);
        //m_processor->StartProcessing();
    }


    bool ProcessingCoreImpl::SetProcessingTypeFromJson(std::string jsondata)
    {
        rapidjson::Document doc;
        doc.Parse(jsondata.c_str());
        //Check if parsed
        if (!doc.IsObject()) {
            std::cerr << "Error parsing JSON" << std::endl;
            return false;
        }
        if (doc.HasMember("model") && doc["model"].IsObject()) {
            const rapidjson::Value& model = doc["model"];
            if (model.HasMember("name") && model["name"].IsString()) {
                std::string modelName = model["name"].GetString();
                std::cout << "Model name: " << modelName << std::endl;
                if (SetProcessorByName(modelName))
                {
                    return true;
                }
                else
                {
                    std::cerr << "No processor by name in settings json" << std::endl;
                }
            }
            else {
                std::cerr << "Model name not found or not a string" << std::endl;
                return false;
            }
        }
        else {
            std::cerr << "Model object not found or not an object" << std::endl;
            return false;
        }
    }
}