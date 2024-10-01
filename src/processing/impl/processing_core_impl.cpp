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
        SGProcessing::Task task;
        auto queryTasks = m_db->Get("tasks/TASK_" + subTask.ipfsblock());
        if (queryTasks.has_value())
        {
            auto element = queryTasks.value();

            task.ParseFromArray(element.data(), element.size());
            //task.ParseFromArray(element, element.second.size());
        }
        else {
            std::cout << "No root task for subtask" << std::endl;
            return;
        }
        if (cidData_.find(subTask.ipfsblock()) == cidData_.end())
        {

            auto buffers = GetCidForProc(subTask.json_data(), task.json_data());


            this->cidData_.insert({ subTask.ipfsblock(), buffers });
            //this->ProcessSubTask2(subTask, result, initialHashCode, buffers->second.at(0));
            this->m_processor->SetData(buffers);
            auto tempresult = this->m_processor->StartProcessing(result, task, subTask);
            std::string hashString(tempresult.begin(), tempresult.end());
            result.set_result_hash(hashString);
        }
        else {
            auto buffers = cidData_.at(subTask.ipfsblock());

            // Set data and process
            this->m_processor->SetData(buffers);
            auto tempresult = this->m_processor->StartProcessing(result, task, subTask);
            std::string hashString(tempresult.begin(), tempresult.end());
            result.set_result_hash(hashString);
        }
    }


    std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> ProcessingCoreImpl::GetCidForProc(std::string json_data, std::string base_json)
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

        auto mainbuffers = std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();

        //Parse json to look for model/image
        rapidjson::Document document;
        document.Parse(json_data.c_str());

        rapidjson::Document base_document;
        base_document.Parse(base_json.c_str());

        std::string baseUrl = "";
        if (base_document.HasMember("data") && base_document["data"].IsObject())
        {
            const auto& input = base_document["data"];
            if (input.HasMember("URL") && input["URL"].IsString())
            {
                baseUrl = input["URL"].GetString();
                std::cout << "Base URL: " << baseUrl << std::endl;
            }
            else
            {
                std::cerr << "No Input file" << std::endl;
                return mainbuffers;
            }
        }
        std::string modelFile = "";
        // Extract model name
        if (base_document.HasMember("model") && base_document["model"].IsObject()) {
            const auto& model = base_document["model"];
            if (model.HasMember("file") && model["file"].IsString()) {
                modelFile = model["file"].GetString();
                std::cout << "Model File: " << modelFile << std::endl;
            }
            else {
                std::cerr << "No model file" << std::endl;
                return mainbuffers;
            }
        }

        std::vector<std::pair<std::string, std::string>> imageresults;
        if (document.HasMember("image") && document["image"].IsObject()) {
            const auto& image = document["image"].GetString();
        }
        else {
            std::cerr << "No input array" << std::endl;
            return mainbuffers;
        }
    
        //Init Loaders
        FileManager::GetInstance().InitializeSingletons();
        //Get Model
        string modelURL = baseUrl + modelFile;
        GetSubCidForProc(ioc, modelURL, mainbuffers);


        //Get Image, TODO: Update to grab multiple files if needed
        //string imageUrl = "https://ipfs.filebase.io/ipfs/" + cid + "/" + inputImage;
        //for (const auto& result : imageresults) {
        //    const auto& [url, image] = result;
        //    std::string fullUrl = url + image;
        //    GetSubCidForProc(ioc, fullUrl, mainbuffers);
        //}
        string imageUrl = baseUrl + modelFile;
        GetSubCidForProc(ioc, imageUrl, mainbuffers);
        //Run IO
        ioc->reset();
        ioc->run();

        return mainbuffers;
    }

    void ProcessingCoreImpl::GetSubCidForProc(std::shared_ptr<boost::asio::io_context> ioc,std::string url, std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>& results)
    {
        //std::pair<std::vector<std::string>, std::vector<std::vector<char>>> results;
        auto modeldata = FileManager::GetInstance().LoadASync(url, false, false, ioc, [](const sgns::AsyncError::CustomResult& status)
            {
                if (status.has_value())
                {
                    std::cout << "Success: " << status.value().message << std::endl;
                }
                else {
                    std::cout << "Error: " << status.error() << std::endl;
                }
            }, [&results](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
                {
                    results->first.insert(results->first.end(), buffers->first.begin(), buffers->first.end());
                    results->second.insert(results->second.end(), buffers->second.begin(), buffers->second.end());
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
                    return false;
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