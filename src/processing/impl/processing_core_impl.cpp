#include <processing/impl/processing_core_impl.hpp>
#include <rapidjson/document.h>
namespace sgns::processing
{
    void ProcessingCoreImpl::ProcessSubTask(
        const SGProcessing::SubTask& subTask, SGProcessing::SubTaskResult& result,
        uint32_t initialHashCode)
    {
        //Check if we're processing too much.
        std::scoped_lock<std::mutex> subTaskCountLock(m_subTaskCountMutex);
        ++m_processingSubTaskCount;
        if ((m_maximalProcessingSubTaskCount > 0)
            && (m_processingSubTaskCount > m_maximalProcessingSubTaskCount))
        {
            // Reset the counter to allow processing restart
            m_processingSubTaskCount = 0;
            throw std::runtime_error("Maximal number of processed subtasks exceeded");
        }
        
        if (cidData_.find(subTask.ipfsblock()) == cidData_.end())
        {
            auto buffers = GetCidForProc(subTask, result, subTask.ipfsblock());
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
        std::cout << "Outside of processing core -------------------------------------------- " << std::endl;
        //TODO: Pass CIDData Into this and start processing
        //m_processor->SetData(cidData_);
        //m_processor->StartProcessing();
    }


    std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> ProcessingCoreImpl::GetCidForProc(const SGProcessing::SubTask& subTask, 
        SGProcessing::SubTaskResult& result, 
        std::string cid)
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

        std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> mainbuffers;
        //Get Image Async
        FileManager::GetInstance().InitializeSingletons();
        //string fileURL = "ipfs://" + subTask.ipfsblock() + "/test.png";
        string fileURL = "https://ipfs.filebase.io/ipfs/" + cid + "/settings.json";
        std::cout << "FILE URLL: " << fileURL << std::endl;
        auto data = FileManager::GetInstance().LoadASync(fileURL, false, false, ioc, [&result, &subTask, ioc, this](const int& status)
            {
                std::cout << "status: " << status << std::endl;
            }, [subTask, &result, ioc, mainbuffers, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
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

                        //Parse json
                        rapidjson::Document document;
                        document.Parse(jsonString.c_str());
                        std::string modelFile = "";
                        // Extract model name
                        if (document.HasMember("model") && document["model"].IsObject()) {
                            const auto& model = document["model"];
                            if (model.HasMember("file") && model["file"].IsString()) {
                                modelFile = model["file"].GetString();
                                std::cout << "Model File: " << modelFile << std::endl;
                            }
                            else {
                                std::cerr << "No model file" << std::endl;
                                return;
                            }
                        }
                        // Extract input image name
                        std::string inputImage = "";
                        if (document.HasMember("input") && document["input"].IsObject()) {
                            const auto& input = document["input"];
                            if (input.HasMember("image") && input["image"].IsString()) {
                                inputImage = input["image"].GetString();
                                std::cout << "Input Image: " << inputImage << std::endl;
                            }
                            else {
                                std::cerr << "No Input file" << std::endl;
                                return;
                            }
                        }
                        mainbuffers->first.insert(mainbuffers->first.end(), buffers->first.begin(), buffers->first.end());
                        mainbuffers->second.insert(mainbuffers->second.end(), buffers->second.begin(), buffers->second.end());
                        ////TODO: This is ugly and needs to be more elegant
                        string modelURL = "https://ipfs.filebase.io/ipfs/" + subTask.ipfsblock() + "/" + modelFile;
                        auto modeldata = FileManager::GetInstance().LoadASync(modelURL, false, false, ioc, [&result, &subTask, ioc, this](const int& status)
                            {
                                std::cout << "status: " << status << std::endl;
                            }, [subTask, &result, ioc, inputImage, mainbuffers, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> modelbuffers)
                                {
                                    if (!modelbuffers || (modelbuffers->first.empty() && modelbuffers->second.empty()))
                                    {
                                        std::cout << "Buffer from AsyncIO is 0" << std::endl;
                                        return;
                                    }
                                    else {
                                        mainbuffers->first.insert(mainbuffers->first.end(), modelbuffers->first.begin(), modelbuffers->first.end());
                                        mainbuffers->second.insert(mainbuffers->second.end(), modelbuffers->second.begin(), modelbuffers->second.end());
                                        string dataUrl = "https://ipfs.filebase.io/ipfs/" + subTask.ipfsblock() + "/" + inputImage;
                                        auto filedata = FileManager::GetInstance().LoadASync(dataUrl, false, false, ioc, [&result, &subTask, ioc, this](const int& status)
                                            {
                                                std::cout << "status: " << status << std::endl;
                                            }, [subTask, &result, ioc, mainbuffers, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> databuffers)
                                                {
                                                    if (!databuffers || (databuffers->first.empty() && databuffers->second.empty()))
                                                    {
                                                        std::cout << "Buffer from AsyncIO is 0" << std::endl;
                                                        return;
                                                    }
                                                    else {
                                                        mainbuffers->first.insert(mainbuffers->first.end(), databuffers->first.begin(), databuffers->first.end());
                                                        mainbuffers->second.insert(mainbuffers->second.end(), databuffers->second.begin(), databuffers->second.end());

                                                    }
                                                }, "file");
                                        //ioc->reset();
                                        //ioc->run();
                                    }

                                }, "file");
                    }
                }, "file");
        //std::cout << "Reset -----------------------" << std::endl;
        ioc->reset();
        ioc->run();
        return mainbuffers;
    }

    std::pair<std::vector<std::string>, std::vector<std::vector<char>>> ProcessingCoreImpl::GetSubCidForProc(std::string url)
    {
        std::pair<std::vector<std::string>, std::vector<std::vector<char>>> results;
        //auto modeldata = FileManager::GetInstance().LoadASync(url, false, false, [&results, this](const int& status)
        //    {
        //        std::cout << "status: " << status << std::endl;
        //    }, [&results, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
        //        {
        //            results.first.insert(result.first.end(), buffers->first.begin(), buffers->second.end());
        //            results.second.insert(result.second.end(), buffers->second.begin(), buffers->second.end());
        //        }, "file");

        return results;

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