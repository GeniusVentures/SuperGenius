#include "processing/impl/processing_core_impl.hpp"

#include <rapidjson/document.h>

#include "FileManager.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::processing, ProcessingCoreImpl::Error, e )
{
    using E = sgns::processing::ProcessingCoreImpl::Error;
    switch ( e )
    {
        case E::MAX_NUMBER_SUBTASKS:
            return "Maximal number of processed subtasks exceeded";
        case E::GLOBALDB_READ_ERROR:
            return "GlobaDB Read error ";
        case E::NO_BUFFER_FROM_JOB_DATA:
            return "No buffer from job data";
    }
    return "Unknown error";
}

namespace sgns::processing
{
    outcome::result<SGProcessing::SubTaskResult> ProcessingCoreImpl::ProcessSubTask(
        const SGProcessing::SubTask &subTask,
        uint32_t                     initialHashCode )
    {
        SGProcessing::SubTaskResult result;
        //Check if we're processing too much.
        std::scoped_lock<std::mutex> subTaskCountLock( m_subTaskCountMutex );
        ++m_processingSubTaskCount;
        if ( ( m_maximalProcessingSubTaskCount > 0 ) && ( m_processingSubTaskCount > m_maximalProcessingSubTaskCount ) )
        {
            // Reset the counter to allow processing restart
            m_processingSubTaskCount = 0;
            return outcome::failure( Error::MAX_NUMBER_SUBTASKS );
        }

        auto queryTasks = m_db->Get( "tasks/TASK_" + subTask.ipfsblock() );
        if ( !queryTasks.has_value() )
        {
            return outcome::failure( Error::GLOBALDB_READ_ERROR );
            //task.ParseFromArray(element, element.second.size());
        }
        SGProcessing::Task task;

        task.ParseFromArray( queryTasks.value().data(), queryTasks.value().size() );
        if ( cidData_.find( subTask.subtaskid() ) == cidData_.end() )
        {
            auto buffers = GetCidForProc( subTask.json_data(), task.json_data() );
            if ( buffers->first.size() <= 0 )
            {
                return outcome::failure( Error::NO_BUFFER_FROM_JOB_DATA );
            }

            this->cidData_.insert( { subTask.subtaskid(), buffers } );
            //this->ProcessSubTask2(subTask, result, initialHashCode, buffers->second.at(0));
            //this->m_processor->SetData(buffers);
            auto        tempresult = this->m_processor->StartProcessing( result,
                                                                  task,
                                                                  subTask,
                                                                  buffers->second,
                                                                  buffers->first );
            std::string hashString( tempresult.begin(), tempresult.end() );
            result.set_result_hash( hashString );
        }
        else
        {
            auto buffers = cidData_.at( subTask.subtaskid() );
            if ( buffers->first.size() <= 0 )
            {
                return outcome::failure( Error::NO_BUFFER_FROM_JOB_DATA );
            }
            // Set data and process
            //this->m_processor->SetData(buffers);
            auto        tempresult = this->m_processor->StartProcessing( result,
                                                                  task,
                                                                  subTask,
                                                                  buffers->second,
                                                                  buffers->first );
            std::string hashString( tempresult.begin(), tempresult.end() );
            result.set_result_hash( hashString );
        }
        return result;
    }

#include <condition_variable>
#include <mutex>

    std::shared_ptr<std::pair<std::vector<char>, std::vector<char>>> ProcessingCoreImpl::GetCidForProc(
        std::string json_data, std::string base_json)
    {
        auto mainbuffers = std::make_shared<std::pair<std::vector<char>, std::vector<char>>>();

        if (!this->SetProcessingTypeFromJson(base_json))
        {
            std::cerr << "No processor available for this type: " << base_json << std::endl;
            return mainbuffers;
        }

        // Parse JSON to extract URLs
        rapidjson::Document base_document;
        base_document.Parse(base_json.c_str());

        std::string baseUrl;
        if (base_document.HasMember("data") && base_document["data"].IsObject())
        {
            const auto& input = base_document["data"];
            if (input.HasMember("URL") && input["URL"].IsString())
            {
                baseUrl = input["URL"].GetString();
            }
            else
            {
                std::cerr << "No Input file" << std::endl;
                return mainbuffers;
            }
        }

        std::string modelFile;
        if (base_document.HasMember("model") && base_document["model"].IsObject())
        {
            const auto& model = base_document["model"];
            if (model.HasMember("file") && model["file"].IsString())
            {
                modelFile = model["file"].GetString();
            }
            else
            {
                std::cerr << "No model file" << std::endl;
                return mainbuffers;
            }
        }

        rapidjson::Document document;
        document.Parse(json_data.c_str());

        std::string image;
        if (document.HasMember("image") && document["image"].IsString())
        {
            image = document["image"].GetString();
        }
        else
        {
            std::cerr << "No input image" << std::endl;
            return mainbuffers;
        }

        FileManager::GetInstance().InitializeSingletons();

        // Synchronization variables
        std::mutex mutex;
        std::condition_variable cv;
        bool modelLoaded = false;
        bool imageLoaded = false;

        // Load Model File Asynchronously
        std::string modelURL = baseUrl + modelFile;
        FileManager::GetInstance().LoadASync(
            modelURL, false, false,
            [](const sgns::AsyncError::CustomResult& status)
            {
                if (status.has_value())
                {
                    std::cout << "Model load success: " << status.value().message << std::endl;
                }
                else
                {
                    std::cout << "Model load error: " << status.error() << std::endl;
                }
            },
            [&mainbuffers, &mutex, &cv, &modelLoaded](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
            {
                if (!buffers->second.empty())
                {
                    mainbuffers->first = buffers->second[0]; // Store model data
                }

                // Mark model as loaded and notify waiting thread
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    modelLoaded = true;
                }
                cv.notify_one();
            },
            "file");

        // Load Image File Asynchronously
        std::string imageUrl = baseUrl + image;
        FileManager::GetInstance().LoadASync(
            imageUrl, false, false,
            [](const sgns::AsyncError::CustomResult& status)
            {
                if (status.has_value())
                {
                    std::cout << "Image load success: " << status.value().message << std::endl;
                }
                else
                {
                    std::cout << "Image load error: " << status.error() << std::endl;
                }
            },
            [&mainbuffers, &mutex, &cv, &imageLoaded](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers)
            {
                if (!buffers->second.empty())
                {
                    mainbuffers->second = buffers->second[0]; // Store image data
                }

                // Mark image as loaded and notify waiting thread
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    imageLoaded = true;
                }
                cv.notify_one();
            },
            "file");
        FileManager::GetInstance().Start();
        // Wait until both operations complete
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&] { return modelLoaded && imageLoaded; });
        }

        // Both model and image data are now loaded
        return mainbuffers;
    }


    void ProcessingCoreImpl::GetSubCidForProc( std::string                              url,
                                               std::vector<char>                       &results )
    {
        //std::pair<std::vector<std::string>, std::vector<std::vector<char>>> results;
        auto modeldata = FileManager::GetInstance().LoadASync(
            url,
            false,
            false,
            []( const sgns::AsyncError::CustomResult &status )
            {
                if ( status.has_value() )
                {
                    std::cout << "Success: " << status.value().message << std::endl;
                }
                else
                {
                    std::cout << "Error: " << status.error() << std::endl;
                }
            },
            [&results]( std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers )
            {
                //results->first.insert(results->first.end(), buffers->first.begin(), buffers->first.end());
                //results->second.insert(results->second.end(), buffers->second.begin(), buffers->second.end());
                results.insert( results.end(), buffers->second[0].begin(), buffers->second[0].end() );
            },
            "file" );
    }

    bool ProcessingCoreImpl::SetProcessingTypeFromJson( std::string jsondata )
    {
        rapidjson::Document doc;
        doc.Parse( jsondata.c_str() );
        //Check if parsed
        if ( !doc.IsObject() )
        {
            std::cerr << "Error parsing JSON" << std::endl;
            return false;
        }
        if ( doc.HasMember( "model" ) && doc["model"].IsObject() )
        {
            const rapidjson::Value &model = doc["model"];
            if ( model.HasMember( "name" ) && model["name"].IsString() )
            {
                std::string modelName = model["name"].GetString();
                std::cout << "Model name: " << modelName << std::endl;
                if ( SetProcessorByName( modelName ) )
                {
                    return true;
                }

                std::cerr << "No processor by name in settings json" << std::endl;
                return false;
            }

            std::cerr << "Model name not found or not a string" << std::endl;
            return false;
        }

        std::cerr << "Model object not found or not an object" << std::endl;
        return false;
    }
}
