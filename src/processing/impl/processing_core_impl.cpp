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
        else {

        }
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

        //std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> mainbuffers;
        auto mainbuffers = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();

        //Get Image Async
        FileManager::GetInstance().InitializeSingletons();
        //string fileURL = "ipfs://" + subTask.ipfsblock() + "/test.png";
        string fileURL = "https://ipfs.filebase.io/ipfs/" + cid + "/settings.json";
        std::cout << "FILE URLL: " << fileURL << std::endl;
        auto data = FileManager::GetInstance().LoadASync(fileURL, false, false, ioc, [&result, &subTask, ioc, this](const int& status)
            {
                std::cout << "status: " << status << std::endl;
            }, [subTask, &result, ioc, &mainbuffers, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
                {
                    std::cout << "Final Callback" << std::endl;

                    if (!buffers || (buffers->first.empty() && buffers->second.empty()))
                    {
                        std::cout << "Buffer from AsyncIO is 0" << std::endl;
                        return;
                    }
                    else {
                        //Process settings json


                        mainbuffers->first.insert(mainbuffers->first.end(), buffers->first.begin(), buffers->first.end());
                        mainbuffers->second.insert(mainbuffers->second.end(), buffers->second.begin(), buffers->second.end());
                    }
                }, "file");
        ioc->reset();
        ioc->run();

        //Parse json
        size_t index = std::string::npos;
        for (size_t i = 0; i < mainbuffers->first.size(); ++i) {
            if (mainbuffers->first[i].find("settings.json") != std::string::npos) {
                index = i;
                break;
            }
        }
        if (index == std::string::npos)
        {
            std::cerr << "settings.json doesn't exist" << std::endl;
            return  mainbuffers;
        }
        std::vector<char>& jsonData = mainbuffers->second[index];
        std::string jsonString(jsonData.begin(), jsonData.end());

        //Set processor or fail.
        if (!this->SetProcessingTypeFromJson(jsonString))
        {
            std::cerr << "No processor available for this type:" << jsonString << std::endl;
            return  mainbuffers;
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
                return mainbuffers;
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
                return mainbuffers;
            }
        }

        //Make results keepers
        std::pair<std::vector<std::string>, std::vector<std::vector<char>>> modelData;
        std::pair<std::vector<std::string>, std::vector<std::vector<char>>> imageData;
        //Get Model
        string modelURL = "https://ipfs.filebase.io/ipfs/" + subTask.ipfsblock() + "/" + modelFile;
        GetSubCidForProc(ioc, modelURL, modelData);


        //Get Image, TODO: Update to grab multiple
        string imageUrl = "https://ipfs.filebase.io/ipfs/" + subTask.ipfsblock() + "/" + inputImage;
        GetSubCidForProc(ioc, imageUrl, imageData);

        //Run IO
        ioc->reset();
        ioc->run();
        //Insert data obtained
        mainbuffers->first.insert(mainbuffers->first.end(), modelData.first.begin(), modelData.first.end());
        mainbuffers->second.insert(mainbuffers->second.end(), modelData.second.begin(), modelData.second.end());
        mainbuffers->first.insert(mainbuffers->first.end(), imageData.first.begin(), imageData.first.end());
        mainbuffers->second.insert(mainbuffers->second.end(), imageData.second.begin(), imageData.second.end());

        return mainbuffers;
    }

    void ProcessingCoreImpl::GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc,std::string url, std::pair<std::vector<std::string>, std::vector<std::vector<char>>> &results)
    {
        //std::pair<std::vector<std::string>, std::vector<std::vector<char>>> results;
        auto modeldata = FileManager::GetInstance().LoadASync(url, false, false, ioc, [this](const int& status)
            {
                std::cout << "status: " << status << std::endl;
            }, [&results, this](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
                {
                    results.first.insert(results.first.end(), buffers->first.begin(), buffers->first.end());
                    results.second.insert(results.second.end(), buffers->second.begin(), buffers->second.end());
                }, "file");

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